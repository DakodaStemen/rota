// rotate-shots: renders every screen to a 720x720 PNG with a software renderer (no display needed).
#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "library.h"
#include "model.h"
#include "png.h"
#include "ui.h"
#include "util.h"

using namespace rotate;

static SystemEntry demo_system(const char* id, const char* name, std::vector<const char*> titles) {
  SystemEntry e;
  e.def = {id, name, "core", {"rom"}};
  for (auto* t : titles) e.games.push_back({id, t, std::string("/roms/") + id + "/" + t + ".rom"});
  return e;
}

static bool shoot(SDL_Renderer* r, SDL_Surface* surf, Ui& ui, const Scene& s, const std::string& path) {
  ui.render(s);
  SDL_RenderPresent(r);
  std::vector<uint8_t> px(static_cast<size_t>(kPanelPx) * kPanelPx * 3);
  std::vector<uint8_t> rgba(static_cast<size_t>(kPanelPx) * kPanelPx * 4);
  if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, rgba.data(), kPanelPx * 4) != 0) return false;
  std::set<uint32_t> colors;
  for (size_t i = 0; i < static_cast<size_t>(kPanelPx) * kPanelPx; ++i) {
    px[i * 3] = rgba[i * 4];
    px[i * 3 + 1] = rgba[i * 4 + 1];
    px[i * 3 + 2] = rgba[i * 4 + 2];
    if (colors.size() < 16) colors.insert(rgba[i * 4] << 16 | rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2]);
  }
  (void)surf;
  if (colors.size() < 5) {
    std::fprintf(stderr, "%s: blank render (%zu colors)\n", path.c_str(), colors.size());
    return false;
  }
  if (!write_png_rgb(path, kPanelPx, kPanelPx, px.data())) return false;
  std::printf("wrote %s\n", path.c_str());
  return true;
}

int main(int argc, char** argv) {
  std::string out = "artifacts";
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
  make_dirs(out);
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {  // SDL_VIDEODRIVER=dummy|offscreen is fine; the surface renderer needs none
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, kPanelPx, kPanelPx, 32, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
  Ui ui(r, find_font("", "DejaVuSans.ttf"), find_font("", "DejaVuSans-Bold.ttf"));
  if (!r || !ui.ok()) {
    std::fprintf(stderr, "renderer/font init failed\n");
    return 1;
  }

  std::vector<SystemEntry> lib = {
      demo_system("gba", "Game Boy Advance", {"Advance Wars", "Drill Dozer", "Metroid Fusion", "Wario Land 4"}),
      demo_system("gbc", "Game Boy Color", {"Link's Awakening DX", "Pokemon Crystal"}),
      demo_system("nds", "Nintendo DS", {"Chrono Trigger", "Hotel Dusk: Room 215", "Professor Layton and the Curious Village"}),
      demo_system("psx", "PlayStation", {"Castlevania: Symphony of the Night", "Crash Bandicoot", "Final Fantasy VII", "Gran Turismo 2", "Metal Gear Solid", "Parasite Eve", "Resident Evil 2", "Spyro the Dragon", "Tekken 3", "Vagrant Story"}),
      demo_system("snes", "Super Nintendo", {"Chrono Trigger", "Super Metroid"}),
  };
  std::vector<Game> recent = {lib[3].games[0], lib[0].games[2], lib[2].games[0]};
  MenuModel m;
  m.set_library(lib, recent);
  m.set_profile(Profile::Balanced);

  Scene s;
  s.model = &m;
  s.stats.battery_pct = 82;
  s.stats.cpu_khz = 1600000;
  s.stats.temp_mc = 41500;
  s.stats.governor = "schedutil";
  s.now = {};
  s.now.tm_hour = 14; s.now.tm_min = 7; s.now.tm_mday = 30; s.now.tm_mon = 9; s.now.tm_year = 126; s.now.tm_wday = 5;
  s.roms_root = "/roms";
  s.touch_rotation = 90;
  s.music.playing = true;
  s.music.track = 1;

  bool ok = true;
  ok &= shoot(r, surf, ui, s, out + "/01-systems.png");
  m.input(Nav::Down); m.input(Nav::Down); m.input(Nav::Down);  // PlayStation (after Recent, GBA, GBC, NDS)
  m.input(Nav::Down);
  m.input(Nav::Accept);
  for (int i = 0; i < 5; ++i) m.input(Nav::Down);
  ok &= shoot(r, surf, ui, s, out + "/02-games-scrolled.png");
  m.input(Nav::Power);
  m.input(Nav::Down);
  m.input(Nav::Down);
  ok &= shoot(r, surf, ui, s, out + "/03-power-profile.png");
  m.input(Nav::Back);
  m.enter_touch();
  ok &= shoot(r, surf, ui, s, out + "/04-touch-rot90.png");
  s.touch_rotation = 0;
  ok &= shoot(r, surf, ui, s, out + "/05-touch-rot0.png");
  m.leave_touch();
  MenuModel empty;
  empty.set_profile(Profile::Save);
  s.model = &empty;
  s.stats.battery_pct = -1;
  ok &= shoot(r, surf, ui, s, out + "/06-empty-library.png");
  SDL_Quit();
  return ok ? 0 : 1;
}
