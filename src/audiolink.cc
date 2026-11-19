/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "audiolink.hh"
#include "utils.hh"

void ArticleAudioLink::consider( const QString & url )
{
  if ( first.isEmpty() && Utils::Url::isAudioUrl( QUrl( url ) ) ) {
    first = url;
  }
}
