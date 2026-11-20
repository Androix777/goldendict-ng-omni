#ifdef WITH_WEB_VIEWER
  #include "local_web_server.hh"
  #include "local_web_reply.hh"
  #include "common/htmlescape.hh"
  #include "headwordscache.hh"

  #include <QFileInfo>
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

QJsonValue fileSizeBytes( Dictionary::Class & dict )
{
  const auto & files = dict.getDictionaryFilenames();
  qint64 bytes       = 0;
  for ( const auto & path : files ) {
    const QFileInfo file( QString::fromStdString( path ) );
    if ( !file.isFile() ) {
      return QJsonValue::Null;
    }
    bytes += file.size();
  }
  return files.empty() ? QJsonValue( QJsonValue::Null ) : QJsonValue( bytes );
}

QJsonObject dictionaryInfo( Dictionary::Class & dict, bool enabled, bool details )
{
  QJsonObject info{ { "id", QString::fromStdString( dict.getId() ) },
                    { "name", QString::fromStdString( dict.getName() ) },
                    { "enabled", enabled },
                    { "articleCount", qint64( dict.getArticleCount() ) },
                    { "wordCount", qint64( dict.getWordCount() ) },
                    { "fileSizeBytes", fileSizeBytes( dict ) } };
  if ( details ) {
    const QString description = Html::unescape( dict.getDescription(), Html::HtmlOption::Keep ).trimmed();
    info.insert( "description",
                 description.isEmpty() || description == "NONE" ? QJsonValue( QJsonValue::Null ) :
                                                                  QJsonValue( description ) );
    HeadwordCache::HeadwordCache cache;
    const bool supported = dict.supportsHeadwordsCache();
    const bool ready     = supported && cache.open( dict.headwordsCachePath() );
    info.insert( "headwordCount", ready ? QJsonValue( qint64( cache.count() ) ) : QJsonValue( QJsonValue::Null ) );
    info.insert( "headwordStatus", ready ? "ready" : supported ? "unavailable" : "unsupported" );
  }
  return info;
}

bool indexNumber( const QString & value, quint32 & number )
{
  bool ok = false;
  number  = value.toUInt( &ok );
  return ok && std::all_of( value.begin(), value.end(), []( QChar c ) {
           return c >= '0' && c <= '9';
         } );
}

