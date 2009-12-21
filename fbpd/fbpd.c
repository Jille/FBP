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

void
send_announce_packet() {
	if(sendto(sfd, &apkt, sizeof(apkt), 0, (struct sockaddr *)&addr, addrlen) == -1) {
		err(1, "sendto");
	}
}

void
read_request() {
	struct RequestPacket rpkt;
	if(recv(sfd, &rpkt, sizeof(rpkt), 0) == -1) {
		err(1, "recv");
	}
	if(rpkt.offset > apkt.numPackets || rpkt.offset + rpkt.num > apkt.numPackets) {
		return;
	}
	pkt_count n;
	for(n = rpkt.offset; rpkt.offset + rpkt.num > n; n++) {
		BM_SET(bitmask, n);
	}
	apkt.status = FBP_STATUS_TRANSFERRING;
}

void
transfer_packet() {
	pkt_count n = offset;
	while(!BM_ISSET(bitmask, n)) {
		n = (n+1) % apkt.numPackets;
		if(n == offset) {
			apkt.status = FBP_STATUS_WAITING;
			return;
		}
	}

	struct DataPacket pkt;
	ssize_t len;
	pkt.fileid = fileid;
	pkt.offset = n;
	// pkt.checksum XXX
	if(n != offset) {
		if(lseek(ffd, n*FBP_PACKET_DATASIZE, SEEK_SET) == -1) {
			err(1, "lseek");
		}
	}
	if((len = read(ffd, pkt.data, FBP_PACKET_DATASIZE)) == -1) {
		err(1, "read");
	}
	BM_CLR(bitmask, n);
	offset = n;

	if(sendto(sfd, &pkt, sizeof(pkt) + FBP_PACKET_DATASIZE - len, 0, (struct sockaddr *)&addr, addrlen) == -1) {
		err(1, "sendto");
	}
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

	offset = 0;

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("255.255.255.255");
	addr.sin_port = htons(FBP_DEFAULT_PORT);
	addrlen = sizeof(addr);

	bzero(&apkt, sizeof(apkt));
	apkt.zero = 0;
	apkt.announceVer = FBP_ANNOUNCE_VERSION;
	apkt.fileid = fileid;
	apkt.status = FBP_STATUS_WAITING;
	apkt.numPackets = ceil(st.st_size / FBP_PACKET_DATASIZE);
	strncpy(apkt.filename, basename(argv[2]), sizeof(apkt.filename));
	apkt.filename[sizeof(apkt.filename)] = 0;

	char hash[41];
	SHA1_File(argv[1], hash);
	strncpy(apkt.checksum, hash, 40);

	BM_INIT(bitmask, apkt.numPackets);

	if((sfd = socket(addr.sin_family, SOCK_DGRAM, 0)) == -1) {
		err(1, "socket");
	}

	int opt = 1;
	if(setsockopt(sfd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) == -1) {
		err(1, "setsockopt");
	}

	while(1) {
		send_announce_packet();
		sleep(1);
	}
	return 0;

	while(1) {
		send_announce_packet();
		if(apkt.status == FBP_STATUS_WAITING) {
			sleep(1);
		} else {
			time_t start, now;
			time(&start);
			do {
				transfer_packet();
				time(&now);
			} while(start == now && apkt.status == FBP_STATUS_TRANSFERRING);
		}

		struct timeval tmo = { 0, 0 };
		while(1) {
			fd_set fds;
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
	}

	return 0;
}
