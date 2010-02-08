#ifndef FBPCLIENT_H
#define FBPCLIENT_H

#include <QUdpSocket>
#include <QFile>
#include <QTimer>
#include <QDir>
#include <QPair>
#include <QMap>

#include "../common/fbp.h"
#include "../common/bitmask.h"

class QHostAddress;

class FbpClient : public QUdpSocket
{
Q_OBJECT
public:
    explicit FbpClient(quint16 port = FBP_DEFAULT_PORT, QObject *parent = 0);
    virtual ~FbpClient();
    quint16  port() const;
    void     setPort( quint16 port );
    bool     isDownloadingFile( int id ) const;

signals:
  void       fileAdded( int id, const QString &fn, int startProgress );
  void       fileRemoved( int id );
  void       downloadStarted( int id );
  void       downloadFinished( int id );
  void       fileProgressChanged( int id, int progress );

public slots:
  void       startDownload( int id, const QDir &directory );

private slots:
   void      slotReadyRead();
   void      clearKnownFiles();
   void      sendRequest( int id );
   void      flushBitmask( int id );

private:

   struct KnownFile {
     char    id;
     QString fileName;
     pkt_count numPackets;
     time_t  lastAnnouncement;
     QString downloadFileName;
     QHostAddress server;
     quint16      serverPort;
     BM_DEFINE(bitmask);
   };

   void      readAnnouncement( struct Announcement *a, int size,
                               QHostAddress *sender, quint16 port );
   void      readDataPacket( struct DataPacket *d, quint32 size );
   int       progressFromBitmask( const struct KnownFile *f ) const;
   QFile    *file_;
   QList<KnownFile*> knownFiles_;
   QTimer   *knownFileClearTimer_;
   QMap<int,QPair<QFile*,QFile*> > downloadingFiles_;
};

#endif // FBPCLIENT_H
