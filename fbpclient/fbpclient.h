#ifndef FBPCLIENT_H
#define FBPCLIENT_H

#include <QUdpSocket>
#include <QFile>

class FbpClient : public QUdpSocket
{
Q_OBJECT
public:
    explicit FbpClient(quint16 port, QObject *parent = 0);
    quint16  port() const;
    void     setPort( quint16 port );

signals:

public slots:

private slots:
   void      slotReadyRead();

private:
   void      readAnnouncement( struct Announcement *a, int size );
   QFile    *file_;
};

#endif // FBPCLIENT_H
