#include "touch.h"

namespace rotate {

TouchHit touch_hit(int x, int y) {
  if (touch_layout::kPrev.contains(x, y)) return TouchHit::Prev;
  if (touch_layout::kPlay.contains(x, y)) return TouchHit::PlayPause;
  if (touch_layout::kNext.contains(x, y)) return TouchHit::Next;
  return TouchHit::None;
}

void unrotate(int rotation, int px, int py, int* ux, int* uy) {
  const int n = kPanelPx;
  switch (((rotation % 360) + 360) % 360) {
    case 90: *ux = py; *uy = n - 1 - px; break;
    case 180: *ux = n - 1 - px; *uy = n - 1 - py; break;
    case 270: *ux = n - 1 - py; *uy = px; break;
    default: *ux = px; *uy = py; break;
  }
}

void MusicStub::apply(TouchHit h) {
  switch (h) {
    case TouchHit::Prev: track = (track + kTracks - 1) % kTracks; break;
    case TouchHit::Next: track = (track + 1) % kTracks; break;
    case TouchHit::PlayPause: playing = !playing; break;
    default: break;
  }
}

std::string MusicStub::title() const {
  static const char* names[kTracks] = {"Track 1 - placeholder", "Track 2 - placeholder", "Track 3 - placeholder"};
  return names[track];
}

}  // namespace rotate
