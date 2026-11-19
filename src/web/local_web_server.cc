#ifdef WITH_WEB_VIEWER
  #include "local_web_server.hh"
  #include "article_lookup.hh"
  #include "article_netmgr.hh"

  #include <QBuffer>
  #include <QFile>
  #include <QPointer>
  #include <QTcpServer>
  #include <QTcpSocket>
  #include <QTimer>
  #include <QUuid>
  #include <QUrlQuery>
  #if QT_VERSION >= QT_VERSION_CHECK( 6, 8, 0 )
    #include <QHttpHeaders>
  #endif

using Status = QHttpServerResponder::StatusCode;

// Keep the response, dictionary and request alive together until delivery.
// Qt 6.4 exposes the socket directly. Newer responders own their stream.
class BrowserReply: public QObject
{
public:
  BrowserReply( QHttpServerResponder && r, QObject * owner ):
    QObject( owner ),
    responder( std::move( r ) )
  {
  #if QT_VERSION < QT_VERSION_CHECK( 6, 8, 0 )
    socket = responder.socket();
    connect( socket, &QTcpSocket::disconnected, this, &QObject::deleteLater );
  #endif
    QTimer::singleShot( 30000, this, [ this ] {
      send( Status::GatewayTimeout );
    } );
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

  sptr< Dictionary::Class > dictionary;
  sptr< Dictionary::DataRequest > resource;
  std::unique_ptr< ArticleLookup > lookup;
  QString page;

private:
  QHttpServerResponder responder;
  bool sent = false;
  #if QT_VERSION < QT_VERSION_CHECK( 6, 8, 0 )
  QPointer< QTcpSocket > socket;
  #endif
};

namespace {
constexpr quint16 viewerPort = 18742;

QString dictionaryIconHtml( Dictionary::Class & dictionary )
{
  QBuffer png;
  if ( !png.open( QIODevice::WriteOnly ) || !dictionary.getIcon().pixmap( 48 ).save( &png, "PNG" ) ) {
    return {};
  }
  return QStringLiteral( "<img class='viewer-dict-icon' alt='' src='data:image/png;base64,%1'>" )
    .arg( QString::fromLatin1( png.data().toBase64() ) );
}

QString pageStart( const QString & word, const Instances::Groups & groups, unsigned group, const QByteArray & nonce )
{
  QString page =
    QStringLiteral(
      "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>%1</title><link rel=stylesheet href='/article.css'><link rel=stylesheet href='/viewer.css'><script nonce='%2' defer blocking=render src='/viewer.js'></script></head><body>"
      "<div id=gd-viewer><nav id=viewer-dictionaries aria-label='Dictionaries' hidden>"
      "<label for=viewer-dictionary>Dictionaries</label><select id=viewer-dictionary></select><ul></ul></nav>"
      "<main><form action='/lookup'><input name=word type=search aria-label='Word' placeholder='Word' value=\"%3\" autofocus>"
      "<select name=group aria-label='Dictionary group'>" )
      .arg( word.isEmpty() ? QStringLiteral( "GoldenDict" ) : word.toHtmlEscaped(),
            QString::fromLatin1( nonce ),
            word.toHtmlEscaped() );
  for ( const auto & entry : groups ) {
    page += QStringLiteral( "<option value='%1'%2>%3</option>" )
              .arg( entry.id )
              .arg( entry.id == group ? QStringLiteral( " selected" ) : QString(), entry.name.toHtmlEscaped() );
  }
  return page + "</select><button>Look up</button></form>"
                "<label class=viewer-inversion><input type=checkbox id=viewer-inversion autocomplete=off> "
                "Invert colors</label><audio id=pronunciation controls hidden></audio>";
}

void serveFile( BrowserReply & reply, const QString & fileName, const QByteArray & type )
{
  QFile file( fileName );
  if ( !file.open( QIODevice::ReadOnly ) ) {
    return reply.send( Status::NotFound );
  }
  reply.send( Status::Ok, file.readAll(), type );
}

QByteArray browserCss( QByteArray css )
{
  // Dictionary backends have already resolved relative CSS URLs against their archives.
  css.replace( "bres://", "/resource/" );
  css.replace( "qrc:///icons/", "/icons/" );
  return css;
}
} // namespace

LocalWebServer::LocalWebServer( const Instances::Groups & groups, const Config::Class & config ):
  groups( groups ),
  config( config )
{
  #if QT_VERSION < QT_VERSION_CHECK( 6, 8, 0 )
  using ResponderArg = QHttpServerResponder &&;
  #else
  using ResponderArg = QHttpServerResponder &;
  #endif
  auto handler = [ this ]( const QHttpServerRequest & request, ResponderArg responder ) {
    dispatch( request, std::move( responder ) );
  };
  http.route( "/", QHttpServerRequest::Method::Get, handler );
  http.router()->addConverter( QMetaType::fromType< QUrl >(), ".+" );
  http.route( "/<arg>",
              QHttpServerRequest::Method::Get,
              [ handler ]( QUrl, const QHttpServerRequest & request, ResponderArg responder ) {
                handler( request, static_cast< ResponderArg >( responder ) );
              } );
}

LocalWebServer::~LocalWebServer()
{
  setCatalogAvailable( false );
}

bool LocalWebServer::listen()
{
  auto listener = std::make_unique< QTcpServer >();
  if ( !listener->listen( QHostAddress::LocalHost, viewerPort ) ) {
    return false;
  }
  #if QT_VERSION >= QT_VERSION_CHECK( 6, 8, 0 )
  if ( !http.bind( listener.get() ) ) {
    return false;
  }
  #else
  http.bind( listener.get() );
  #endif
  port = listener->serverPort();
  listener.release(); // bind() transfers ownership to the HTTP server.
  return true;
}

QUrl LocalWebServer::url() const
{
  return QUrl( QStringLiteral( "http://127.0.0.1:%1/" ).arg( port ) );
}

void LocalWebServer::setCatalogAvailable( bool available )
{
  catalogAvailable = available;
  if ( !available ) {
    const auto children = requests.children();
    for ( auto child : children ) {
      auto reply = static_cast< BrowserReply * >( child );
      reply->send( Status::ServiceUnavailable );
      delete reply; // Release backend requests before replacing indices.
    }
  }
}

void LocalWebServer::dispatch( const QHttpServerRequest & request, QHttpServerResponder && responder )
{
  auto & reply               = *new BrowserReply( std::move( responder ), &requests );
  const QByteArray authority = "127.0.0.1:" + QByteArray::number( port );
  if ( request.value( "Host" ) != authority ) {
    return reply.send( Status::Forbidden );
  }
  if ( !catalogAvailable || requests.children().size() > 48 ) {
    return reply.send( Status::ServiceUnavailable );
  }
  const QUrl target  = request.url();
  const QString path = target.path( QUrl::FullyDecoded );
  if ( path == "/" || path == "/lookup" ) {
    return lookup( reply, target );
  }
  if ( path == "/viewer.js" ) {
    return serveFile( reply, ":/viewer/viewer.js", "text/javascript; charset=utf-8" );
  }
  if ( path == "/article.css" ) {
    QFile file( ":/article-style.css" );
    if ( !file.open( QIODevice::ReadOnly ) ) {
      return reply.send( Status::NotFound );
    }
    return reply.send( Status::Ok, browserCss( file.readAll() ), "text/css; charset=utf-8" );
  }
  if ( path == "/viewer.css" ) {
    return serveFile( reply, ":/viewer/viewer.css", "text/css; charset=utf-8" );
  }
  if ( path.startsWith( "/icons/" ) && !path.mid( 7 ).contains( '/' ) && Utils::isSafeUserFileName( path.mid( 7 ) ) ) {
    return serveFile( reply, ":" + path, getMimeTypeWithFallback( target ) );
  }
  if ( path.startsWith( "/resource/" ) ) {
    return resource( reply, path.mid( 10 ) );
  }
  reply.send( Status::NotFound );
}

void LocalWebServer::lookup( BrowserReply & reply, const QUrl & url )
{
  // HTML GET forms encode spaces as '+'. Preserve a literal plus encoded as %2B.
  QByteArray encoded = url.query( QUrl::FullyEncoded ).toLatin1().replace( "+", "%20" );
  QUrlQuery query( QString::fromLatin1( encoded ) );
  QString word = query.queryItemValue( "word", QUrl::FullyDecoded ).trimmed();
  if ( word.size() > 512 ) {
    return reply.send( Status::BadRequest );
  }
  unsigned groupId = query.hasQueryItem( "group" ) ? query.queryItemValue( "group" ).toUInt() : GroupId::AllGroupId;
  const auto group = groups.findGroup( groupId );
  if ( !group ) {
    return reply.send( Status::NotFound );
  }

  const auto nonce  = QUuid::createUuid().toRfc4122().toBase64();
  reply.page        = pageStart( word, groups, groupId, nonce );
  const auto finish = [ &reply, nonce ] {
    reply.page += "</main></div></body></html>";
    reply.send(Status::Ok, reply.page.toUtf8(), "text/html; charset=utf-8",
      "default-src 'none'; script-src 'nonce-" + nonce + "'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
      "font-src 'self' data:; media-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
  };
  if ( word.isEmpty() ) {
    return finish();
  }

  const auto * configured = config.getGroup( groupId );
  const auto & muted =
    configured && groupId != GroupId::AllGroupId ? configured->mutedDictionaries : config.mutedDictionaries;
  ArticleLookup::Dictionaries selected;
  for ( const auto & dict : group->dictionaries ) {
    if ( dict->isLocalDictionary() && !muted.contains( QString::fromStdString( dict->getId() ) ) ) {
      selected.push_back( dict );
    }
  }
  reply.lookup = std::make_unique< ArticleLookup >( word,
                                                    std::move( selected ),
                                                    QMap< QString, QString >{},
                                                    config.preferences.ignoreDiacritics );
  connect(
    reply.lookup.get(),
    &ArticleLookup::resultsReady,
    &reply,
    [ &reply, finish, found = false ]( const std::vector< ArticleLookup::Result > & results, bool complete ) mutable {
      for ( const auto & result : results ) {
        found               = true;
        const auto & data   = result.article->getFullData();
        const auto fragment = QByteArray::fromRawData( data.data(), data.size() ).toBase64();
        reply.page += QStringLiteral(
                        "<article id='dictionary-%2'><h2>%4%1</h2><section class='gdarticlebody' id='gd-%2' data-fragment='%3'></section>" )
                        .arg( QString::fromStdString( result.dictionary->getName() ).toHtmlEscaped(),
                              QString::fromStdString( result.dictionary->getId() ).toHtmlEscaped(),
                              QString::fromLatin1( fragment ),
                              dictionaryIconHtml( *result.dictionary ) );
        const auto error = result.article->getErrorString();
        if ( !error.isEmpty() ) {
          reply.page += "<p>" + error.toHtmlEscaped() + "</p>";
        }
        reply.page += "</article>";
      }
      if ( complete ) {
        if ( !found ) {
          reply.page += "<p>No entries found.</p>";
        }
        finish();
      }
    } );
  reply.lookup->start();
}

void LocalWebServer::resource( BrowserReply & reply, const QString & path )
{
  const auto sep = path.indexOf( '/' );
  if ( sep <= 0 ) {
    return reply.send( Status::NotFound );
  }
  const QString id = path.left( sep );
  QString name     = path.mid( sep + 1 );
  name.replace( '\\', '/' );
  if ( !Utils::isSafeUserFileName( name ) || name.startsWith( '/' ) || name.contains( ':' )
       || name.contains( QChar::Null ) ) {
    return reply.send( Status::BadRequest );
  }

  const auto all = groups.findGroup( GroupId::AllGroupId );
  if ( !all ) {
    return reply.send( Status::ServiceUnavailable );
  }
  for ( const auto & dictionary : all->dictionaries ) {
    if ( dictionary->isLocalDictionary() && QString::fromStdString( dictionary->getId() ) == id ) {
      reply.dictionary = dictionary;
      break;
    }
  }
  if ( !reply.dictionary ) {
    return reply.send( Status::NotFound );
  }
  try {
    reply.resource = reply.dictionary->getResource( name.toStdString() );
  }
  catch ( const std::exception & ) {
    return reply.send( Status::NotFound );
  }
  if ( !reply.resource ) {
    return reply.send( Status::NotFound );
  }
  auto deliver = [ &reply, name ] {
    if ( reply.resource->dataSize() < 0 ) {
      return reply.send( Status::NotFound );
    }
    const auto & data = reply.resource->getFullData();
    QByteArray bytes( data.data(), data.size() );
    QUrl url;
    url.setPath( name );
    const auto mime = getMimeTypeWithFallback( url, bytes );
    if ( mime.startsWith( "text/css" ) ) {
      bytes = browserCss( std::move( bytes ) );
    }
    else if ( !mime.startsWith( "image/" ) && !mime.startsWith( "audio/" ) && !mime.startsWith( "video/" )
              && !mime.startsWith( "font/" ) ) {
      return reply.send( Status::UnsupportedMediaType );
    }
    reply.send( Status::Ok, bytes, mime );
  };
  if ( reply.resource->isFinished() ) {
    deliver();
  }
  else {
    // Queue to the application thread and also cover a finish during connection.
    connect( reply.resource.get(), &Dictionary::Request::finished, &reply, deliver, Qt::QueuedConnection );
    if ( reply.resource->isFinished() ) {
      deliver();
    }
  }
}
#endif
