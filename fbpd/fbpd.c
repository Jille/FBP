#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "fbp.h"

int fd;
struct sockaddr_in addr;
socklen_t addrlen;
struct Announcement apkt;

void
send_announce_packet() {
	sendto(fd, &apkt, sizeof(apkt), 0, (struct sockaddr *)&addr, addrlen);
}

int
main(int argc, char **argv) {
	if(argc != 3) {
		fprintf(stderr, "Usage: %s <fid> <file>\n", argv[0]);
		return 1;
	}

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("255.255.255.255");
	addr.sin_port = htons(FBP_DEFAULT_PORT);
	addrlen = sizeof(addr);

	bzero(&apkt, sizeof(apkt));
	apkt.zero = 0;
	apkt.announceVer = FBP_ANNOUNCE_VERSION;
	apkt.status = FBP_STATUS_WAITING;
	apkt.numPackets = 0; // XXX
	strncpy(apkt.filename, basename(argv[2]), sizeof(apkt.filename));

	if((fd = socket(addr.sin_family, SOCK_DGRAM, 0)) == -1) {
		err(1, "socket");
	}

	int opt = 1;
	if(setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) == -1) {
		err(1, "setsockopt");
	}

	while(1) {
		send_announce_packet();
		sleep(1);
	}

	return 0;
}
