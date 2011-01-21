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

#define	IS_PAST(tf, tl)	(((tf).tv_sec == (tl).tv_sec) ? ((tf).tv_usec > (tl).tv_usec) : ((tf).tv_sec > (tl).tv_sec))
#define	TIMEVAL_IS_ZERO(tv)	((tv).tv_sec == 0 && (tv).tv_usec == 0)
#define	TIMEVAL_SUBSTRACT(th, tl)	(((th).tv_sec - (tl).tv_sec) * 1000000 + (th).tv_usec - (tl).tv_usec)
#define	TIMEVAL_SET(tv, sec, usec)	do { (tv).tv_sec = sec; (tv).tv_usec = usec; } while(0)
#define	TIMEVAL_CLEAR(tv)	TIMEVAL_SET((tv), 0, 0)

int sfd, ffd;
pkt_count offset;
unsigned char fileid;
struct sockaddr_in addr;
socklen_t addrlen;
struct Announcement apkt;
int packets_queued = 0;
BM_DEFINE(bitmask);

#ifdef RATE_LIMIT
int limit_pps = 10000;
#endif

static void inline
request_packet(int n) {
	if(!BM_ISSET(bitmask, n)) {
		packets_queued++;
		BM_SET(bitmask, n);
	}
}

static void inline
fbp_sendto(const void *buf, size_t len) {
	size_t res = sendto(sfd, buf, len, 0, (struct sockaddr *)&addr, addrlen);
	if(res == -1) {
		err(1, "sendto()");
	}
}

void
transmit_announce_packet() {
	printf("Announcing file %d\n", fileid);
	apkt.status = (packets_queued > 0) ? FBP_STATUS_TRANSFERRING : FBP_STATUS_WAITING;
	fbp_sendto(&apkt, sizeof(apkt));
}

void
transmit_data_packet() {
	assert(packets_queued > 0);
	pkt_count n = offset % apkt.numPackets;
	while(!BM_ISSET(bitmask, n)) {
		n = (n+1) % apkt.numPackets;
	}

	struct DataPacket pkt;
	ssize_t len;
	pkt.fileid = fileid;
	pkt.offset = n;
	if(n != offset) {
		if(lseek(ffd, n*FBP_PACKET_DATASIZE, SEEK_SET) == -1) {
			err(1, "lseek");
		}
	}
	// printf("Yo, I'm going to read offset %d. So, I'm at %ld now.\n", n, (long)lseek(ffd, 0, SEEK_CUR));
	if((len = read(ffd, pkt.data, FBP_PACKET_DATASIZE)) == -1) {
		err(1, "read");
	}
	pkt.size = len;
	assert(len > 0);
	assert(len == FBP_PACKET_DATASIZE || n == apkt.numPackets - 1);

	fbp_sendto(&pkt, sizeof(pkt) - FBP_PACKET_DATASIZE + len);

	offset = n + 1;
	packets_queued--;
	BM_CLR(bitmask, n);
}

void
receive_packet() {
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
			request_packet(n);
		}
	}
}

void
usage(char *progname) {
#ifdef RATE_LIMIT
	fprintf(stderr, "Usage: %s [-b 192.168.0.255] [-p 100000] <fid> <file>\n", progname);
#else
	fprintf(stderr, "Usage: %s [-b 192.168.0.255] <fid> <file>\n", progname);
#endif
	exit(1);
}

