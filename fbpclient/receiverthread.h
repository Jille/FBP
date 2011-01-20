#ifndef RECEIVERTHREAD_H
#define RECEIVERTHREAD_H

#include <QObject>
#include <QThread>
#include <QUdpSocket>
#include "fbpclient.h"

class ReceiverThread : public QThread
{
Q_OBJECT
public:
    explicit ReceiverThread(quint64 port, FbpClient *parent = 0);
            ~ReceiverThread();
    void     run();

signals:
    /**
     * A data packet was received. You must delete[] the DataPacket yourself.
     */
    void gotDataPacket(struct DataPacket*);
    /**
     * An announcement packet was received. You must delete[] the Announcement yourself.
     */
    void gotAnnouncement(struct Announcement*, QString, quint16);

private slots:
    void onReadyRead();
    void sendDatagram( const char*, qint64, QString, quint16 );

private:
    class BoundSocket : public QUdpSocket {
    public:
      BoundSocket() : QUdpSocket() {}
      void setLocalPort(quint16 p) { QUdpSocket::setLocalPort(p); }
      void setPeerPort(quint16 p)  { QUdpSocket::setPeerPort(p); }
    };

    void      readAnnouncement( struct Announcement *a );
    void      readDataPacket( struct DataPacket *d, quint32 size );

    // Used for checking whether the fbpclient is downloading something.
    // If you use this, ***MAKE SURE THE METHOD YOU USE IS THREAD SAFE!***
    FbpClient *parent_;

    quint64 port_;
    BoundSocket *sock_;
};

#endif // RECEIVERTHREAD_H
