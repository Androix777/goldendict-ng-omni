#pragma once
#ifdef WITH_WEB_VIEWER
  #include "article_lookup.hh"
  #include <QHttpServerResponder>
  #include <QJsonDocument>
  #include <QJsonObject>
  #include <QPointer>
  #include <QTcpSocket>
  #include <QTimer>
  #if QT_VERSION >= QT_VERSION_CHECK( 6, 8, 0 )
    #include <QHttpHeaders>
  #endif

using Status = QHttpServerResponder::StatusCode;

// Keep the response, dictionary and request alive together until delivery.
// Qt 6.4 exposes the socket directly. Newer responders own their stream.
class BrowserReply: public QObject
{
public:
  BrowserReply( QHttpServerResponder && r, QObject * owner, bool apiRequest ):
    QObject( owner ),
    responder( std::move( r ) ),
    apiRequest( apiRequest )
  {
  #if QT_VERSION < QT_VERSION_CHECK( 6, 8, 0 )
    socket = responder.socket();
    connect( socket, &QTcpSocket::disconnected, this, &QObject::deleteLater );
  #endif
    timeout.setSingleShot( true );
    connect( &timeout, &QTimer::timeout, this, [ this ] {
      error( Status::GatewayTimeout, "Lookup timed out" );
    } );
    timeout.start( 30000 );
  }

  ~BrowserReply() override
  {
    if ( resource && !resource->isFinished() ) {
      resource->cancel();
    }
  }

  void send( Status status,
             const QByteArray & bytes = {},
             const QByteArray & type  = "text/plain; charset=utf-8",
             QByteArray policy        = "default-src 'none'; frame-ancestors 'none'; sandbox" )
  {
    if ( sent ) {
      return;
    }
    sent = true;
    deleteLater();
  #if QT_VERSION < QT_VERSION_CHECK( 6, 8, 0 )
    if ( !socket || socket->state() != QAbstractSocket::ConnectedState ) {
      return;
    }
    responder.write( bytes,
                     { { "Content-Type", type },
                       { "Content-Security-Policy", policy },
                       { "X-Content-Type-Options", "nosniff" },
                       { "Cross-Origin-Resource-Policy", "same-origin" },
                       { "Cache-Control", "no-store" },
                       { "Referrer-Policy", "no-referrer" } },
                     status );
  #else
    QHttpHeaders headers;
    headers.append( "Content-Type", type );
    headers.append( "Content-Security-Policy", policy );
    headers.append( "X-Content-Type-Options", "nosniff" );
    headers.append( "Cross-Origin-Resource-Policy", "same-origin" );
    headers.append( "Cache-Control", "no-store" );
    headers.append( "Referrer-Policy", "no-referrer" );
    responder.write( bytes, headers, status );
  #endif
  }

  void json( Status status, const QJsonObject & object )
  {
    send( status, QJsonDocument( object ).toJson( QJsonDocument::Compact ), "application/json; charset=utf-8" );
  }

  void error( Status status, const QString & message )
  {
    if ( apiRequest ) {
      json( status, { { "error", message } } );
    }
    else {
      send( status );
    }
  }

  sptr< Dictionary::Class > dictionary;
  sptr< Dictionary::DataRequest > resource;
  std::unique_ptr< ArticleLookup > lookup;
  QString page;
  QTimer timeout;

private:
  QHttpServerResponder responder;
  const bool apiRequest;
  bool sent = false;
  #if QT_VERSION < QT_VERSION_CHECK( 6, 8, 0 )
  QPointer< QTcpSocket > socket;
  #endif
};
#endif
