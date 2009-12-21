#ifndef FBP_GLOBAL_H
#define FBP_GLOBAL_H

#include <QtGlobal>

#define FBPD_DEFAULT_PORT 1026
#define FBPD_PACKET_DATASIZE 4096

struct Announcement
{
  char zero;          // ALWAYS 0, means this is an announcement packet
  char announceVer;   // ALWAYS 1, means this is version 1 of the announcement
  char status;        // 0=not sending, 1=sending
  char unused2;       // unused
  quint32 numPackets; // 4 bytes: number of packets
  char filename[32];  // 32 bytes of filename
} __attribute__((__packed__));

#endif // FBP_GLOBAL_H
