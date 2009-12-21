#ifndef FBPCLIENT_H
#define FBPCLIENT_H

#include <QUdpSocket>
#include <QFile>
#include <QTimer>

#include "../common/fbp.h"

class FbpClient : public QUdpSocket
{
Q_OBJECT
public:
    explicit FbpClient(quint16 port = FBP_DEFAULT_PORT, QObject *parent = 0);
    quint16  port() const;
    void     setPort( quint16 port );

signals:
  void       fileAdded( int id );
  void       fileRemoved( int id );

public slots:

private slots:
   void      slotReadyRead();
   void      clearKnownFiles();

private:

   struct KnownFile {
     char    id;
     QString fileName;
     time_t  lastAnnouncement;
   };

   void      readAnnouncement( struct Announcement *a, int size );
   QFile    *file_;
   QList<KnownFile> knownFiles_;
   QTimer   *knownFileClearTimer_;
};

#endif // FBPCLIENT_H
