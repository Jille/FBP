#include "fbpclient.h"
#include "../common/fbp.h"

FbpClient::FbpClient(quint16 port, QObject *parent)
: QUdpSocket(parent)
, file_( 0 )
, knownFileClearTimer_( new QTimer() )
{
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
    knownFiles_.append( k );
    index      = knownFiles_.size()-1;

    qDebug() << "New file added with ID" << (int)k.id << "filename" << k.fileName;
    emit fileAdded( k.id );
  }

  // Got an announcement for an existing file
  knownFiles_[index].lastAnnouncement = time(NULL);
  //qDebug() << "Got announcement for file" << (int)knownFiles_[index].id;
}

void FbpClient::setPort( quint16 port )
{
  setLocalPort( port );
  bind( port, QUdpSocket::ShareAddress );
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

    // if it's an announcement package, read it as such
    if( data[0] == '\0' )
    {
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
      qWarning() << "Unknown package type";
    }

    delete [] data;
  }
}
