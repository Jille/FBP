#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "fbp.h"
#include "bitmask.h"

#ifndef MAX
#define	MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif
#ifndef MIN
#define	MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

struct transfer {
	unsigned char fileid;
	int fd;
	pkt_count offset;
	struct timeval start;
	BM_DEFINE(bitmask);
};

int sfd;
struct sockaddr_in addr;
socklen_t addrlen;
struct transfer *transfers[(sizeof(unsigned char) * 256) - 1];

void
start_transfer(struct Announcement *apkt, struct sockaddr_in *raddr, socklen_t raddrlen) {
	assert(transfers[apkt->fileid] == NULL);

	struct transfer *t = calloc(1, sizeof(struct transfer));
	transfers[apkt->fileid] = t;

	char *fname;
	asprintf(&fname, "data/%s", apkt->filename);
	t->fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0644);
	free(fname);
	if(t->fd == -1) {
		err(1, "open");
	}
	t->fileid = apkt->fileid;
	t->offset = 0;
	BM_INIT(t->bitmask, apkt->numPackets);
	gettimeofday(&t->start, NULL);
}

void
handle_announcement(struct Announcement *apkt, ssize_t pktlen, struct sockaddr_in *raddr, socklen_t raddrlen) {
	if(apkt->announceVer > FBP_ANNOUNCE_VERSION) {
		printf("handle_announcement(): Dropping too high version\n");
		return;
	}
	if(transfers[apkt->fileid] == NULL) {
		printf("handle_announcement(): Unknown file-id %d; starting transfer\n", apkt->fileid);
		start_transfer(apkt, raddr, raddrlen);
	}
	struct transfer *t = transfers[apkt->fileid];
	if(t->fd == -1) {
		return;
	}
	if(apkt->status == FBP_STATUS_TRANSFERRING) {
		printf("handle_announcement(): [%d] Transfer is running; I can wait\n", apkt->fileid);
		return;
	}

	pkt_count n;
	struct RequestPacket rpkt;
	bzero(&rpkt, sizeof(rpkt));
	int done = 1;

	rpkt.fileid = t->fileid;
	int rid = 0;
	rpkt.requests[rid].offset = 0;
	rpkt.requests[rid].num = 0;
	for(n = 0; apkt->numPackets > n; n++) {
		if(!BM_ISSET(t->bitmask, n)) {
			if(rpkt.requests[rid].num == 0) {
				rpkt.requests[rid].offset = n;
			}
			rpkt.requests[rid].num++;
		} else if(rpkt.requests[rid].num > 0) {
			printf("handle_announcement(): [%d] Requesting %d packets from offset %d (in rid %d)\n", apkt->fileid, rpkt.requests[rid].num, rpkt.requests[rid].offset, rid);
			if(rid < FBP_REQUESTS_PER_PACKET - 1) {
				rid++;
			} else {
				done = 0;
				if(sendto(sfd, &rpkt, sizeof(rpkt), 0, (struct sockaddr *)raddr, raddrlen) == -1) {
					err(1, "sendto");
				}
				for(rid = 0; FBP_REQUESTS_PER_PACKET > rid; rid++) {
					rpkt.requests[rid].offset = 0;
					rpkt.requests[rid].num = 0;
				}
				rid = 0;
			}
		}
	}
	if(rpkt.requests[rid].num > 0) {
		printf("handle_announcement(): [%d] Requesting %d packets from offset %d\n", apkt->fileid, rpkt.requests[rid].num, rpkt.requests[rid].offset);
		rid++;
	}
	if(rid > 0) {
		done = 0;
		if(sendto(sfd, &rpkt, sizeof(rpkt), 0, (struct sockaddr *)raddr, raddrlen) == -1) {
			err(1, "sendto");
		}
	}

	if(done) {
		struct timeval now;
		gettimeofday(&now, NULL);
		now.tv_sec -= t->start.tv_sec;
		now.tv_usec -= t->start.tv_usec;
		if(now.tv_usec < 0) {
			now.tv_usec += 1000000;
			now.tv_sec++;
		}
		printf("handle_announcement(): [%d] Ready in %ld.%ld seconds\n", apkt->fileid, now.tv_sec, now.tv_usec);
		char checksum[sizeof(apkt->checksum)];
		sha1_file(checksum, t->fd);
		if(strncmp(apkt->checksum, checksum, sizeof(checksum)) != 0) {
			int n;
			printf("handle_announcement(): [%d] Checksum mismatch: %.*s != %.*s. Restarting transfer.\n", apkt->fileid, sizeof(checksum), apkt->checksum, sizeof(checksum), checksum);
			for(n = 0; apkt->numPackets > n; n++) {
				BM_CLR(t->bitmask, n);
			}
		} else {
			close(t->fd);
			t->fd = -1;
		}
	}
}

void
handle_datapacket(struct DataPacket *dpkt, ssize_t pktlen) {
	if(transfers[dpkt->fileid] == NULL) {
		// XXX bufferen ?
		return;
	}
	struct transfer *t = transfers[dpkt->fileid];
	if(t->fd == -1) {
		// transfer is complete
		return;
	}
	if(t->offset != dpkt->offset) {
		if(lseek(t->fd, dpkt->offset * FBP_PACKET_DATASIZE, SEEK_SET) == -1) {
			err(1, "lseek");
		}
	}
	if(write(t->fd, dpkt->data, MIN(FBP_PACKET_DATASIZE, FBP_PACKET_DATASIZE - sizeof(struct DataPacket) + pktlen)) == -1) {
		err(1, "write");
	}
	t->offset = dpkt->offset+1;
	BM_SET(t->bitmask, dpkt->offset);
}

int
main(int argc, char **argv) {
	assert((1 >> 1) == 0 /* require little endian */);

	bzero(&transfers, sizeof(transfers));

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(FBP_DEFAULT_PORT);
	addrlen = sizeof(addr);

	if((sfd = socket(addr.sin_family, SOCK_DGRAM, 0)) == -1) {
		err(1, "socket");
	}

	if(bind(sfd, (struct sockaddr *)&addr, addrlen) == -1) {
		err(1, "bind");
	}

	while(1) {
		struct sockaddr_in raddr;
		socklen_t raddrlen = sizeof(raddr);
		ssize_t len;
		char buf[MAX(sizeof(struct DataPacket), sizeof(struct Announcement))];

		len = recvfrom(sfd, buf, sizeof(buf), 0, (struct sockaddr *)&raddr, &raddrlen);

		if(buf[0] == 0) {
			handle_announcement((struct Announcement *)buf, len, &raddr, raddrlen);
		} else {
			handle_datapacket((struct DataPacket *)buf, len);
		}
	}

	return 0;
}
