#include "fbpclient.h"
#include "../common/fbp.h"

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

void FbpClient::clearKnownFiles()
{
  time_t t = time(NULL);

  for( int i = 0; i < knownFiles_.size(); ++i )
  {
    // if the last announcement was more than 5 seconds ago...
    if( knownFiles_[i].lastAnnouncement < t - 5)
    {
      qDebug() << "File with ID" << (int)knownFiles_[i].id << "is expired, "
                  "removing it from the list.";

      // TODO: what if this file is being downloaded right now?

      emit fileRemoved( knownFiles_[i].id );

      knownFiles_.removeAt( i );
    }
  }
}

quint16 FbpClient::port() const
{
  return localPort();
}

void FbpClient::readAnnouncement( struct Announcement *a, int size )
{
  Q_ASSERT( size == sizeof( struct Announcement ) );

  char id = a->fileid;
  int index = -1;
  // Find this fileid in knownFiles_
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i].id == id ) index = i;

  // Force correct ending of the file name. If it's shorter than 256
  // bytes, this won't mess with that; if it's longer, this will make
  // sure the name can be read and prevent crashes and buffer overflows
  // etc...
  a->filename[255] = '\0';

  if( index == -1 )
  {
    // New file! Add it to knownFiles
    struct KnownFile k;
    k.fileName = QString( a->filename );
    k.id       = a->fileid;
    k.numPackets = a->numPackets;
    knownFiles_.append( k );
    index      = knownFiles_.size()-1;

    qDebug() << "New file added with ID" << (int)k.id << "filename" << k.fileName;
    emit fileAdded( k.id );
  }

  // Got an announcement for an existing file
  knownFiles_[index].lastAnnouncement = time(NULL);
  //qDebug() << "Got announcement for file" << (int)knownFiles_[index].id;

  if( knownFiles_[index].numPackets != a->numPackets )
  {
    qWarning() << "Warning: Invalid announcement: Number of packets for this "
                  "id is different from the number of packets in this "
                  "announcement.";
    knownFiles_[index].lastAnnouncement = 0;
    clearKnownFiles();
  }
}

void FbpClient::readDataPacket( struct DataPacket *d, int size )
{
  Q_ASSERT( size == sizeof( struct DataPacket ) );
  Q_ASSERT( downloadingFiles_.contains( d->fileid ) );

  // See if the bitmap for this file wants this packet
  int id = d->fileid;
  pkt_count offset = d->offset;
  pkt_count numPackets = knownFiles_[id].numPackets;

  if( offset >= numPackets )
  {
    qWarning() << "Wait, what? Received a data packet with offset larger or "
                  "equal to packet count. Ignoring.";
    return;
  }

  if( !BM_ISSET( knownFiles_[id].bitmask, offset ) )
  {
    // We don't have it!
    // Check the checksum
    char *checksum = d->checksum;
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
    dataFile->write( d->data, FBP_PACKET_DATASIZE );

    // Got it! Set it in the bitmask, so we don't download it twice
    BM_SET( knownFiles_[id].bitmask, offset );

    // Flush bitmask file to disk
    QFile *bitmaskFile = downloadingFiles_[id].second;
    bitmaskFile->seek( 0 );
    if( bitmaskFile->write( (const char*)knownFiles_[id].bitmask,
                            BM_SIZE( numPackets ) )
      != BM_SIZE( numPackets ) )
    {
      qWarning() << "Couldn't write complete bitmap to disk?"
                 << bitmaskFile->errorString();
      return;
    }

    qDebug() << "Succesfully downloaded packet " << offset;
    return;
  }

  qDebug() << "Already have packet " << offset;
}

void FbpClient::setPort( quint16 port )
{
  setLocalPort( port );
  bind( port, QUdpSocket::ShareAddress );
}

void FbpClient::startDownload( int id, const QDir &downloadDir )
{
  int index = -1;
  // Find this fileid in knownFiles_
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i].id == id ) index = i;

  if( index == -1 )
  {
    qWarning() << "Got startDownload() for an ID I don't know";
    return;
  }

  // We will start downloading to downloadDir
  QFile *dataFile    = new QFile( downloadDir.filePath( knownFiles_[index].fileName + ".data"  ) );
  QFile *bitmaskFile = new QFile( downloadDir.filePath( knownFiles_[index].fileName + ".bmask" ) );

  dataFile->open( QIODevice::WriteOnly );
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
  pkt_count numPackets = knownFiles_[id].numPackets;
  int bitmapSize = BM_SIZE(numPackets);
  if( bitmaskFile->size() != bitmapSize )
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
  BM_INIT( knownFiles_[id].bitmask, numPackets );

  // Read the bitmask from the file if it's not empty (size should be correct)
  Q_ASSERT( bitmaskFile->size() == 0 || bitmaskFile->size() == bitmapSize );
  if( bitmaskFile->size() != 0 )
  {
    if( bitmaskFile->read( (char*)knownFiles_[id].bitmask, bitmapSize ) != bitmapSize )
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
  downloadingFiles_.insert( id, qMakePair( dataFile, bitmaskFile ) );
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
        readAnnouncement( a, readSize );
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
      if( !downloadingFiles_.contains( fileId ) )
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
