#ifndef CONTROLPROTOCOL_H
#define CONTROLPROTOCOL_H

#include <QUdpSocket>

#define DEFAULT_FBP_CONTROL_PORT 1025

class ControlProtocol : public QUdpSocket
{
Q_OBJECT
public:
  explicit ControlProtocol(int port = DEFAULT_FBP_CONTROL_PORT, QObject *parent = 0);
  int      port() const;
  void     setPort( int port );

signals:

public slots:
  void announceTransfer( const QString &filename, int size );
  void open();

private:
  int port_;
};

#endif // CONTROLPROTOCOL_H
