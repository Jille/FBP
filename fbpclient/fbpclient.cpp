#include "fbpclient.h"
#include "../common/fbp.h"
#include <QDateTime>
#include "receiverthread.h"

FbpClient::FbpClient(quint16 port, QObject *parent)
: QObject(parent)
, thread_( new ReceiverThread(port, this) )
, knownFileClearTimer_( new QTimer() )
, updateInterfaceTimer_( new QTimer() )
{
  // If this is a big-endian system, crash
  Q_ASSERT((1 >> 1) == 0);

  connect( thread_, SIGNAL(gotAnnouncement(Announcement*, QString, quint16)),
           this,    SLOT(announcementReceived(Announcement*, QString, quint16)));
  connect( thread_, SIGNAL(gotDataPacket(DataPacket*)),
           this,    SLOT(readDataPacket(DataPacket*)));
  connect( this,    SIGNAL(sendDatagram(const char*,qint64,QString,quint16)),
            thread_,SLOT(sendDatagram(const char*,qint64,QString,quint16)));

  connect( knownFileClearTimer_, SIGNAL(         timeout() ),
           this,                 SLOT(   clearKnownFiles() ) );
  connect( updateInterfaceTimer_, SIGNAL(        timeout() ),
           this,                  SLOT(  updateInterface() ) );

  knownFileClearTimer_->setSingleShot( false );
  knownFileClearTimer_->setInterval( 5000 );
  knownFileClearTimer_->start();

  updateInterfaceTimer_->setSingleShot( false );
  updateInterfaceTimer_->setInterval( 200 );
  updateInterfaceTimer_->start();
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
  downloadingFilesMutex_.lock();
  if( !downloadingFiles_.contains( id ) )
  {
    downloadingFilesMutex_.unlock();
    return;
  }
  downloadingFilesMutex_.unlock();

  qDebug() << "finishDownload for" << id;
  emit fileProgressChanged( id, 100 );

  int index = -1;
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i]->id == id ) index = i;
  if( index == -1 ) return;

  // Don't download this file anymore, it's finished
  downloadingFilesMutex_.lock();
  QFile *newFile = downloadingFiles_[id].first;
  QFile *bitmask = downloadingFiles_[id].second;
  downloadingFiles_.remove( id );
  downloadingFilesMutex_.unlock();

  bitmask->remove();
  newFile->flush();
  if( QFile::exists( knownFiles_[index]->fileName ) )
  {
    QString fileName = knownFiles_[index]->fileName + ".data";
    qDebug() << "Would overwrite file " << knownFiles_[index]->fileName << "saving as" << fileName;
    emit fileOverwriteWarning( id, fileName );
    knownFiles_[index]->fileName = fileName;
  }
  else
  {
    newFile->rename( knownFiles_[index]->fileName );
  }
  newFile->close();
  delete newFile; delete bitmask;

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
  downloadingFilesMutex_.lock();
  QFile *bitmaskFile = downloadingFiles_[id].second;
  downloadingFilesMutex_.unlock();
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

/**
 * This method returns whether the FbpClient wants to download the file with
 * given ID.
 * This method is THREAD SAFE. It can be called from other threads safely.
 */
