#pragma once
#include <string>

namespace rotate {

constexpr int kPanelPx = 720;  // square panel, pixels

struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Touch-mode layout shared by the renderer and hit testing (UI coordinates, before rotation).
namespace touch_layout {
constexpr Rect kPrev{110, 584, 140, 80};
constexpr Rect kPlay{290, 584, 140, 80};
constexpr Rect kNext{470, 584, 140, 80};
}  // namespace touch_layout

enum class TouchHit { None, Prev, PlayPause, Next };
TouchHit touch_hit(int ui_x, int ui_y);

// Maps a physical touch point to UI coordinates when the touch screen is drawn rotated by
// `rotation` degrees clockwise (0/90/180/270).
void unrotate(int rotation, int px, int py, int* ux, int* uy);

// Music player stub: no audio backend yet, only state so the UI and touch path are exercised.
struct MusicStub {
  bool playing = false;
  int track = 0;
  void apply(TouchHit h);
  std::string title() const;
  static constexpr int kTracks = 3;
};

}  // namespace rotate
