#include "fbpclient.h"
#include "../common/fbp.h"
#include <QDateTime>

FbpClient::FbpClient(quint16 port, QObject *parent)
: QUdpSocket(parent)
, file_( 0 )
, knownFileClearTimer_( new QTimer() )
{
  // If this is a big-endian system, crash
  Q_ASSERT((1 >> 1) == 0);

  connect( this,                 SIGNAL(       readyRead() ),
           this,                 SLOT(     slotReadyRead() ) );
  connect( knownFileClearTimer_, SIGNAL(         timeout() ),
           this,                 SLOT(   clearKnownFiles() ) );

  setPort( port );
  knownFileClearTimer_->setSingleShot( false );
  knownFileClearTimer_->setInterval( 5000 );
  knownFileClearTimer_->start();
}

FbpClient::~FbpClient()
{
  qDeleteAll( knownFiles_ );
  knownFiles_.clear();
}

void FbpClient::clearKnownFiles()
{
  for( int i = 0; i < knownFiles_.size(); ++i )
  {
    // if the last announcement was more than 5 seconds ago...
    if( QDateTime::fromTime_t( knownFiles_[i]->lastAnnouncement )
        < QDateTime::currentDateTime().addSecs(- 5) )
    {
      qDebug() << "File with ID" << (int)knownFiles_[i]->id << "is expired, "
                  "removing it from the list.";

      // TODO: what if this file is being downloaded right now?

      emit fileRemoved( knownFiles_[i]->id );

      delete knownFiles_.takeAt( i );
    }
  }
}

void FbpClient::finishDownload( int id )
{
  if( !downloadingFiles_.contains( id ) )
    return;

  int index = -1;
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i]->id == id ) index = i;
  if( index == -1 ) return;

  // Don't download this file anymore, it's finished
  QFile *newFile = downloadingFiles_[id].first;
  QFile *bitmask = downloadingFiles_[id].second;
  downloadingFiles_.remove( id );

  bitmask->remove();
  newFile->flush();
  if( QFile::exists( knownFiles_[index]->fileName ) )
  {
    QString fileName = knownFiles_[index]->fileName + ".data";
    emit fileOverwriteWarning( id, fileName );
    knownFiles_[index]->fileName = fileName;
  }
  else
  {
    newFile->rename( knownFiles_[index]->fileName );
  }
  newFile->close();
  delete newFile; delete bitmask;

  emit fileProgressChanged( id, 100 );
  emit downloadFinished( id, knownFiles_[index]->fileName );
}

const QString &FbpClient::fileNameForFile( int id ) const
{
  int index = -1;
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i]->id == id ) index = i;
  Q_ASSERT( index != -1 );

  return knownFiles_[index]->fileName;
}

void FbpClient::flushBitmask( int id )
{
  int index = -1;
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i]->id == id ) index = i;
  if( index == -1 ) return;

  pkt_count numPackets = knownFiles_[index]->numPackets;
  QFile *bitmaskFile = downloadingFiles_[id].second;
  bitmaskFile->seek( 0 );
  if( bitmaskFile->write( (const char*)knownFiles_[index]->bitmask,
                          BM_SIZE( numPackets ) )
    != BM_SIZE( numPackets ) )
  {
    qWarning() << "Couldn't write complete bitmap to disk?"
               << bitmaskFile->errorString();
    return;
  }

  if( !bitmaskFile->flush() )
  {
    qWarning() << "Couldn't flush! " << bitmaskFile->errorString();
    return;
  }
}

bool FbpClient::isDownloadingFile( int id ) const
{
  return downloadingFiles_.contains( id );
}

quint16 FbpClient::port() const
{
  return localPort();
}

int FbpClient::progressFromBitmask( const struct KnownFile *k ) const
{
  double numPackets = (double)k->numPackets;
  int packetsDone = 0;
  for( int i = 0; i < numPackets; ++i )
  {
    if( BM_ISSET( k->bitmask, i ) )
      packetsDone++;
  }

  int percentage = ( packetsDone / numPackets ) * 100;

  return percentage;
}

