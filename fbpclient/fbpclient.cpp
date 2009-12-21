#include "fbpclient.h"
#include "../common/fbp.h"

FbpClient::FbpClient(quint16 port, QObject *parent)
: QUdpSocket(parent)
, file_( 0 )
{
  connect( this,           SIGNAL(     readyRead() ),
           this,           SLOT(   slotReadyRead() ) );

  setPort( port );
}

quint16 FbpClient::port() const
{
  return localPort();
}

void FbpClient::readAnnouncement( struct Announcement *a, int size )
{
  qDebug() << "Read announcement with size " << size;

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
      if( data[1] != '\1' )
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
