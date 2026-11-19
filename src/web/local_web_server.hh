#pragma once
#ifdef WITH_WEB_VIEWER
  #include "instances.hh"
  #include <QHttpServer>

class BrowserReply;

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
  void resource( BrowserReply & reply, const QString & path );

  const Instances::Groups & groups;
  const Config::Class & config;
  QHttpServer http;
  QObject requests;
  bool catalogAvailable = true;
  quint16 port          = 0;
};
#endif
