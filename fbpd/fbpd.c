#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <netinet/in.h>
#include "fbp.h"

int sfd, ffd;
off_t fpos;
struct sockaddr_in addr;
socklen_t addrlen;
struct Announcement apkt;
int *bitmask;

#define BM_SET(m, n) m[n/sizeof(int)] |= (1 << (n % sizeof(int)))
#define BM_CLR(m, n) m[n/sizeof(int)] &= ~(1 << (n % sizeof(int)))
#define BM_ISSET(m, n) (m[n/sizeof(int)] & (1 << (n % sizeof(int))) != 0)

void
send_announce_packet() {
	sendto(sfd, &apkt, sizeof(apkt), 0, (struct sockaddr *)&addr, addrlen);
}

void
read_request() {
	char buf[128];
	ssize_t len = recv(sfd, buf, sizeof(buf), 0);
	// XXX
}

int
main(int argc, char **argv) {
	struct stat st;
	if(argc != 3) {
		fprintf(stderr, "Usage: %s <fid> <file>\n", argv[0]);
		return 1;
	}

	if((ffd = open(argv[2], O_RDONLY)) == -1) {
		err(1, "open(%s)", argv[2]);
	}

	if(fstat(ffd, &st) == -1) {
		err(1, "fstat(%s)", argv[2]);
	}

	fpos = 0;

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("255.255.255.255");
	addr.sin_port = htons(FBP_DEFAULT_PORT);
	addrlen = sizeof(addr);

	bzero(&apkt, sizeof(apkt));
	apkt.zero = 0;
	apkt.announceVer = FBP_ANNOUNCE_VERSION;
	apkt.status = FBP_STATUS_WAITING;
	apkt.numPackets = ceil(st.st_size / FBP_PACKET_DATASIZE);
	strncpy(apkt.filename, basename(argv[2]), sizeof(apkt.filename));
	apkt.filename[sizeof(apkt.filename)] = 0;

	bitmask = calloc(1, apkt.numPackets / sizeof(int));

	if((sfd = socket(addr.sin_family, SOCK_DGRAM, 0)) == -1) {
		err(1, "socket");
	}

	int opt = 1;
	if(setsockopt(sfd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) == -1) {
		err(1, "setsockopt");
	}

	send_announce_packet();
	return 0;

	while(1) {
		fd_set fds;
		struct timeval tmo = { 0, 0 };
		FD_SET(sfd, &fds);
		switch(select(sfd+1, &fds, NULL, NULL, &tmo)) {
			case -1:
				err(1, "select");
			case 0:
				break;
			case 1:
				read_request();
				break;
		}
		send_announce_packet();
		sleep(1);
	}

	return 0;
}
