#include "ui.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>

#include "theme.h"

#ifndef ROTATE_ASSET_DIR
#define ROTATE_ASSET_DIR ""
#endif

namespace rotate {

static bool exists(const std::string& p) {
  struct stat st;
  return !p.empty() && stat(p.c_str(), &st) == 0;
}

std::string find_font(const std::string& explicit_path, const std::string& file_name) {
  std::string cands[5];
  cands[0] = explicit_path;
  if (const char* a = std::getenv("ROTATE_ASSETS")) cands[1] = std::string(a) + "/fonts/" + file_name;
  cands[2] = "/usr/share/rotate/fonts/" + file_name;
  cands[3] = std::string(ROTATE_ASSET_DIR) + "/fonts/" + file_name;
  cands[4] = "/usr/share/fonts/truetype/dejavu/" + file_name;
  for (const auto& c : cands)
    if (exists(c)) return c;
  return "";
}

Ui::Ui(SDL_Renderer* r, const std::string& reg, const std::string& bold) : r_(r) {
  if (!TTF_WasInit() && TTF_Init() != 0) return;
  f18_ = TTF_OpenFont(reg.c_str(), 18);
  f22_ = TTF_OpenFont(reg.c_str(), 24);
  f30b_ = TTF_OpenFont(bold.c_str(), 30);
  f40b_ = TTF_OpenFont(bold.c_str(), 42);
  f120b_ = TTF_OpenFont(bold.c_str(), 120);
  ok_ = f18_ && f22_ && f30b_ && f40b_ && f120b_;
}

Ui::~Ui() {
  for (TTF_Font* f : {f18_, f22_, f30b_, f40b_, f120b_})
    if (f) TTF_CloseFont(f);
  if (target_) SDL_DestroyTexture(target_);
}

void Ui::fill(SDL_Rect rc, SDL_Color c) {
  SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
  SDL_RenderFillRect(r_, &rc);
}

void Ui::text(TTF_Font* f, const std::string& str, int x, int y, SDL_Color c, Align a, int max_w) {
  if (str.empty()) return;
  std::string s = str;
  int w = 0, h = 0;
  TTF_SizeUTF8(f, s.c_str(), &w, &h);
  while (max_w > 0 && w > max_w && s.size() > 1) {  // ellipsize on a UTF-8 boundary
    do s.pop_back();
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80);
    TTF_SizeUTF8(f, (s + "\xe2\x80\xa6").c_str(), &w, &h);
    if (w <= max_w) {
      s += "\xe2\x80\xa6";
      break;
    }
  }
  SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
  if (!surf) return;
  SDL_Texture* t = SDL_CreateTextureFromSurface(r_, surf);
  int dx = a == Align::Left ? x : a == Align::Center ? x - surf->w / 2 : x - surf->w;
  SDL_Rect dst{dx, y, surf->w, surf->h};
  SDL_FreeSurface(surf);
  if (t) {
    SDL_RenderCopy(r_, t, nullptr, &dst);
    SDL_DestroyTexture(t);
  }
}

void Ui::header(const Scene& s, const std::string& title) {
  using namespace theme;
  text(f40b_, title, 40, 28, kText, Align::Left, 400);
  char buf[64];
  std::string right = s.stats.battery_pct >= 0 ? (std::snprintf(buf, sizeof buf, "Battery %d%%", s.stats.battery_pct), buf) : "Battery --";
  text(f22_, right, 680, 24, kText, Align::Right);
  text(f18_, profile_label(s.model->profile()), 680, 56, kDim, Align::Right);
  fill({40, 92, 640, 2}, kDim);
}

void Ui::draw_menu(const Scene& s) {
  using namespace theme;
  const MenuModel& m = *s.model;
  header(s, m.title());
  auto rows = m.rows();
  if (rows.empty()) {
    text(f30b_, "No games found", 360, 280, kText, Align::Center);
    text(f22_, "Copy ROMs to " + s.roms_root + "/<system>/", 360, 330, kDim, Align::Center, 640);
  }
  const bool power = m.screen() == Screen::Power;
  const int kRowH = power ? 84 : 60, kTop = 108;
  int first = m.first_visible();
  for (int i = 0; i < MenuModel::kVisibleRows && first + i < static_cast<int>(rows.size()); ++i) {
    int idx = first + i, y = kTop + i * kRowH;
    bool sel = idx == m.selected();
    bool active = m.screen() == Screen::Power && idx == static_cast<int>(m.profile());
    if (sel) fill({32, y, 656, kRowH - 6}, kAccent);
    SDL_Color fg = sel ? kBg : kText, sub = sel ? kBg : kDim;
    if (sel) text(f30b_, ">", 46, y + 7, fg);
    text(f30b_, rows[idx].title, 84, y + 7, fg, Align::Left, rows[idx].subtitle.empty() || m.screen() == Screen::Power ? 440 : 360);
    if (m.screen() == Screen::Power) {
      text(f18_, rows[idx].subtitle, 84, y + 42, sub, Align::Left, 560);
      if (active) text(f22_, "ACTIVE", 668, y + 12, fg, Align::Right);
    } else {
      text(f22_, rows[idx].subtitle, 668, y + 12, sub, Align::Right, 280);
    }
  }
  if (m.total() > MenuModel::kVisibleRows) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d / %d", m.selected() + 1, m.total());
    text(f18_, buf, 680, 596, kDim, Align::Right);
  }
  if (!s.status.empty()) text(f22_, s.status, 40, 596, kAccent, Align::Left, 520);
  fill({40, 640, 640, 2}, kDim);
  text(f22_, m.hint(), 360, 660, kDim, Align::Center, 640);
}