int
main(int argc, char **argv) {
	struct stat st;
	fd_set rfds, wfds;
	int want_announce = 1;
	struct timeval now = { 0, 0 };
#ifdef RATE_LIMIT
	int patsts = limit_pps; // Packets allowed to send this second
	struct timeval nextPacket = { 0, 0 };
#endif
	char ch;
	char *bcast_addr = "127.0.0.1";

	assert((1 >> 1) == 0 /* require little endian */);

	while((ch = getopt(argc, argv, "b:p:")) != -1) {
		switch(ch) {
			case 'b':
				bcast_addr = optarg;
				break;
#ifdef RATE_LIMIT
			case 'p':
				limit_pps = strtol(optarg, (char **)NULL, 10);
				if(limit_pps < 1 || limit_pps >= 1000000) {
					fprintf(stderr, "%s: rate limi must be between 1 and 1000000\n", argv[0]);
					usage(argv[0]);
				}
				break;
#endif
			default:
				usage(argv[0]);
		}
	}

	if(argc - optind != 2) {
		usage(argv[0]);
	}

	fileid = strtol(argv[optind + 0], (char **)NULL, 10);
	if(fileid < 1) {
		fprintf(stderr, "%s: fid must be between 1 and 255\n", argv[0]);
		usage(argv[0]);
	}

	if((ffd = open(argv[optind + 1], O_RDONLY)) == -1) {
		err(1, "open(%s)", argv[optind + 1]);
	}

	if(fstat(ffd, &st) == -1) {
		err(1, "fstat(%s)", argv[optind + 1]);
	}

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(bcast_addr);
	addr.sin_port = htons(FBP_DEFAULT_PORT);
	addrlen = sizeof(addr);

	bzero(&apkt, sizeof(apkt));
	apkt.zero = 0;
	apkt.announceVer = FBP_ANNOUNCE_VERSION;
	apkt.fileid = fileid;
	apkt.status = FBP_STATUS_WAITING;
	apkt.numPackets = ceil(st.st_size / (double)FBP_PACKET_DATASIZE);
	strncpy(apkt.filename, basename(argv[optind + 1]), sizeof(apkt.filename));
	apkt.filename[sizeof(apkt.filename) - 1] = 0;

	sha1_file(apkt.checksum, ffd);
	offset = apkt.numPackets;

	BM_INIT(bitmask, apkt.numPackets);

	if((sfd = socket(addr.sin_family, SOCK_DGRAM, 0)) == -1) {
		err(1, "socket");
	}

	int opt = 1;
	if(setsockopt(sfd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) == -1) {
		err(1, "setsockopt");
	}

	while(1) {
		struct timeval tmo = {0, 0};
		int old_tv_sec = now.tv_sec;
		int n;

		gettimeofday(&now, NULL);
		if(now.tv_sec != old_tv_sec) {
			want_announce = 1;
#ifdef RATE_LIMIT
			patsts = limit_pps;
			TIMEVAL_CLEAR(nextPacket);
			// puts("time barried killed");
#endif
		}
#ifdef RATE_LIMIT
		else if(IS_PAST(now, nextPacket)) {
/*
			if(!TIMEVAL_IS_ZERO(nextPacket)) {
				puts("time barrier passed");
			}
*/
			TIMEVAL_CLEAR(nextPacket);
		}
#endif

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(sfd, &rfds);
#ifdef RATE_LIMIT
		if(want_announce || (packets_queued && TIMEVAL_IS_ZERO(nextPacket)))
#else
		if(want_announce || packets_queued)
#endif
		{
			FD_SET(sfd, &wfds);
		}

#ifndef RATE_LIMIT
		if(want_announce) {
			n = select(sfd+1, &rfds, &wfds, NULL, NULL);
		} else {
#endif
			tmo.tv_usec = 999999 - now.tv_usec;
#ifdef RATE_LIMIT
			if(patsts > 0 && !TIMEVAL_IS_ZERO(nextPacket)) {
				tmo.tv_usec = MIN(tmo.tv_usec, TIMEVAL_SUBSTRACT(nextPacket, now));
			}
/*
			printf("Will select for %ld us%s\n", tmo.tv_usec, FD_ISSET(sfd, &wfds) ? " or write unlock" : "");
*/
			if(tmo.tv_usec < 0 || tmo.tv_usec >= 1000000) {
				printf("tmo: %ld.%ld\n", tmo.tv_sec, tmo.tv_usec);
				printf("now: %ld.%ld\n", now.tv_sec, now.tv_usec);
				printf("nextPacket: %ld.%ld\n", nextPacket.tv_sec, nextPacket.tv_usec);
			}
			assert(tmo.tv_usec >= 0);
			assert(tmo.tv_usec < 1000000);
#endif
			n = select(sfd+1, &rfds, &wfds, NULL, &tmo);
#ifndef RATE_LIMIT
		}
#endif
		switch(n) {
			case -1:
				err(1, "select");
			case 0:
				continue;
			default:
				if(FD_ISSET(sfd, &wfds)) {
					if(want_announce) {
						transmit_announce_packet();
						want_announce = 0;
					} else {
						transmit_data_packet();
#ifdef RATE_LIMIT
						// printf("Sent %d/%d packet this second\n", limit_pps - patsts, limit_pps);
						assert(TIMEVAL_IS_ZERO(nextPacket));
						assert(patsts > 0);
						patsts--;
						nextPacket.tv_sec = now.tv_sec;
						nextPacket.tv_usec = now.tv_usec + (1000000 / limit_pps);
						while(nextPacket.tv_usec > 1000000) {
							nextPacket.tv_sec++;
							nextPacket.tv_usec -= 1000000;
						}
/*
						printf("Set time barrier to %ld.%ld\n", nextPacket.tv_sec, nextPacket.tv_usec);
*/
#endif
					}
				}
				if(FD_ISSET(sfd, &rfds)) {
					receive_packet();
				}
				break;
		}
	}

	return 0;
}