void FbpClient::readAnnouncement( struct Announcement *a, int size,
                                  QHostAddress *sender, quint16 port )
{
  Q_ASSERT( size == sizeof( struct Announcement ) );

  char id = a->fileid;
  int index = -1;
  // Find this fileid in knownFiles_
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i]->id == id ) index = i;

  // Force correct ending of the file name. If it's shorter than 256
  // bytes, this won't mess with that; if it's longer, this will make
  // sure the name can be read and prevent crashes and buffer overflows
  // etc...
  a->filename[255] = '\0';

  if( index == -1 )
  {
    // New file! Add it to knownFiles
    struct KnownFile *k = new struct KnownFile;
    k->fileName   = QString( a->filename );
    k->id         = id;
    k->numPackets = a->numPackets;
    k->server     = *sender;
    k->serverPort = port;
    knownFiles_.append( k );
    index         = knownFiles_.size()-1;

    emit fileAdded( id, k->fileName, 0 );
  }

  // Got an announcement for an existing file
  knownFiles_[index]->lastAnnouncement = QDateTime::currentDateTime().toTime_t();

  if( knownFiles_[index]->numPackets != a->numPackets )
  {
    qWarning() << "Warning: Invalid announcement: Number of packets for this "
                  "id is different from the number of packets in this "
                  "announcement.";
    knownFiles_[index]->lastAnnouncement = 0;
    clearKnownFiles();
    return;
  }
  if( knownFiles_[index]->server != *sender
   || knownFiles_[index]->serverPort != port )
  {
    qWarning() << "Warning: Invalid announcement: Different server hosting "
                  "overlapping ID or other port used.";
    knownFiles_[index]->lastAnnouncement = 0;
    clearKnownFiles();
    return;
  }

  // If we're currently downloading this file and server status is WAITING,
  // we can request a new range of packets :)
  if( isDownloadingFile( id ) && a->status == FBP_STATUS_WAITING )
  {
    sendRequest( id );
  }
}

void FbpClient::readDataPacket( struct DataPacket *d, quint32 size )
{
  Q_ASSERT( isDownloadingFile( d->fileid ) );

  if( size > sizeof(struct DataPacket) )
  {
    // Drop if size isn't correct
    // Note that size can be *smaller* than the size of the DataPacket too,
    // if this is the last packet (this is checked below).
    qWarning() << "Dropped incorrectly sized packet.";
    return;
  }

  // See if the bitmap for this file wants this packet
  int id = d->fileid;
  int index = -1;
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i]->id == id ) index = i;
  if( index == -1 )
    return;

  pkt_count offset = d->offset;
  pkt_count numPackets = knownFiles_[index]->numPackets;

  if( offset >= numPackets )
  {
    qWarning() << "Wait, what? Received a data packet with offset larger or "
                  "equal to packet count. Dropping.";
    return;
  }

  // Check packet size (if it's not a full packet and this is not the last
  // packet either, drop it)
  if( size != sizeof( struct DataPacket )
   && offset != numPackets - 1 )
  {
    qWarning() << "Packet isn't last packet, and is incorrectly sized (" << size
               << "instead of" << sizeof(struct DataPacket) << "), dropping it.";
    return;
  }

  if( !BM_ISSET( knownFiles_[index]->bitmask, offset ) )
  {
    // We don't have it!
    // Check the checksum
    char *checksum = d->checksum;
    Q_UNUSED( checksum );
    // todo

    // Append zeroes to the file if it's not large enough
    QFile *dataFile = downloadingFiles_[id].first;
    int length      = dataFile->size();
    int data_offset = offset * FBP_PACKET_DATASIZE;
    int zeroes      = data_offset - length;

    if( zeroes > 0 )
    {
      if( !dataFile->seek( length ) )
      {
        qWarning() << "Failed to seek() to end of file: "
                   << dataFile->errorString();
        return;
      }
      char *toWrite = new char[zeroes];
      for( int i = 0; i < zeroes; ++i )
        toWrite[i] = '\0';
      if( dataFile->write( toWrite, zeroes ) != zeroes )
      {
        qWarning() << "Failed to write zeroes to data file: "
                   << dataFile->errorString();
        return;
      }
      delete [] toWrite;
    }

    // Write the data itself
    length = dataFile->size();
    Q_ASSERT( length >= data_offset );
    if( !dataFile->seek( data_offset ) )
    {
      qWarning() << "Failed to seek() to data offset: "
                 << dataFile->errorString();
      return;
    }

    // Write the data in this packet. For all packets but the last one, this
    // will be FBP_PACKET_DATASIZE bytes, for the last one, it will be less
    // than that.
    int dataSize = size - ( sizeof(struct DataPacket) - FBP_PACKET_DATASIZE );
    Q_ASSERT( dataSize <= FBP_PACKET_DATASIZE );
    if( !dataFile->write( d->data, dataSize ) || !dataFile->flush() )
    {
      qWarning() << "Couldn't write data: " << dataFile->errorString();
      return;
    }

    // Got it! Set it in the bitmask, so we don't download it twice
    BM_SET( knownFiles_[index]->bitmask, offset );

    Q_ASSERT( BM_ISSET( knownFiles_[index]->bitmask, offset ) );

    // Flush bitmask file to disk
    flushBitmask( id );

    emit fileProgressChanged( id, progressFromBitmask( knownFiles_[index] ) );
    return;
  }
}