bool FbpClient::isDownloadingFile( int id )
{
  downloadingFilesMutex_.lock();
  bool res = downloadingFiles_.contains( id );
  downloadingFilesMutex_.unlock();
  return res;
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

/**
 * We got a signal from the ReceiverThread that we should request new data
 * packets. We should check what offsets we don't have, neither on disk nor
 * in the handling queue, and request all remaining packets.
 */
void FbpClient::announcementReceived( struct Announcement *a, QString sender, quint16 port )
{
  char id = a->fileid;

  // Force correct ending of the file name. If it's shorter than 256
  // bytes, this won't mess with that; if it's longer, this will make
  // sure the name can be read and prevent crashes and buffer overflows
  // etc...
  a->filename[255] = '\0';

  int index = -1;
  for( int i = 0; i < knownFiles_.size(); ++i )
      if( knownFiles_[i]->id == id ) index = i;

  if( index == -1 )
  {
    // New file! Add it to knownFiles
    struct KnownFile *k = new struct KnownFile;
    k->fileName   = QString( a->filename );
    k->id         = id;
    k->numPackets = a->numPackets;
    k->server     = sender;
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
    goto endparse;
  }
  if( knownFiles_[index]->server != sender
   || knownFiles_[index]->serverPort != port )
  {
    qWarning() << "Warning: Invalid announcement: Different server hosting "
                  "overlapping ID or other port used.";
    knownFiles_[index]->lastAnnouncement = 0;
    clearKnownFiles();
    goto endparse;
  }

  // If we're currently downloading this file and server status is WAITING,
  // we can request a new range of packets :)
  if( isDownloadingFile( id ) && a->status == FBP_STATUS_WAITING )
  {
    sendRequest( id );
  }

endparse:
  delete [] a;
}

void FbpClient::readDataPacket( struct DataPacket *d )
{
  pkt_count offset = d->offset;
  pkt_count numPackets = 0;

  Q_ASSERT( isDownloadingFile( d->fileid ) );

  // See if the bitmap for this file wants this packet
  int id = d->fileid;
  int index = -1;
  for( int i = 0; i < knownFiles_.size(); ++i )
    if( knownFiles_[i]->id == id ) index = i;
  if( index == -1 )
    goto endparse;

  numPackets = knownFiles_[index]->numPackets;

  if( offset >= numPackets )
  {
    qWarning() << "Wait, what? Received a data packet with offset larger or "
                  "equal to packet count. Dropping.";
    goto endparse;
  }

  if( !BM_ISSET( knownFiles_[index]->bitmask, offset ) )
  {
    // We don't have it!

    // Append zeroes to the file if it's not large enough
    downloadingFilesMutex_.lock();
    QFile *dataFile = downloadingFiles_[id].first;
    downloadingFilesMutex_.unlock();
    int length      = dataFile->size();
    int data_offset = offset * FBP_PACKET_DATASIZE;
    int zeroes      = data_offset - length;

    if( zeroes > 0 )
    {
      if( !dataFile->seek( length ) )
      {
        qWarning() << "Failed to seek() to end of file: "
                   << dataFile->errorString();
        goto endparse;
      }
      char *toWrite = new char[zeroes];
      for( int i = 0; i < zeroes; ++i )
        toWrite[i] = '\0';
      if( dataFile->write( toWrite, zeroes ) != zeroes )
      {
        qWarning() << "Failed to write zeroes to data file: "
                   << dataFile->errorString();
        goto endparse;
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
      goto endparse;
    }

    // Write the data in this packet. For all packets but the last one, this
    // will be FBP_PACKET_DATASIZE bytes, for the last one, it will be less
    // than that.
    Q_ASSERT( d->size <= FBP_PACKET_DATASIZE );
    if( !dataFile->write( d->data, d->size ) || !dataFile->flush() )
    {
      qWarning() << "Couldn't write data: " << dataFile->errorString();
      goto endparse;
    }

    // Got it! Set it in the bitmask, so we don't download it twice
    BM_SET( knownFiles_[index]->bitmask, offset );

    Q_ASSERT( BM_ISSET( knownFiles_[index]->bitmask, offset ) );

    // Flush bitmask file to disk
    flushBitmask( id );

    goto endparse;
  }

endparse:
  delete [] d;
}

void FbpClient::sendRequest( int id )
{
  if( !isDownloadingFile( id ) )
    return;

  qDebug() << "Send request.";

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
        qDebug() << "Requesting" << numPackets << "packets starting with" << offset;
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
  qDebug() << "RequestNum=" << requestNum;
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

  // ReceiverThread will delete rp
  emit sendDatagram( (const char*)rp, sizeof(struct RequestPacket),
                     knownFiles_[index]->server, knownFiles_[index]->serverPort );
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
  downloadingFilesMutex_.lock();
  downloadingFiles_.insert( id, qMakePair( dataFile, bitmaskFile ) );
  downloadingFilesMutex_.unlock();
  flushBitmask( id );
}

void FbpClient::startListening()
{
  thread_->start(QThread::HighestPriority);
}

void FbpClient::updateInterface()
{
  downloadingFilesMutex_.lock();
  QList<int> ids = downloadingFiles_.keys();
  downloadingFilesMutex_.unlock();

  foreach(int id, ids)
  {
    int index = -1;
    for( int i = 0; i < knownFiles_.size(); ++i )
      if( knownFiles_[i]->id == id ) index = i;
    if(index < 0)
      continue;

    // Update the GUI
    emit fileProgressChanged( id, progressFromBitmask( knownFiles_[index] ) );
  }
}