void headwords( BrowserReply & reply, Dictionary::Class & dict, const QUrlQuery & query )
{
  constexpr quint32 maxPage  = 100;
  const bool explicitIndices = query.hasQueryItem( "indices" );
  const bool sample          = query.queryItemValue( "mode" ) == "sample";
  if ( ( query.hasQueryItem( "mode" ) && !sample && query.queryItemValue( "mode" ) != "page" )
       || ( explicitIndices
            && ( query.hasQueryItem( "offset" ) || query.hasQueryItem( "limit" ) || query.hasQueryItem( "mode" ) ) )
       || ( sample && query.hasQueryItem( "offset" ) ) ) {
    return reply.error( Status::BadRequest, "Choose offset/limit, mode=sample with limit, or indices" );
  }
  quint32 limit = 20, offset = 0;
  if ( ( query.hasQueryItem( "limit" ) && !indexNumber( query.queryItemValue( "limit" ), limit ) ) || limit < 1
       || limit > maxPage
       || ( query.hasQueryItem( "offset" ) && !indexNumber( query.queryItemValue( "offset" ), offset ) ) ) {
    return reply.error( Status::BadRequest, "Expected a nonnegative offset and limit from 1 to 100" );
  }
  QList< quint32 > indices;
  if ( explicitIndices ) {
    const auto values = query.queryItemValue( "indices", QUrl::FullyDecoded ).split( ',' );
    if ( values.size() > maxPage ) {
      return reply.error( Status::BadRequest, "At most 100 indices per request" );
    }
    for ( const auto & value : values ) {
      quint32 index;
      if ( !indexNumber( value, index ) ) {
        return reply.error( Status::BadRequest, "Expected comma-separated nonnegative indices" );
      }
      indices.append( index );
    }
  }
  if ( !dict.supportsHeadwordsCache() ) {
    return reply.error( Status::NotImplemented, "This dictionary does not support indexed headwords" );
  }
  HeadwordCache::HeadwordCache cache;
  if ( !cache.open( dict.headwordsCachePath() ) ) {
    return reply.error( Status::ServiceUnavailable, "Headword index unavailable; retry after background indexing" );
  }
  const quint32 total = cache.count();
  QJsonValue nextOffset( QJsonValue::Null );
  if ( sample ) {
    const quint32 n = std::min( total, limit );
    for ( quint32 i = 0; i < n; ++i ) {
      // Take the midpoint of each interval, avoiding a bias toward the first entries.
      indices.append( quint32( ( 2 * i + 1 ) * quint64( total ) / ( 2 * n ) ) );
    }
  }
  else if ( !explicitIndices && offset < total ) {
    const quint32 end = offset + std::min( total - offset, limit );
    for ( quint32 i = offset; i < end; ++i ) {
      indices.append( i );
    }
    if ( end < total ) {
      nextOffset = qint64( end );
    }
  }
  QJsonArray words;
  for ( const auto index : indices ) {
    if ( index >= total ) {
      return reply.error( Status::BadRequest,
                          QStringLiteral( "Index %1 is outside headword count %2" ).arg( index ).arg( total ) );
    }
    const QString word = cache.at( index );
    if ( word.isEmpty() ) {
      return reply.error( Status::InternalServerError, "Could not read the indexed headword" );
    }
    words.append( QJsonObject{ { "index", qint64( index ) }, { "word", word } } );
  }
  reply.json( Status::Ok,
              { { "dictionaryId", QString::fromStdString( dict.getId() ) },
                { "total", qint64( total ) },
                { "headwords", words },
                { "nextOffset", nextOffset } } );
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
  const bool browse  = path == "/api/v1/headwords";
  if ( !catalog && !browse && path != "/api/v1/lookup" ) {
    return reply.error( Status::NotFound, "Unknown API endpoint" );
  }
  const QStringList allowed = catalog ? QStringList{ "group", "dictionary" } :
    browse                            ? QStringList{ "dictionary", "indices", "offset", "limit", "mode" } :
                                        QStringList{ "word", "dictionary", "group", "format" };
  if ( !validParameters( query, allowed ) ) {
    return reply.error( Status::BadRequest, "Unknown or repeated parameter" );
  }
  const auto format = query.hasQueryItem( "format" ) ? query.queryItemValue( "format", QUrl::FullyDecoded ) : "html";
  if ( format != "html" && format != "text" ) {
    return reply.error( Status::BadRequest, "Expected format=html or format=text" );
  }
  unsigned groupId = GroupId::AllGroupId;
  if ( query.hasQueryItem( "group" ) ) {
    if ( !catalog && query.hasQueryItem( "dictionary" ) ) {
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
  const QString word = query.queryItemValue( "word", QUrl::FullyDecoded ).trimmed();
  if ( !catalog && !browse && ( word.isEmpty() || word.size() > 512 || word.contains( QChar::Null ) ) ) {
    return reply.error( Status::BadRequest, "Expected a nonempty word of at most 512 UTF-16 code units" );
  }
  const bool singleDictionary = query.hasQueryItem( "dictionary" );
  if ( browse && !singleDictionary ) {
    return reply.error( Status::BadRequest, "Expected dictionary ID" );
  }
  auto selected = localDictionaries( *group, !catalog );
  if ( singleDictionary ) {
    const auto id = query.queryItemValue( "dictionary", QUrl::FullyDecoded ).toStdString();
    if ( id.empty() ) {
      return reply.error( Status::BadRequest, "Empty dictionary ID" );
    }
    const auto it = std::find_if( selected.begin(), selected.end(), [ &id ]( const auto & dict ) {
      return dict->getId() == id;
    } );
    if ( it == selected.end() ) {
      return reply.error( Status::NotFound, "Dictionary unavailable in this group" );
    }
    selected = { *it };
  }
  if ( catalog ) {
    QJsonArray list;
    const auto & muted = mutedDictionaries( groupId );
    try {
      for ( const auto & dict : selected ) {
        list.append(
          dictionaryInfo( *dict, !muted.contains( QString::fromStdString( dict->getId() ) ), singleDictionary ) );
      }
    }
    catch ( const std::exception & ) {
      return reply.error( Status::InternalServerError, "Could not read dictionary metadata" );
    }
    return reply.json( Status::Ok, { { "dictionaries", list } } );
  }
  if ( browse ) {
    return headwords( reply, *selected.front(), query );
  }
  reply.lookup = createLookup( word, std::move( selected ) );
  connect( reply.lookup.get(),
           &ArticleLookup::resultsReady,
           &reply,
           [ this, &reply, word, format, articles = QJsonArray{} ]( const auto & results, bool complete ) mutable {
             for ( const auto & result : results ) {
               QJsonObject article{ { "dictionaryId", QString::fromStdString( result.dictionary->getId() ) },
                                    { "dictionaryName", QString::fromStdString( result.dictionary->getName() ) } };
               const auto error = result.article->getErrorString();
               if ( !error.isEmpty() ) {
                 article.insert( "error", error );
               }
               for ( const auto & html : result.article->getArticleFragments() ) {
                 article.insert( "html", html );
                 articles.append( article );
               }
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
