#ifdef WITH_WEB_VIEWER
  #include "local_web_server.hh"
  #include "local_web_reply.hh"

  #include <QJsonArray>
  #include <QUrlQuery>
  #include <algorithm>

namespace {
bool validParameters( const QUrlQuery & query, QStringList allowed )
{
  for ( const auto & item : query.queryItems( QUrl::FullyDecoded ) ) {
    if ( !allowed.removeOne( item.first ) ) {
      return false; // Unknown or repeated parameter.
    }
  }
  return true;
}
} // namespace

void LocalWebServer::api( BrowserReply & reply, const QString & path, const QUrlQuery & query )
{
  if ( path == "/api/v1/groups" ) {
    if ( !query.isEmpty() ) {
      return reply.error( Status::BadRequest, "Groups takes no parameters" );
    }
    QJsonArray list;
    for ( const auto & group : groups ) {
      list.append( QJsonObject{ { "id", qint64( group.id ) }, { "name", group.name } } );
    }
    return reply.json( Status::Ok, { { "groups", list } } );
  }

  const bool catalog = path == "/api/v1/dictionaries";
  if ( !catalog && path != "/api/v1/lookup" ) {
    return reply.error( Status::NotFound, "Unknown API endpoint" );
  }
  const QStringList allowed = catalog ? QStringList{ "group" } : QStringList{ "word", "dictionary", "group", "format" };
  if ( !validParameters( query, allowed ) ) {
    return reply.error( Status::BadRequest, "Unknown or repeated parameter" );
  }
  const auto format = query.hasQueryItem( "format" ) ? query.queryItemValue( "format", QUrl::FullyDecoded ) : "html";
  if ( format != "html" && format != "text" ) {
    return reply.error( Status::BadRequest, "Expected format=html or format=text" );
  }
  unsigned groupId = GroupId::AllGroupId;
  if ( query.hasQueryItem( "group" ) ) {
    if ( query.hasQueryItem( "dictionary" ) ) {
      return reply.error( Status::BadRequest, "Choose either group or dictionary" );
    }
    bool ok = false;
    groupId = query.queryItemValue( "group", QUrl::FullyDecoded ).toUInt( &ok );
    if ( !ok ) {
      return reply.error( Status::BadRequest, "Invalid group ID" );
    }
  }
  const auto group = groups.findGroup( groupId );
  if ( !group ) {
    return reply.error( Status::NotFound, "Unknown group" );
  }
  if ( catalog ) {
    QJsonArray list;
    const auto & muted = mutedDictionaries( groupId );
    for ( const auto & dict : localDictionaries( *group, false ) ) {
      const auto id = QString::fromStdString( dict->getId() );
      list.append( QJsonObject{ { "id", id },
                                { "name", QString::fromStdString( dict->getName() ) },
                                { "enabled", !muted.contains( id ) } } );
    }
    return reply.json( Status::Ok, { { "dictionaries", list } } );
  }

  const QString word = query.queryItemValue( "word", QUrl::FullyDecoded ).trimmed();
  if ( word.isEmpty() || word.size() > 512 || word.contains( QChar::Null ) ) {
    return reply.error( Status::BadRequest, "Expected a nonempty word of at most 512 UTF-16 code units" );
  }
  auto selected = localDictionaries( *group, true );
  if ( query.hasQueryItem( "dictionary" ) ) {
    const auto id = query.queryItemValue( "dictionary", QUrl::FullyDecoded ).toStdString();
    if ( id.empty() ) {
      return reply.error( Status::BadRequest, "Empty dictionary ID" );
    }
    const auto it = std::find_if( selected.begin(), selected.end(), [ &id ]( const auto & dict ) {
      return dict->getId() == id;
    } );
    if ( it == selected.end() ) {
      return reply.error( Status::NotFound, "Unknown or disabled dictionary" );
    }
    selected = { *it };
  }
  reply.lookup = createLookup( word, std::move( selected ) );
  connect( reply.lookup.get(),
           &ArticleLookup::resultsReady,
           &reply,
           [ this, &reply, word, format, articles = QJsonArray{} ]( const std::vector< ArticleLookup::Result > & results,
                                                      bool complete ) mutable {
             for ( const auto & result : results ) {
               const auto & data = result.article->getFullData();
               QJsonObject article{ { "dictionaryId", QString::fromStdString( result.dictionary->getId() ) },
                                    { "dictionaryName", QString::fromStdString( result.dictionary->getName() ) },
                                    { "html", QString::fromUtf8( data.data(), data.size() ) } };
               const auto error = result.article->getErrorString();
               if ( !error.isEmpty() ) {
                 article.insert( "error", error );
               }
               articles.append( article );
             }
             if ( complete ) {
               if ( format == "text" && !articles.isEmpty() ) {
                 return renderText( reply, word, articles );
               }
               reply.json( Status::Ok, { { "word", word }, { "articles", articles } } );
             }
           } );
  reply.lookup->start();
}
#endif
