#include "article_lookup.hh"
#include <algorithm>
#include <set>

ArticleLookup::ArticleLookup( QString word,
                              Dictionaries dictionaries,
                              QMap< QString, QString > contexts,
                              bool ignoreDiacritics ):
  word( std::move( word ) ),
  dictionaries( std::move( dictionaries ) ),
  contexts( std::move( contexts ) ),
  ignoreDiacritics( ignoreDiacritics )
{
}

ArticleLookup::~ArticleLookup()
{
  cancel();
}

void ArticleLookup::start()
{
  if ( stage != Stage::Idle ) {
    return;
  }
  stage = Stage::Alternates;
  for ( const auto & dictionary : dictionaries ) {
    try {
      auto request = dictionary->findHeadwordsForSynonym( Text::removeTrailingZero( word ) );
      connect( request.get(),
               &Dictionary::Request::finished,
               this,
               &ArticleLookup::collectAlternates,
               Qt::QueuedConnection );
      alternates.push_back( std::move( request ) );
    }
    catch ( const std::exception & e ) {
      qWarning() << "Synonym lookup failed in" << dictionary->getName().c_str() << e.what();
    }
  }
  collectAlternates();
}

void ArticleLookup::collectAlternates()
{
  if ( stage != Stage::Alternates || std::any_of( alternates.begin(), alternates.end(), []( const auto & r ) {
         return !r->isFinished();
       } ) ) {
    return;
  }
  stage = Stage::Articles;
  std::set< std::u32string > words;
  for ( const auto & request : alternates ) {
    for ( const auto & match : request->getAllMatches() ) {
      words.insert( match.word );
    }
  }
  alternates.clear();
  const std::vector< std::u32string > variants( words.begin(), words.end() );
  const auto query = word.toStdU32String();
  for ( const auto & dictionary : dictionaries ) {
    sptr< Dictionary::DataRequest > request;
    try {
      if ( word == ":about" ) {
        auto description = std::make_shared< Dictionary::DataRequestInstant >( true );
        description->appendString( dictionary->getDescription().toStdString() );
        request = description;
      }
      else {
        request = dictionary->getArticle(
          query,
          variants,
          Text::removeTrailingZero( contexts.value( QString::fromStdString( dictionary->getId() ) ) ),
          ignoreDiacritics );
      }
    }
    catch ( const std::exception & e ) {
      request = std::make_shared< Dictionary::DataRequestInstant >( QString::fromUtf8( e.what() ) );
    }
    connect( request.get(),
             &Dictionary::Request::finished,
             this,
             &ArticleLookup::collectArticles,
             Qt::QueuedConnection );
    pending.push_back( { dictionary, std::move( request ) } );
  }
  collectArticles();
}

void ArticleLookup::collectArticles()
{
  if ( stage != Stage::Articles ) {
    return;
  }
  std::vector< Result > ready;
  while ( next < pending.size() && pending[ next ].article->isFinished() ) {
    auto result = std::move( pending[ next++ ] );
    if ( result.article->dataSize() >= 0 || !result.article->getErrorString().isEmpty() ) {
      ready.push_back( std::move( result ) );
    }
  }
  const bool complete = next == pending.size();
  if ( complete ) {
    stage = Stage::Done;
  }
  if ( complete || !ready.empty() ) {
    emit resultsReady( ready, complete );
  }
}

void ArticleLookup::cancel()
{
  if ( stage == Stage::Done ) {
    return;
  }
  stage = Stage::Done;
  for ( const auto & request : alternates ) {
    request->cancel();
  }
  for ( size_t i = next; i < pending.size(); ++i ) {
    pending[ i ].article->cancel();
  }
}
