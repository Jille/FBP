#include "controlprotocol.h"

ControlProtocol::ControlProtocol( int port, QObject *parent) :
    QUdpSocket(parent)
{
  setPort( port );
}

void ControlProtocol::announceTransfer( const QString &fn, int size )
{
  if( !isOpen() )
    open();

  writeDatagram( )
}

int ControlProtocol::port() const
{
  return localPort();
}

void ControlProtocol::setPort( int port )
{
  setLocalPort( port );
  bind( port, QUdpSocket::ShareAddress );
}
