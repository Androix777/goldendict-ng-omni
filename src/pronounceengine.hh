#pragma once

#include <QObject>

/// Desktop pronunciation selection. Called on the application thread.
class PronounceEngine: public QObject
{
  Q_OBJECT
  bool occupied = false;

public:
  explicit PronounceEngine( QObject * parent = nullptr );
  void reset();
  void offer( const QString & link );
signals:
  void emitAudio( QString audioLink );
};