void Ui::button(Rect b, const std::string& label) {
  using namespace theme;
  fill({b.x, b.y, b.w, b.h}, kBg);
  fill({b.x, b.y, b.w, 3}, kAccent);
  text(f30b_, label, b.x + b.w / 2, b.y + 22, kText, Align::Center);
}

void Ui::draw_touch(const Scene& s) {
  using namespace theme;
  char buf[96];
  std::strftime(buf, sizeof buf, "%H:%M", &s.now);
  text(f120b_, buf, 360, 10, kText, Align::Center);
  std::strftime(buf, sizeof buf, "%A, %d %B", &s.now);
  text(f22_, buf, 360, 160, kDim, Align::Center);

  struct Tile { const char* label; std::string value; };
  Tile tiles[3];
  tiles[0] = {"BATTERY", s.stats.battery_pct >= 0 ? std::to_string(s.stats.battery_pct) + "%" : "--"};
  if (s.stats.cpu_khz >= 0) {
    std::snprintf(buf, sizeof buf, "%.2f", s.stats.cpu_khz / 1e6);
    tiles[1] = {"CPU GHz", buf};
  } else {
    tiles[1] = {"CPU GHz", "--"};
  }
  if (s.stats.temp_mc != INT_MIN) {
    std::snprintf(buf, sizeof buf, "%.1f\xc2\xb0""C", s.stats.temp_mc / 1000.0);
    tiles[2] = {"TEMP", buf};
  } else {
    tiles[2] = {"TEMP", "--"};
  }
  for (int i = 0; i < 3; ++i) {
    int x = 40 + i * 220;
    fill({x, 210, 200, 130}, kPanel);
    text(f18_, tiles[i].label, x + 100, 226, kDim, Align::Center);
    text(f40b_, tiles[i].value, x + 100, 270, kText, Align::Center, 184);
  }
  std::string gov = s.stats.governor.empty() ? "unknown" : s.stats.governor;
  text(f22_, "Profile: " + std::string(profile_label(s.model->profile())) + "   Governor: " + gov, 360, 356, kDim, Align::Center, 640);

  fill({40, 410, 640, 270}, kPanel);
  text(f18_, "MUSIC PLAYER (stub, no audio yet)", 60, 426, kDim);
  text(f30b_, s.music.title(), 60, 460, kText, Align::Left, 600);
  text(f22_, s.music.playing ? "Playing" : "Paused", 60, 510, kDim);
  button(touch_layout::kPrev, "Prev");
  button(touch_layout::kPlay, s.music.playing ? "Pause" : "Play");
  button(touch_layout::kNext, "Next");
  text(f18_, "Swivel up to return to games", 360, 696 - 4, kDim, Align::Center);
}

void Ui::render(const Scene& s) {
  using namespace theme;
  bool touch = s.model->screen() == Screen::Touch;
  if (touch && s.touch_rotation != 0) {
    if (!target_)
      target_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, kPanelPx, kPanelPx);
    SDL_SetRenderTarget(r_, target_);
    SDL_SetRenderDrawColor(r_, kBg.r, kBg.g, kBg.b, 255);
    SDL_RenderClear(r_);
    draw_touch(s);
    SDL_SetRenderTarget(r_, nullptr);
    SDL_SetRenderDrawColor(r_, kBg.r, kBg.g, kBg.b, 255);
    SDL_RenderClear(r_);
    SDL_RenderCopyEx(r_, target_, nullptr, nullptr, s.touch_rotation, nullptr, SDL_FLIP_NONE);
    return;
  }
  SDL_SetRenderDrawColor(r_, kBg.r, kBg.g, kBg.b, 255);
  SDL_RenderClear(r_);
  if (touch) draw_touch(s);
  else draw_menu(s);
}

}  // namespace rotate
