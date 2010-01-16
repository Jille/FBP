#include <arpa/inet.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
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
#include <time.h>
#include <unistd.h>
#include "fbp.h"
#include "bitmask.h"

int sfd, ffd;
pkt_count offset;
unsigned char fileid;
struct sockaddr_in addr;
socklen_t addrlen;
struct Announcement apkt;
BM_DEFINE(bitmask);

// Rate limiting
struct timeval packetInterval = {0, 0};
int announce_rate = 1;

#define	UPDATE_ANNOUNCE_RATE()	do { announce_rate = 10000 /*1000000 / packetInterval.tv_usec */; printf("New announce rate: %d\n", announce_rate); } while(0)

ssize_t
fbp_sendto(const void *buf, size_t len) {
	ssize_t res;
	int errors = 0;
	while((res = sendto(sfd, buf, len, 0, (struct sockaddr *)&addr, addrlen)) == -1) {
		if(errno == ENOBUFS && errors < 100) {
			usleep(10000);
			if(!errors) {
				packetInterval.tv_usec += 1000;
				if(packetInterval.tv_usec > 1000000) {
					packetInterval.tv_usec = 1000000;
				}
				UPDATE_ANNOUNCE_RATE();
				errors++;
				puts("Error while sending; slowing down");
			}
		} else {
			err(1, "sendto");
		}
	}
	return res;
}

void
send_announce_packet() {
	printf("Announcing file %d\n", fileid);
	fbp_sendto(&apkt, sizeof(apkt));
}

void
read_request() {
	struct RequestPacket rpkt;
	int i;
	if(recv(sfd, &rpkt, sizeof(rpkt), 0) == -1) {
		err(1, "recv");
	}
	for(i=0; 30 > i; i++) {
		if(rpkt.requests[i].offset > apkt.numPackets || rpkt.requests[i].offset + rpkt.requests[i].num > apkt.numPackets) {
			printf("Received invalid request range for fileid %d\n", rpkt.fileid);
			return;
		}
		pkt_count n;
		for(n = rpkt.requests[i].offset; rpkt.requests[i].offset + rpkt.requests[i].num > n; n++) {
			BM_SET(bitmask, n);
		}
	}
	apkt.status = FBP_STATUS_TRANSFERRING;
	puts("Going into transfermode");
}

void
transfer_packet() {
	pkt_count n = offset % apkt.numPackets;
	// printf("Attempting to transfer packet; my previous was %d; and there are %d packets\n", n, apkt.numPackets);
	if(apkt.numPackets != 1) {
		while(!BM_ISSET(bitmask, n)) {
			n = (n+1) % apkt.numPackets;
			if(n == (offset % apkt.numPackets)) {
				apkt.status = FBP_STATUS_WAITING;
				puts("Going into idlemode");
				return;
			}
		}
	}

	struct DataPacket pkt;
	ssize_t len;
	pkt.fileid = fileid;
	pkt.offset = n;
	// pkt.checksum XXX
	if(n != offset) {
		printf("I was at the wrong offset (%d instead of %d)\n", offset, n);
		if(lseek(ffd, n*FBP_PACKET_DATASIZE, SEEK_SET) == -1) {
			err(1, "lseek");
		}
	}
	// printf("Yo, I'm going to read offset %d. So, I'm at %ld now.\n", n, (long)lseek(ffd, 0, SEEK_CUR));
	if((len = read(ffd, pkt.data, FBP_PACKET_DATASIZE)) == -1) {
		err(1, "read");
	}
	assert(len > 0 && (len == FBP_PACKET_DATASIZE || n == apkt.numPackets - 1));
	BM_CLR(bitmask, n);
	offset = n + 1;

	// printf("Sending offset %d with data: %.*s\n", pkt.offset, len, pkt.data);

	fbp_sendto(&pkt, sizeof(pkt) - FBP_PACKET_DATASIZE + len);
}

int
main(int argc, char **argv) {
	assert((1 >> 1) == 0 /* require little endian */);
	struct stat st;
	if(argc != 3) {
		fprintf(stderr, "Usage: %s <fid> <file>\n", argv[0]);
		return 1;
	}

	fileid = strtol(argv[1], (char **)NULL, 10);
	if(fileid < 1) {
		fprintf(stderr, "%s: fid must be between 1 and 255\n", argv[0]);
		return 1;
	}

	if((ffd = open(argv[2], O_RDONLY)) == -1) {
		err(1, "open(%s)", argv[2]);
	}

	if(fstat(ffd, &st) == -1) {
		err(1, "fstat(%s)", argv[2]);
	}

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("192.168.0.255");
	addr.sin_port = htons(FBP_DEFAULT_PORT);
	addrlen = sizeof(addr);

	bzero(&apkt, sizeof(apkt));
	apkt.zero = 0;
	apkt.announceVer = FBP_ANNOUNCE_VERSION;
	apkt.fileid = fileid;
	apkt.status = FBP_STATUS_WAITING;
	apkt.numPackets = ceil(st.st_size / (double)FBP_PACKET_DATASIZE);
	strncpy(apkt.filename, basename(argv[2]), sizeof(apkt.filename));
	apkt.filename[sizeof(apkt.filename)] = 0;

	sha1_file(apkt.checksum, ffd);
	offset = apkt.numPackets;

	BM_INIT(bitmask, apkt.numPackets);

/*
	apkt.status = FBP_STATUS_TRANSFERRING;
	BM_SET(bitmask, 0);
*/

	if((sfd = socket(addr.sin_family, SOCK_DGRAM, 0)) == -1) {
		err(1, "socket");
	}

	int opt = 1;
	if(setsockopt(sfd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) == -1) {
		err(1, "setsockopt");
	}

	int i = 0;
	UPDATE_ANNOUNCE_RATE();
	while(1) {
		fd_set fds;
		struct timeval tmo = { 0, 0 };

		if(apkt.status == FBP_STATUS_WAITING) {
			send_announce_packet();
			tmo.tv_sec = 1;
			tmo.tv_usec = 0;
		} else {
			if(++i >= announce_rate) {
				send_announce_packet();
				i = 0;
			}
			tmo = packetInterval;
			transfer_packet();
		}

		FD_ZERO(&fds);
		FD_SET(sfd, &fds);
		switch(select(sfd+1, &fds, NULL, NULL, &tmo)) {
			case -1:
				err(1, "select");
			case 0:
				continue;
			case 1:
				read_request();
				break;
		}
	}

	return 0;
}
