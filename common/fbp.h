#ifndef FBP_GLOBAL_H
#define FBP_GLOBAL_H

#include <inttypes.h>

#define FBP_DEFAULT_PORT        1026
#define FBP_PACKET_DATASIZE     4096
#define FBP_ANNOUNCE_VERSION    1
#define FBP_STATUS_WAITING      0
#define FBP_STATUS_TRANSFERRING 1

typedef int32_t pkt_count;

struct Announcement
{
  char zero;            // ALWAYS 0, means this is an announcement packet
  char announceVer;     // ALWAYS 1, means this is version 1 of the announcement
  unsigned char fileid; // ID of the file (must be > 0)
  char status;          // 0=waiting, 1=transferring
  pkt_count numPackets; // 4 bytes: number of packets
  char filename[256];   // 256 bytes of filename
  char checksum[40];    // complete SHA1 checksum of the file
} __attribute__((__packed__));

struct RequestPacket
{
  unsigned char fileid;          // ID of the file (must be > 0)
  pkt_count offset;     // first packet we want to receive
  pkt_count num;        // number of packets we want to receive
} __attribute__((__packed__));

struct DataPacket
{
  unsigned char fileid;          // ID of the file (must be > 0)
  char unused1;         // unused
  char unused2;         // unused
  char checksum[5];     // checksum of this packet
  pkt_count offset;     // offset number of this packet
  char data[FBP_PACKET_DATASIZE]; // the actual data
} __attribute__((__packed__));

#endif // FBP_GLOBAL_H
