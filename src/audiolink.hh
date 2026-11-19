/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QString>

/// First playable pronunciation supplied by a dictionary while producing one article.
class ArticleAudioLink
{
  QString first;

public:
  void consider( const QString & url );
  const QString & url() const
  {
    return first;
  }
};
