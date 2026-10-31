// rotate-frontend: games-first menu on SDL2 (KMSDRM on device). Event-driven: sleeps between 20 ms input
// polls and only redraws on input, on swivel events, or on the touch-mode clock tick.
#include <SDL.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "client.h"
#include "config.h"
#include "library.h"
#include "model.h"
#include "retroarch.h"
#include "touch.h"
#include "ui.h"
#include "util.h"

using namespace rotate;

namespace {

struct App {
  Config cfg;
  History history{""};
  MenuModel model;
  MusicStub music;
  std::string status;
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  Ui* ui = nullptr;
  bool fullscreen = false;
  bool debug_keys = false;
  Uint32 swivel_event = 0;
  SwivelClient* client = nullptr;

  bool open_video() {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
      std::fprintf(stderr, "SDL video: %s\n", SDL_GetError());
      return false;
    }
    fullscreen = std::strcmp(SDL_GetCurrentVideoDriver(), "KMSDRM") == 0;
    win = SDL_CreateWindow("Rotate OS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kPanelPx, kPanelPx,
                           fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
    if (!win) return false;
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) return false;
    SDL_RenderSetLogicalSize(ren, kPanelPx, kPanelPx);
    ui = new Ui(ren, find_font(cfg.font_regular, "DejaVuSans.ttf"), find_font(cfg.font_bold, "DejaVuSans-Bold.ttf"));
    if (!ui->ok()) {
      std::fprintf(stderr, "font load failed\n");
      return false;
    }
    return true;
  }
  void close_video() {
    delete ui;
    ui = nullptr;
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    ren = nullptr;
    win = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);  // releases DRM master so RetroArch can take it
  }

  void reload_library() {
    auto lib = scan_library(cfg);
    history.load();
    auto recent = recent_games(history, lib);
    model.set_library(std::move(lib), std::move(recent));
  }

  void draw() {
    Scene s;
    s.model = &model;
    s.stats = read_stats(cfg.sysfs_root);
    s.music = music;
    std::time_t t = std::time(nullptr);
    localtime_r(&t, &s.now);
    s.touch_rotation = cfg.touch_rotation;
    s.status = status;
    s.roms_root = cfg.roms_root;
    ui->render(s);
    SDL_RenderPresent(ren);
  }

  void launch(const Game& g) {
    const SystemDef* sys = cfg.find_system(g.system);
    if (!sys) return;
    LaunchSpec spec = build_launch(cfg, *sys, g);
    if (!prepare_launch(spec)) {
      status = "Cannot write " + spec.append_config_path;
      return;
    }
    close_video();
    pid_t pid = spawn(spec);
    int code = -1;
    if (pid > 0) {
      if (client) client->send("GAME " + std::to_string(pid));
      code = wait_child(pid);
      if (client) client->send("MENU");
    }
    if (pid > 0 && code != 127) {
      history.record(g.system, g.path, static_cast<long long>(std::time(nullptr)));
      history.save();
      status.clear();
    } else {
      status = "Could not start " + cfg.retroarch_bin;
    }
    if (!open_video()) std::exit(1);
    if (client) client->send("GET");  // resync touch state after the game
    reload_library();
  }

  // returns false to quit
  bool handle(const SDL_Event& e, bool& dirty) {
    Outcome out;
    bool nav = false;
    Nav n = Nav::Up;
    auto key = [&](Nav v) { n = v; nav = true; };
    switch (e.type) {
      case SDL_QUIT: return false;
      case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_EXPOSED) dirty = true;
        break;
      case SDL_CONTROLLERDEVICEADDED: SDL_GameControllerOpen(e.cdevice.which); break;
      case SDL_CONTROLLERBUTTONDOWN:
        switch (e.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_DPAD_UP: key(Nav::Up); break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN: key(Nav::Down); break;
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
          case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: key(Nav::PageUp); break;
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
          case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: key(Nav::PageDown); break;
          case SDL_CONTROLLER_BUTTON_A: key(Nav::Accept); break;
          case SDL_CONTROLLER_BUTTON_B: key(Nav::Back); break;
          case SDL_CONTROLLER_BUTTON_Y:
          case SDL_CONTROLLER_BUTTON_START: key(Nav::Power); break;
        }
        break;
      case SDL_KEYDOWN:  // gpio-keys gamepads can also show up as a keyboard
        switch (e.key.keysym.sym) {
          case SDLK_UP: key(Nav::Up); break;
          case SDLK_DOWN: key(Nav::Down); break;
          case SDLK_LEFT: case SDLK_PAGEUP: key(Nav::PageUp); break;
          case SDLK_RIGHT: case SDLK_PAGEDOWN: key(Nav::PageDown); break;
          case SDLK_RETURN: case SDLK_SPACE: key(Nav::Accept); break;
          case SDLK_ESCAPE: case SDLK_BACKSPACE: key(Nav::Back); break;
          case SDLK_TAB: key(Nav::Power); break;
          case SDLK_t:
            if (debug_keys) {  // desktop testing without the daemon
              if (model.screen() == Screen::Touch) model.leave_touch(); else model.enter_touch();
              dirty = true;
            }
            break;
        }
        break;
      case SDL_FINGERDOWN:
        if (model.screen() == Screen::Touch) {
          int ux, uy;
          unrotate(cfg.touch_rotation, static_cast<int>(e.tfinger.x * kPanelPx), static_cast<int>(e.tfinger.y * kPanelPx), &ux, &uy);
          TouchHit h = touch_hit(ux, uy);
          if (h != TouchHit::None) {
            music.apply(h);
            dirty = true;
          }
        }
        break;
      default:
        if (swivel_event && e.type == swivel_event) {
          std::string line(static_cast<char*>(e.user.data1));
          if (line == "TOUCH on") model.enter_touch();
          else if (line == "TOUCH off") model.leave_touch();
          dirty = true;
        }
    }
    if (nav) {
      out = model.input(n);
      dirty = true;
      status.clear();
    }
    if (out.kind == Outcome::SetProfile) {
      auto r = apply_profile(cfg, out.profile);
      status = r.ok ? std::string("Profile: ") + profile_label(out.profile) : "Power helper failed";
    } else if (out.kind == Outcome::Launch) {
      launch(out.game);
      dirty = true;
    }
    return true;
  }
};

