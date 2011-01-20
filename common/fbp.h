#ifndef FBP_GLOBAL_H
#define FBP_GLOBAL_H

#include <inttypes.h>

#define FBP_DEFAULT_PORT        1026
#define FBP_PACKET_DATASIZE     1024
#define FBP_ANNOUNCE_VERSION    2
#define FBP_STATUS_WAITING      0
#define FBP_STATUS_TRANSFERRING 1
#define FBP_REQUESTS_PER_PACKET 30

typedef int32_t pkt_count;

struct Announcement
{
  char zero;            // ALWAYS 0, means this is an announcement packet
  char announceVer;     // See FBP_ANNOUNCE_VERSION (must be equal to process)
  unsigned char fileid; // ID of the file (must be > 0)
  char status;          // 0=waiting, 1=transferring
  pkt_count numPackets; // 4 bytes: number of packets
  char filename[256];   // 256 bytes of filename
  char checksum[40];    // complete SHA1 checksum of the file
} __attribute__((__packed__));

struct _requestData {
	pkt_count offset;     // first packet we want to receive
	pkt_count num;        // number of packets we want to receive
} __attribute__((__packed__));

struct RequestPacket
{
  unsigned char fileid;          // ID of the file (must be > 0)
	struct _requestData requests[FBP_REQUESTS_PER_PACKET];
} __attribute__((__packed__));

struct DataPacket
{
  unsigned char fileid;          // ID of the file (must be > 0)
  char unused1;         // unused
  unsigned short size;
  pkt_count offset;     // offset number of this packet
  char data[FBP_PACKET_DATASIZE]; // the actual data
} __attribute__((__packed__));

void sha1_file(char *, int);

#endif // FBP_GLOBAL_H
