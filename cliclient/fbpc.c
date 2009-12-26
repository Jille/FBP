#include <arpa/inet.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <math.h>
#include <netinet/in.h>
#include <sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
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
	t->fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	free(fname);
	if(t->fd == -1) {
		err(1, "open");
	}
	t->fileid = apkt->fileid;
	t->offset = 0;
	BM_INIT(t->bitmask, apkt->numPackets);
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
	int done = 1;

	rpkt.fileid = t->fileid;
	rpkt.offset = 0;
	rpkt.num = 0;
	for(n = 0; apkt->numPackets > n; n++) {
		if(!BM_ISSET(t->bitmask, n)) {
			if(rpkt.num == 0) {
				rpkt.offset = n;
			}
			rpkt.num++;
		} else if(rpkt.num > 0) {
			printf("handle_announcement(): [%d] Requesting %d packets from offset %d\n", apkt->fileid, rpkt.num, rpkt.offset);
			done = 0;
			if(sendto(sfd, &rpkt, sizeof(rpkt), 0, (struct sockaddr *)raddr, raddrlen) == -1) {
				err(1, "sendto");
			}
			rpkt.offset = 0;
			rpkt.num = 0;
		}
	}
	if(rpkt.num > 0) {
		printf("handle_announcement(): [%d] Requesting %d packets from offset %d\n", apkt->fileid, rpkt.num, rpkt.offset);
		done = 0;
		if(sendto(sfd, &rpkt, sizeof(rpkt), 0, (struct sockaddr *)raddr, raddrlen) == -1) {
			err(1, "sendto");
		}
	}

	if(done) {
		printf("handle_announcement(): [%d] Ready!\n", apkt->fileid);
		close(t->fd);
		t->fd = -1;
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
