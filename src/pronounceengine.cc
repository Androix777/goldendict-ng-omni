#include "pronounceengine.hh"

PronounceEngine::PronounceEngine( QObject * parent ):
  QObject( parent )
{
}

void PronounceEngine::reset()
{
  occupied = false;
}

void PronounceEngine::offer( const QString & link )
{
  if ( !occupied && !link.isEmpty() ) {
    occupied = true;
    emit emitAudio( link );
  }
}
