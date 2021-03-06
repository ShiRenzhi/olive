#include "tracklist.h"

#include "timeline/sequence.h"

TrackList::TrackList(Sequence *parent, olive::TrackType type) :
  QObject(parent),
  type_(type)
{
  // Ensure we have at least one track
  AddTrack();
}

void TrackList::Save(QXmlStreamWriter &stream)
{
  stream.writeStartElement("Tracks");

  for (int i=0;i<tracks_.size();i++) {
    tracks_.at(i)->Save(stream);
  }

  stream.writeEndElement(); // Tracks
}

TrackList* TrackList::copy(Sequence *parent)
{
  TrackList* t = new TrackList(parent, type_);

  t->ResizeTrackArray(tracks_.size());
  for (int i=0;i<tracks_.size();i++) {
    t->tracks_[i] = tracks_.at(i)->copy(t);
  }

  return t;
}

void TrackList::AddTrack()
{
  Track* track = new Track(this, type_);
  tracks_.append(track);

  emit TrackCountChanged();
}

void TrackList::RemoveTrack(int i)
{
  if (tracks_.size() == 1) {
    return;
  }
  tracks_.removeAt(i);

  emit TrackCountChanged();
}

Track *TrackList::First()
{
  return tracks_.first();
}

Track *TrackList::Last()
{
  return tracks_.last();
}

int TrackList::TrackCount()
{
  return tracks_.size();
}

int TrackList::IndexOfTrack(Track *track)
{
  for (int i=0;i<tracks_.size();i++) {
    if (tracks_.at(i) == track) {
      return i;
    }
  }

  return -1;
}

Track *TrackList::TrackAt(int i)
{
  while (i >= tracks_.size()) {
    AddTrack();
  }

  return tracks_.at(i);
}

QVector<Track*> TrackList::tracks()
{
  return tracks_;
}

olive::TrackType TrackList::type()
{
  return type_;
}

Sequence *TrackList::GetParent()
{
  return static_cast<Sequence*>(parent());
}

void TrackList::ResizeTrackArray(int i)
{
  tracks_.resize(i);
}