// SDL_WaitEvent/SDL_WaitEventTimeout fall back to a 1 ms SDL_Delay polling loop on the dummy and
// KMSDRM video drivers (no native wait hook), ~1000 wakeups/s. Poll every kIdlePollMs instead: input
// latency <= 20 ms, ~50 wakeups/s, and no redraw unless something changed. timeout_ms < 0 waits forever.
int wait_event(SDL_Event* e, int timeout_ms) {
  constexpr int kIdlePollMs = 20;
  Uint32 start = SDL_GetTicks();
  for (;;) {
    if (SDL_PollEvent(e)) return 1;
    int left = timeout_ms < 0 ? kIdlePollMs : timeout_ms - static_cast<int>(SDL_GetTicks() - start);
    if (left <= 0) return 0;
    SDL_Delay(static_cast<Uint32>(left < kIdlePollMs ? left : kIdlePollMs));
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string cfg_path = "/etc/rotate/frontend.ini";
  long exit_after_ms = -1;
  bool no_daemon = false;
  App app;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--config") && i + 1 < argc) cfg_path = argv[++i];
    else if (!std::strcmp(argv[i], "--exit-after-ms") && i + 1 < argc) exit_after_ms = std::atol(argv[++i]);
    else if (!std::strcmp(argv[i], "--no-daemon")) no_daemon = true;
    else {
      std::fprintf(stderr, "usage: rotate-frontend [--config FILE] [--exit-after-ms N] [--no-daemon]\n");
      return 2;
    }
  }
  std::string err;
  if (!Config::load(cfg_path, app.cfg, &err)) {
    std::fprintf(stderr, "rotate-frontend: %s\n", err.c_str());
    return 1;
  }
  app.debug_keys = std::getenv("ROTATE_DEBUG_KEYS") != nullptr;
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");  // touch is handled as fingers only
  if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  app.swivel_event = SDL_RegisterEvents(1);
  SwivelClient client(app.cfg.socket_path, [&app](const std::string& line) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = app.swivel_event;
    ev.user.data1 = SDL_strdup(line.c_str());
    SDL_PushEvent(&ev);
  });
  if (!no_daemon) {
    app.client = &client;
    client.start();
  }
  app.history = History(app.cfg.state_dir + "/history.tsv");
  app.reload_library();
  Profile p = current_profile(app.cfg);
  app.model.set_profile(p);
  if (!app.open_video()) {
    client.stop();
    return 1;
  }

  Uint32 t_start = SDL_GetTicks();
  bool dirty = true, run = true;
  while (run) {
    if (dirty) {
      app.draw();
      dirty = false;
    }
    int timeout = -1;
    if (app.model.screen() == Screen::Touch) {  // clock + stats tick, nothing else polls
      std::time_t now = std::time(nullptr);
      timeout = static_cast<int>(std::min<long>(5000, (60 - now % 60) * 1000L + 50));
    }
    if (exit_after_ms >= 0) {
      long left = exit_after_ms - static_cast<long>(SDL_GetTicks() - t_start);
      if (left <= 0) break;
      timeout = timeout < 0 ? static_cast<int>(left) : static_cast<int>(std::min<long>(timeout, left));
    }
    SDL_Event e;
    int got = wait_event(&e, timeout);
    if (!got) {
      if (app.model.screen() == Screen::Touch) dirty = true;
      continue;
    }
    do {
      run = app.handle(e, dirty);
      if (app.swivel_event && e.type == app.swivel_event) SDL_free(e.user.data1);
    } while (run && SDL_PollEvent(&e));
  }
  client.stop();
  app.close_video();
  SDL_Quit();
  return 0;
}
