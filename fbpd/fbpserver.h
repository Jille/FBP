#ifndef FBPSERVER_H
#define FBPSERVER_H

#include <QUdpSocket>
#include <QFile>
#include <QTimer>

#include "../common/fbp_global.h"

class FbpServer : private QUdpSocket
{
Q_OBJECT
public:
    explicit FbpServer( quint16 port = FBPD_DEFAULT_PORT, QObject *parent = 0);
    quint16  port() const;
    void     setPort( quint16 port );
    void     transferFile( QFile *file );

signals:

public slots:

private slots:
   void      slotReadyRead();
   void      announceFile();

private:
   QFile    *file_;
   QTimer   *announceTimer_;

};

#endif // FBPSERVER_H
