#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>
#include <QtCore/QString>
#include <QtCore/QFile>
#include "fbpserver.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    QStringList args = a.arguments();
    if( args.size() < 2 || args.size() > 3 )
    {
      QString app;
      if( args.size() == 0 )
           app = "fbpd";
      else app = args[0];

      qWarning() << "Usage: " << app << " <fname> [port]";
      qWarning() << "To distribute the file <fname> to FBP clients over port "
                    "[port] (default is " << FBPD_DEFAULT_PORT << ")";
      return 1;
    }
    QString fileName = args[1];
    int port = FBPD_DEFAULT_PORT;
    if( args.size() == 3 )
      port = args[2].toInt();

    QFile f( fileName );
    if( !f.open( QIODevice::ReadOnly ) )
    {
      qWarning() << "Error: couldn't read " << fileName << ": "
                 << f.errorString();
      return 2;
    }

    qDebug() << "Going to distribute file " << fileName
             << "to FBDP clients over port " << port;

    // Set up an FbpServer
    FbpServer fb( port );
    fb.transferFile( &f );

    return a.exec();
}