void FbpClient::sendRequest( int id )
{
  if( !isDownloadingFile( id ) )
    return;

  int index = -1;
  // Find this fileid in knownFiles_
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i]->id == id ) index = i;

  if( index == -1 )
  {
    qWarning() << "Got sendRequest for an ID I don't know.";
    return;
  }

  // First, we should determine what parts of the file we miss
  long offset  = -1;
  int  numPackets = 0;

  // This is a very inefficient method, we should use bitwise comparison
  // of a full byte at once until an unset bit is found. Stuff for later.
  pkt_count totalNum = knownFiles_[index]->numPackets;

  struct RequestPacket *rp = new struct RequestPacket;
  rp->fileid = id;
  int requestNum = 0;

  for( pkt_count i = 0; i <= totalNum; ++i )
  {
    if( i == totalNum || BM_ISSET( knownFiles_[index]->bitmask, i ) )
    {
      // if we already set the first packet, this marks the end of the first
      // range we don't have, so send the request here
      if( offset != -1 )
      {
        rp->requests[requestNum].offset = offset;
        rp->requests[requestNum].num    = numPackets;
        requestNum++;
        offset = -1;
        numPackets = 0;

        // send no more than 30 requests in one packet
        if( requestNum >= FBP_REQUESTS_PER_PACKET )
        {
          break;
        }
      }
      // otherwise, go on searching, we haven't found the first range yet
      continue;
    }

    // If this is the first packet we see which we don't have, save it as such
    if( offset == -1 )
    {
      offset = i;
      numPackets = 1;
    }
    // otherwise, we are counting up
    else
      numPackets++;
  }

  // If we have all packages, no request needs to be sent
  if( requestNum == 0 )
  {
    finishDownload( id );
    delete rp;
    return;
  }

  // zero out the rest of the RequestPacket
  for( int i = requestNum; i < FBP_REQUESTS_PER_PACKET; ++i )
  {
    rp->requests[i].offset = 0;
    rp->requests[i].num    = 0;
  }

  writeDatagram( (const char*)rp, sizeof( struct RequestPacket ),
                 knownFiles_[index]->server, knownFiles_[index]->serverPort );
  delete rp;
}

void FbpClient::setPort( quint16 port )
{
  setLocalPort( port );
  setPeerPort( port );
  bind( port, QUdpSocket::ShareAddress );
}

