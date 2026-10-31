#pragma once
#include <SDL.h>
#include <SDL_ttf.h>

#include <ctime>
#include <string>

#include "model.h"
#include "sysinfo.h"
#include "touch.h"

namespace rotate {

struct Scene {
  const MenuModel* model = nullptr;
  Stats stats;
  MusicStub music;
  std::tm now{};
  int touch_rotation = 0;   // degrees clockwise, applied to the Touch screen only
  std::string status;       // one-line message above the hint bar (e.g. launch failure)
  std::string roms_root;    // shown when the library is empty
};

class Ui {
 public:
  Ui(SDL_Renderer* r, const std::string& regular_ttf, const std::string& bold_ttf);
  ~Ui();
  bool ok() const { return ok_; }
  // Draws the whole 720x720 frame; does not present.
  void render(const Scene& s);

 private:
  enum class Align { Left, Center, Right };
  void text(TTF_Font* f, const std::string& s, int x, int y, SDL_Color c, Align a = Align::Left, int max_w = 0);
  void fill(SDL_Rect r, SDL_Color c);
  void draw_menu(const Scene& s);
  void draw_touch(const Scene& s);
  void header(const Scene& s, const std::string& title);
  void button(Rect r, const std::string& label);

  SDL_Renderer* r_;
  SDL_Texture* target_ = nullptr;
  TTF_Font *f18_ = nullptr, *f22_ = nullptr, *f30b_ = nullptr, *f40b_ = nullptr, *f120b_ = nullptr;
  bool ok_ = false;
};

// Looks for a font file: explicit path, $ROTATE_ASSETS/fonts, install dir, source tree, system DejaVu.
std::string find_font(const std::string& explicit_path, const std::string& file_name);

}  // namespace rotate
