#include "receiverthread.h"
#include <QUdpSocket>
#include "../common/fbp.h"

ReceiverThread::ReceiverThread(quint64 port, FbpClient *parent)
: QThread()
, parent_(parent)
, port_(port)
, sock_(0)
{
  // TODO this is very hacky. we should make ReceiverThread a simple QObject
  // and run it from elsewhere, so it has thread() != mainThread.
  // For now, hack hack hack:
  moveToThread(this);
}

ReceiverThread::~ReceiverThread()
{
  delete sock_;
}

void ReceiverThread::run()
{
  if(sock_ == 0)
    sock_ = new ReceiverThread::BoundSocket();
  sock_->setLocalPort( port_ );
  sock_->setPeerPort( port_ );
  sock_->bind( port_, QUdpSocket::ShareAddress );

  connect( sock_, SIGNAL(readyRead()),
           this,  SLOT(onReadyRead()));

  qDebug() << "ReceiverThread running!";
  exec();
  qDebug() << "ReceiverThread shutting down!";
}

void ReceiverThread::sendDatagram( const char *pkt, qint64 size,
                                   QString host, quint16 port )
{
  sock_->writeDatagram(pkt, size, QHostAddress(host), port);
}


/**
 * A datagram can be read. If it's a data packet we are interested in, put it
 * into the inter-thread queue and register the data packet in the queue
 * content bitmap. If it's an announcement packet with the idle
 * bit set and the inter-thread queue does not contain data packets for that
 * file ID, fire an inter-thread signal for the FbpClient to make a request.
 */
void ReceiverThread::onReadyRead()
{
  while( sock_->hasPendingDatagrams() )
  {
    QHostAddress sender;
    quint16 port;

    qint64 pendingSize = sock_->pendingDatagramSize();
    // will be freed in the FbpClient
    char *data = new char[pendingSize];

    qint64 readSize = sock_->readDatagram( data, pendingSize, &sender, &port );
    Q_ASSERT( readSize == pendingSize );
    Q_ASSERT( pendingSize >= 2 );

    // read first byte: file ID
    unsigned char fileId = data[0];

    if( fileId )
    {
      // is it a file we're interested in?
      if( !parent_->isDownloadingFile( fileId ) )
      {
        delete [] data;
        continue;
      }

      struct DataPacket *dp = (struct DataPacket*) data;

      if( 0 // readSize > sizeof(struct DataPacket*)
       || dp->size > FBP_PACKET_DATASIZE
       || dp->size == 0 )
      {
        qWarning() << "Data packet size for" << dp->offset
                   << "is incorrect (" << dp->size << "/" << readSize
                   << "), dropping packet";
        delete [] data;
        return;
      }

      emit gotDataPacket( dp );
    }
    else
    {
      // it's an announcement package, read it as such
      if( FBP_ANNOUNCE_VERSION == data[1] )
        // data will be automatically free'd by FbpClient
        emit gotAnnouncement((struct Announcement*) data, sender.toString(), port);
      else
      {
        fprintf(stderr,"Announcement has version %d, cannot read.\n",data[1]);
        delete [] data;
      }
    }
  }
}
