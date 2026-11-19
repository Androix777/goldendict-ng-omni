#pragma once

#include "dict/dictionary.hh"

/// One lookup owns its dictionaries and all outstanding requests. Construct,
/// start and cancel it in the application's event loop.
class ArticleLookup: public QObject
{
  Q_OBJECT

public:
  using Dictionaries = std::vector< sptr< Dictionary::Class > >;
  struct Result
  {
    sptr< Dictionary::Class > dictionary;
    sptr< Dictionary::DataRequest > article;
  };

  ArticleLookup( QString word, Dictionaries dictionaries, QMap< QString, QString > contexts, bool ignoreDiacritics );
  ~ArticleLookup() override;
  void start();
  void cancel();

signals:
  /// Delivered in dictionary order, including the final empty batch for a miss.
  void resultsReady( const std::vector< Result > & results, bool complete );

private:
  void collectAlternates();
  void collectArticles();

  const QString word;
  const Dictionaries dictionaries;
  const QMap< QString, QString > contexts;
  const bool ignoreDiacritics;
  enum class Stage {
    Idle,
    Alternates,
    Articles,
    Done
  } stage = Stage::Idle;
  std::vector< sptr< Dictionary::WordSearchRequest > > alternates;
  std::vector< Result > pending;
  size_t next = 0;
};
