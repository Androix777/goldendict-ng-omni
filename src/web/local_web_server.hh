#pragma once
#ifdef WITH_WEB_VIEWER
  #include "article_lookup.hh"
  #include "instances.hh"
  #include <QHttpServer>

class BrowserReply;
class QJsonArray;

/// Local reader for the application's file-backed dictionaries.
class LocalWebServer: public QObject
{
public:
  LocalWebServer( const Instances::Groups & groups, const Config::Class & config );
  ~LocalWebServer() override;
  bool listen();
  QUrl url() const;
  void setCatalogAvailable( bool available );

private:
  void dispatch( const QHttpServerRequest & request, QHttpServerResponder && responder );
  void lookup( BrowserReply & reply, const QUrl & url );
  void api( BrowserReply & reply, const QString & path, const QUrlQuery & query );
  void renderText( BrowserReply & reply, const QString & word, const QJsonArray & articles );
  const QSet< QString > & mutedDictionaries( unsigned groupId ) const;
  ArticleLookup::Dictionaries localDictionaries( const Instances::Group & group, bool enabledOnly ) const;
  std::unique_ptr< ArticleLookup > createLookup( const QString & word, ArticleLookup::Dictionaries selected ) const;
  void resource( BrowserReply & reply, const QString & path );

  const Instances::Groups & groups;
  const Config::Class & config;
  QHttpServer http;
  QObject requests;
  unsigned activeTextRequests = 0;
  bool catalogAvailable = true;
  quint16 port          = 0;
};
#endif