void FbpClient::startDownload( int id, const QDir &downloadDir )
{
  if( isDownloadingFile( id ) )
    return;

  int index = -1;
  // Find this fileid in knownFiles_
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i]->id == id ) index = i;

  if( index == -1 )
  {
    qWarning() << "Got startDownload() for an ID I don't know";
    return;
  }

  // We will start downloading to downloadDir
  knownFiles_[index]->fileName = downloadDir.filePath( knownFiles_[index]->fileName );
  QFile *dataFile    = new QFile( knownFiles_[index]->fileName + ".data"  );
  QFile *bitmaskFile = new QFile( knownFiles_[index]->fileName + ".bmask" );

  dataFile->open( QIODevice::ReadWrite );
  bitmaskFile->open( QIODevice::ReadWrite );

  if( !dataFile->isOpen() )
  {
    qWarning() << "Couldn't open data file: " << dataFile->errorString();
    delete dataFile; delete bitmaskFile;
    return;
  }
  if( !bitmaskFile->isOpen() )
  {
    qWarning() << "Couldn't open bitmask file: " << bitmaskFile->errorString();
    delete dataFile; delete bitmaskFile;
    return;
  }

  // If the bitmask file is incomplete, we will assume both the data and
  // bitmask files are incorrect and truncate them
  pkt_count numPackets = knownFiles_[index]->numPackets;
  int bitmaskSize = BM_SIZE(numPackets);
  if( bitmaskFile->size() != bitmaskSize
   && bitmaskFile->size() != 0 )
  {
    qWarning() << "Bitmap file size is incorrect, removing and restarting "
                  "download";
    bitmaskFile->remove();
    dataFile->remove();
    delete bitmaskFile;
    delete dataFile;
    startDownload( id, downloadDir );
    return;
  }

  // Allocate bitmask
  BM_INIT( knownFiles_[index]->bitmask, numPackets );

  // Read the bitmask from the file if it's not empty (size should be correct)
  Q_ASSERT( bitmaskFile->size() == 0 || bitmaskFile->size() == bitmaskSize );
  if( bitmaskFile->size() != 0 )
  {
    if( bitmaskFile->read( (char*)knownFiles_[index]->bitmask, bitmaskSize ) != bitmaskSize )
    {
      qWarning() << "Couldn't read bitmap file size, please try removing the "
                    "file to restart the download, or fix permissions.";
      delete bitmaskFile;
      delete dataFile;
      return;
    }
  }

  // Bitmap is now correct, we can start the download
  // (after the next line, any packets not in bitmask that come in with given
  // id will be writen to the data file, their bitmask updated, and written
  // to bitmaskFile.
  emit downloadStarted( id );
  downloadingFiles_.insert( id, qMakePair( dataFile, bitmaskFile ) );
  flushBitmask( id );
}

void FbpClient::slotReadyRead()
{
  while( hasPendingDatagrams() )
  {
    QHostAddress sender; quint16 senderPort;

    qint64 pendingSize = pendingDatagramSize();
    char *data = new char[pendingSize];

    qint64 readSize = readDatagram( data, pendingSize, &sender, &senderPort );
    Q_ASSERT( readSize == pendingSize );
    Q_ASSERT( pendingSize >= 2 );

    // read first byte: file ID
    unsigned char fileId = data[0];

    if( fileId == 0 )
    {
      // it's an announcement package, read it as such
      if( data[1] == FBP_ANNOUNCE_VERSION )
      {
        struct Announcement *a = new struct Announcement();
        memcpy( a, data, pendingSize );
        readAnnouncement( a, readSize, &sender, senderPort );
        delete a;
      }
      else
      {
        qWarning() << "Announcement has version " << (int)(data[1])
                   << ", cannot read, sorry";
      }
    }
    else
    {
      // is it a file we're interested in?
      if( !isDownloadingFile( fileId ) )
      {
        // nope, ignore
        goto endloop;
      }

      struct DataPacket *d = new struct DataPacket;
      memcpy( d, data, pendingSize );
      readDataPacket( d, readSize );
      delete d;
    }

    endloop:
    delete [] data;
  }
}
