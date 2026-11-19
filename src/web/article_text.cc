#ifdef WITH_WEB_VIEWER
  #include "local_web_server.hh"
  #include "local_web_reply.hh"

  #include <QFile>
  #include <QJsonArray>
  #include <QWebEnginePage>
  #include <QWebEngineProfile>
  #include <QWebEngineScriptCollection>
  #include <QWebEngineSettings>
  #include <QWebEngineUrlRequestInterceptor>
  #include <QWebEngineView>

namespace {
QString source( const QString & path )
{
  QFile file( path );
  return file.open( QIODevice::ReadOnly ) ? QString::fromUtf8( file.readAll() ) : QString{};
}

// The reader needs the local viewer shell and its resources. Keep it separate
// from the desktop article profile (cookies, custom scripts and URL handlers).
class ReaderRequests: public QWebEngineUrlRequestInterceptor
{
public:
  explicit ReaderRequests( QUrl origin ):
    origin( std::move( origin ) )
  {
  }
  void interceptRequest( QWebEngineUrlRequestInfo & info ) override
  {
    const auto url = info.requestUrl();
    if ( url.scheme() == "data" && info.resourceType() != QWebEngineUrlRequestInfo::ResourceTypeMainFrame ) {
      return;
    }
    const auto path  = url.path();
    const bool local = url.scheme() == origin.scheme() && url.host() == origin.host() && url.port() == origin.port();
    const bool asset = path == "/viewer.js" || path == "/viewer.css" || path == "/article.css"
      || path.startsWith( "/resource/" ) || path.startsWith( "/icons/" );
    info.block(
      !local || ( info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame ? url != origin : !asset ) );
  }

private:
  QUrl origin;
};

class TextJob: public QObject
{
public:
  TextJob( BrowserReply & reply, const QUrl & origin, QString word, QJsonArray articles ):
    QObject( &reply ),
    reply( &reply ),
    origin( origin ),
    word( std::move( word ) ),
    articles( std::move( articles ) ),
    requests( origin ),
    page( &profile )
  {
    profile.setUrlRequestInterceptor( &requests );
    page.settings()->setAttribute( QWebEngineSettings::LocalContentCanAccessFileUrls, false );
    view.setAttribute( Qt::WA_DontShowOnScreen );
    view.setAttribute( Qt::WA_ShowWithoutActivating );
    view.setAttribute( Qt::WA_QuitOnClose, false );
    view.setWindowFlag( Qt::WindowDoesNotAcceptFocus );
    view.setPage( &page );
    view.resize( 1000, 800 );
    // A realized viewport gives media queries and images the same dimensions as the browser reader.
    view.show();

    connect( &page, &QWebEnginePage::loadFinished, this, [ this ]( bool ok ) {
      if ( !this->reply ) {
        return;
      }
      if ( !ok ) {
        return this->reply->error( Status::InternalServerError, "Could not load the article reader" );
      }
      extract();
    } );
    connect( &page, &QWebEnginePage::renderProcessTerminated, this, [ this ] {
      if ( this->reply ) {
        this->reply->error( Status::InternalServerError, "Article renderer stopped" );
      }
    } );
  }

  void next()
  {
    static const auto layout = source( ":/viewer/text_layout.js" );
    const auto article       = articles.at( index ).toObject();
    const auto args          = QJsonDocument( QJsonArray{ article.value( "dictionaryId" ), article.value( "html" ) } )
                        .toJson( QJsonDocument::Compact );
    QWebEngineScript script;
    script.setWorldId( QWebEngineScript::MainWorld );
    script.setInjectionPoint( QWebEngineScript::DocumentCreation );
    script.setSourceCode( layout + "\nprepareTextArticle(..." + QString::fromUtf8( args ) + ");" );
    page.scripts().clear();
    page.scripts().insert( script );
    page.load( origin );
  }

private:
  void extract()
  {
    static const auto extractor = source( ":/viewer/article_text.js" );
    const QString js            = "extractTextArticle(" + extractor + ")";
    page.runJavaScript( js, [ this, guard = reply ]( const QVariant & result ) {
      if ( !guard ) { // WebEngine also delivers pending callbacks during destruction.
        return;
      }
      const auto extracted = QJsonDocument::fromJson( result.toString().toUtf8() ).object();
      if ( !extracted.value( "text" ).isString() ) {
        return guard->error( Status::InternalServerError, "Text conversion failed" );
      }
      auto article = articles.at( index ).toObject();
      article.remove( "html" );
      article.insert( "text", extracted.value( "text" ) );
      article.insert( "warnings", extracted.value( "warnings" ) );
      articles.replace( index++, article );
      if ( index == articles.size() ) {
        guard->json( Status::Ok, { { "word", word }, { "articles", articles } } );
      }
      else {
        next();
      }
    } );
  }

  QPointer< BrowserReply > reply;
  QUrl origin;
  QString word;
  QJsonArray articles;
  qsizetype index = 0;
  ReaderRequests requests;
  QWebEngineProfile profile;
  QWebEnginePage page;
  QWebEngineView view;
};
} // namespace

void LocalWebServer::renderText( BrowserReply & reply, const QString & word, const QJsonArray & articles )
{
  if ( activeTextRequests >= 2 ) {
    return reply.error( Status::ServiceUnavailable, "Text renderer busy" );
  }
  ++activeTextRequests;
  connect( &reply, &QObject::destroyed, this, [ this ] {
    --activeTextRequests;
  } );
  reply.timeout.start( 120000 );
  ( new TextJob( reply, url(), word, articles ) )->next();
}
#endif
