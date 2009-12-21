#include "fbp_global.h"
#include "fbpserver.h"

FbpServer::FbpServer(quint16 port, QObject *parent)
: QUdpSocket(parent)
, file_( 0 )
, announceTimer_( new QTimer(this) )
{
  connect( this,           SIGNAL(     readyRead() ),
           this,           SLOT(   slotReadyRead() ) );
  connect( announceTimer_, SIGNAL(       timeout() ),
           this,           SLOT(    announceFile() ) );

  setPort( port );
  announceTimer_->setSingleShot( false );
  announceTimer_->setInterval( 1000 );
  announceTimer_->start();
}

void FbpServer::announceFile()
{
  // Ignore if there's no file to announce
  if( file_ == 0 )
    return;

  struct Announcement *a = new struct Announcement;
  // announcement packet
  a->zero = 0;
  // announce version = 1
  a->announceVer = 1;
  // not sending anything atm
  a->status = 0;

  qint64 fileSize = file_->size();
  a->numPackets = fileSize / FBPD_PACKET_DATASIZE;

  QByteArray ba;
  ba.append( (char*)a, sizeof( Announcement ) );
}

quint16 FbpServer::port() const
{
  return localPort();
}

void FbpServer::setPort( quint16 port )
{
  setLocalPort( port );
  bind( port, QUdpSocket::ShareAddress );
}

void FbpServer::slotReadyRead()
{
  while( hasPendingDatagrams() )
  {
    QHostAddress sender; quint16 senderPort;

    qint64 pendingSize = pendingDatagramSize();
    char *data = new char[pendingSize];

    qint64 readSize = readDatagram( data, pendingSize, &sender, &senderPort );
    Q_ASSERT( readSize == pendingSize );

    // no idea what to do with this yet.

    delete [] data;
  }
}

void FbpServer::transferFile( QFile *file )
{
  file_ = file;

}
