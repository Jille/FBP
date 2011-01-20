#ifndef FBPCLIENT_H
#define FBPCLIENT_H

#include <QDir>
#include <QMap>
#include <QMutex>
#include <QPair>
#include <QTimer>

#include "../common/fbp.h"
#include "../common/bitmask.h"

class QHostAddress;
class ReceiverThread;

class FbpClient : public QObject
{
Q_OBJECT
  friend class ReceiverThread;

public:
    explicit FbpClient(quint16 port = FBP_DEFAULT_PORT, QObject *parent = 0);
    virtual ~FbpClient();
    void     startListening();
    bool     isDownloadingFile( int id );
    const QString &fileNameForFile( int id ) const;

signals:
  void       fileAdded( int id, const QString &fn, int startProgress );
  void       fileRemoved( int id );
  void       downloadStarted( int id );
  void       downloadFinished( int id, const QString &fn );
  void       fileProgressChanged( int id, int progress );
  void       fileOverwriteWarning( int id, const QString &fn );

  // For internal use only
  void sendDatagram( const char*, qint64, QString, quint16 );

public slots:
  void       startDownload( int id, const QDir &directory );

private slots:
   void      clearKnownFiles();
   void      sendRequest( int id );
   void      flushBitmask( int id );
   void      finishDownload( int id );
   void      announcementReceived( struct Announcement *a, QString sender, quint16 port );
   void      readDataPacket( struct DataPacket *d );

private:
   struct KnownFile {
     char    id;
     QString fileName;
     pkt_count numPackets;
     time_t  lastAnnouncement;
     QString server;
     quint16 serverPort;
     BM_DEFINE(bitmask);
   };

   int       progressFromBitmask( const struct KnownFile *f ) const;
   QMap<int,QPair<QFile*,QFile*> > downloadingFiles_;
   QMutex downloadingFilesMutex_;
   ReceiverThread *thread_;
   QList<KnownFile*> knownFiles_;
   QTimer   *knownFileClearTimer_;
};

#endif // FBPCLIENT_H
