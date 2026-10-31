// Config, library scan, history, menu model, touch geometry, sysfs readers.
#include "check.h"
#include "config.h"
#include "library.h"
#include "model.h"
#include "sysinfo.h"
#include "touch.h"

using namespace rotate;

static Config demo_cfg(const std::string& roms) {
  Ini ini;
  ini.parse("[paths]\nroms_root = " + roms + "\n[retroarch]\ncores_dir = /cores\n"
            "[swivel]\ntouch_rotation = -90\n"
            "[system.gba]\nname = Game Boy Advance\ncore = mgba\nextensions = gba .ZIP\n"
            "[system.psx]\nname = PlayStation\ncore = /abs/pcsx.so\nextensions = cue\n"
            "[system.broken]\nname = No core\n");
  return Config::from_ini(ini);
}

static void test_ini_config() {
  Ini ini;
  ini.parse("# c\n; c\n[a]\nk = v\nq = \"quoted value\"\n\n[b]\nx=1\n");
  CHECK_EQ(ini.get("a", "k"), std::string("v"));
  CHECK_EQ(ini.get("a", "q"), std::string("quoted value"));
  CHECK_EQ(ini.get("b", "x"), std::string("1"));
  CHECK_EQ(ini.get("b", "missing", "dflt"), std::string("dflt"));
  CHECK(ini.has("a", "k") && !ini.has("a", "x"));

  Config c = demo_cfg("/r");
  CHECK_EQ(c.systems.size(), size_t(2));  // 'broken' has no core and is dropped
  CHECK_EQ(c.systems[0].exts.size(), size_t(2));
  CHECK_EQ(c.systems[0].exts[1], std::string("zip"));  // lowercased, dot stripped
  CHECK_EQ(c.core_path(c.systems[0]), std::string("/cores/mgba_libretro.so"));
  CHECK_EQ(c.core_path(c.systems[1]), std::string("/abs/pcsx.so"));
  CHECK_EQ(c.touch_rotation, 270);  // -90 normalised
  CHECK_EQ(c.cmd_port, 55355);
  CHECK_EQ(c.closed_value, 1);

  Config shipped;
  std::string err;
  CHECK(Config::load(std::string(ROTATE_SCRIPTS_DIR) + "/../config/frontend.ini", shipped, &err));
  CHECK(shipped.systems.size() >= 6);
  CHECK(shipped.find_system("psx") != nullptr);
  CHECK(!Config::load("/nonexistent.ini", shipped, &err));
}

static void test_scan_history() {
  CHECK_EQ(clean_title("Sonic The Hedgehog (USA, Europe) [!].md"), std::string("Sonic The Hedgehog"));
  CHECK_EQ(clean_title("Plain.gba"), std::string("Plain"));
  CHECK_EQ(clean_title("(Weird).gba"), std::string("(Weird)"));

  TempDir t;
  t.put("gba/zelda (USA).gba", "x");
  t.put("gba/Advance Wars.GBA", "x");
  t.put("gba/notes.txt", "x");
  t.put("gba/.hidden.gba", "x");
  t.put("gba/sub/deep/Metroid.zip", "x");
  t.put("psx/Crash.cue", "x");
  t.put("empty/none", "x");
  t.put("unknown/x.gba", "x");
  Config c = demo_cfg(t.path);
  auto lib = scan_library(c);
  CHECK_EQ(lib.size(), size_t(2));
  CHECK_EQ(lib[0].def.id, std::string("gba"));  // sorted by display name: Game Boy Advance < PlayStation
  CHECK_EQ(lib[0].games.size(), size_t(3));
  CHECK_EQ(lib[0].games[0].name, std::string("Advance Wars"));  // case-insensitive sort
  CHECK_EQ(lib[0].games[1].name, std::string("Metroid"));
  CHECK_EQ(lib[0].games[2].name, std::string("zelda"));
  CHECK_EQ(lib[1].games.size(), size_t(1));

  History h(t / "state/history.tsv");
  h.record("gba", lib[0].games[0].path, 100);
  h.record("psx", lib[1].games[0].path, 200);
  h.record("gba", lib[0].games[1].path, 300);
  h.record("gba", lib[0].games[0].path, 400);          // moves to top, no duplicate
  h.record("gba", t / "gba/deleted.gba", 500);          // file gone, must be filtered later
  CHECK_EQ(h.entries().size(), size_t(4));
  CHECK(h.save());
  History h2(t / "state/history.tsv");
  h2.load();
  CHECK_EQ(h2.entries().size(), size_t(4));
  CHECK_EQ(h2.entries()[0].time, 500LL);
  auto rec = recent_games(h2, lib);
  CHECK_EQ(rec.size(), size_t(3));
  CHECK_EQ(rec[0].name, std::string("Advance Wars"));  // newest existing first
  CHECK_EQ(rec[1].name, std::string("Metroid"));
  CHECK_EQ(rec[2].name, std::string("Crash"));
  History cap(t / "cap.tsv", 3);
  for (int i = 0; i < 10; ++i) cap.record("gba", "/p" + std::to_string(i), i);
  CHECK_EQ(cap.entries().size(), size_t(3));
}

static std::vector<SystemEntry> lib3() {
  std::vector<SystemEntry> l;
  for (const char* id : {"gba", "nds", "psx"}) {
    SystemEntry e;
    e.def = {id, id, "c", {"rom"}};
    for (int i = 0; i < 12; ++i) e.games.push_back({id, std::string(id) + "-game" + std::to_string(i), "/roms/x"});
    l.push_back(e);
  }
  return l;
}

static void test_model() {
  MenuModel m;
  m.set_library(lib3(), {});
  CHECK(m.library_empty() == false);
  auto rows = m.rows();
  CHECK_EQ(rows.size(), size_t(3));
  CHECK_EQ(rows[0].title, std::string("gba"));

  // recent on top
  std::vector<Game> rec = {{"psx", "psx-game3", "/roms/x"}, {"gba", "gba-game0", "/roms/x"}};
  m.set_library(lib3(), rec);
  rows = m.rows();
  CHECK_EQ(rows.size(), size_t(4));
  CHECK_EQ(rows[0].title, std::string("Recently Played"));
  CHECK_EQ(m.selected(), 0);

  Outcome o = m.input(Nav::Accept);  // open Recently Played
  CHECK(o.kind == Outcome::None);
  CHECK(m.screen() == Screen::Games);
  CHECK_EQ(m.rows().size(), size_t(2));
  CHECK_EQ(m.rows()[0].subtitle, std::string("psx"));
  m.input(Nav::Down);
  o = m.input(Nav::Accept);
  CHECK(o.kind == Outcome::Launch);
  CHECK_EQ(o.game.name, std::string("gba-game0"));
  m.input(Nav::Back);
  CHECK(m.screen() == Screen::Systems);
  m.input(Nav::Up);  // wraps to last system
  CHECK_EQ(m.selected(), 3);
  m.input(Nav::Down);
  CHECK_EQ(m.selected(), 0);
  m.input(Nav::Down);
  m.input(Nav::Accept);  // gba
  CHECK_EQ(m.title(), std::string("gba"));
  CHECK_EQ(m.rows().size(), size_t(12));
  m.input(Nav::PageDown);
  CHECK_EQ(m.selected(), 8);
  CHECK_EQ(m.first_visible(), 4);  // 12 rows, 8 visible: window clamps
  m.input(Nav::PageDown);
  CHECK_EQ(m.selected(), 11);
  m.input(Nav::PageUp);
  CHECK_EQ(m.selected(), 3);
  CHECK_EQ(m.first_visible(), 0);

  // power menu: opens on the active profile, returns to where it came from
  m.set_profile(Profile::Max);
  m.input(Nav::Power);
  CHECK(m.screen() == Screen::Power);
  CHECK_EQ(m.selected(), 2);
  m.input(Nav::Up);
  o = m.input(Nav::Accept);
  CHECK(o.kind == Outcome::SetProfile && o.profile == Profile::Balanced);
  CHECK(m.profile() == Profile::Balanced);
  m.input(Nav::Back);
  CHECK(m.screen() == Screen::Games);
  CHECK_EQ(m.selected(), 3);  // game selection preserved

  // touch mode overlays and restores; menu input is ignored meanwhile
  m.enter_touch();
  CHECK(m.screen() == Screen::Touch);
  CHECK(m.input(Nav::Accept).kind == Outcome::None);
  CHECK(m.screen() == Screen::Touch);
  m.enter_touch();  // idempotent
  m.leave_touch();
  CHECK(m.screen() == Screen::Games);
  m.input(Nav::Power);
  m.enter_touch();
  m.leave_touch();
  CHECK(m.screen() == Screen::Power);

  MenuModel e;
  CHECK(e.library_empty());
  CHECK(e.input(Nav::Accept).kind == Outcome::None);
  CHECK(e.input(Nav::Down).kind == Outcome::None);
}

static void test_touch() {
  // forward transform (what SDL_RenderCopyEx does for angle CW on a square): ui -> physical
  auto fwd = [](int rot, int ux, int uy, int* px, int* py) {
    const int n = kPanelPx;
    switch (rot) {
      case 90: *px = n - 1 - uy; *py = ux; break;
      case 180: *px = n - 1 - ux; *py = n - 1 - uy; break;
      case 270: *px = uy; *py = n - 1 - ux; break;
      default: *px = ux; *py = uy;
    }
  };
  for (int rot : {0, 90, 180, 270}) {
    for (auto pt : {std::pair<int, int>{0, 0}, {719, 0}, {100, 600}, {360, 360}, {600, 40}}) {
      int px, py, ux, uy;
      fwd(rot, pt.first, pt.second, &px, &py);
      unrotate(rot, px, py, &ux, &uy);
      CHECK(ux == pt.first && uy == pt.second);
    }
  }
  // a tap on the physical spot where the Play button is drawn (rot 90) hits PlayPause
  int px, py;
  fwd(90, 360, 620, &px, &py);
  int ux, uy;
  unrotate(90, px, py, &ux, &uy);
  CHECK(touch_hit(ux, uy) == TouchHit::PlayPause);
  CHECK(touch_hit(5, 5) == TouchHit::None);
  CHECK(touch_hit(150, 620) == TouchHit::Prev);
  CHECK(touch_hit(540, 620) == TouchHit::Next);

  MusicStub m;
  CHECK(!m.playing);
  m.apply(TouchHit::PlayPause);
  CHECK(m.playing);
  m.apply(TouchHit::Next);
  CHECK_EQ(m.track, 1);
  m.apply(TouchHit::Prev);
  m.apply(TouchHit::Prev);
  CHECK_EQ(m.track, 2);  // wraps
  m.apply(TouchHit::None);
  CHECK_EQ(m.track, 2);
}

static void test_sysinfo() {
  TempDir t;
  t.put("class/power_supply/ac/type", "Mains\n");
  t.put("class/power_supply/ac/capacity", "100\n");
  t.put("class/power_supply/sc27xx-fgu/type", "Battery\n");
  t.put("class/power_supply/sc27xx-fgu/capacity", "73\n");
  t.put("devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "1612800\n");
  t.put("devices/system/cpu/cpu0/cpufreq/scaling_governor", "schedutil\n");
  t.put("class/thermal/thermal_zone0/temp", "38500\n");
  t.put("class/thermal/thermal_zone1/temp", "44250\n");
  Stats s = read_stats(t.path);
  CHECK_EQ(s.battery_pct, 73);  // skips the Mains supply
  CHECK_EQ(s.cpu_khz, 1612800L);
  CHECK_EQ(s.temp_mc, 44250);
  CHECK_EQ(s.governor, std::string("schedutil"));
  Stats none = read_stats(t / "nowhere");
  CHECK_EQ(none.battery_pct, -1);
  CHECK_EQ(none.cpu_khz, -1L);
  CHECK(none.temp_mc == INT_MIN);

  t.put("class/backlight/panel0/brightness", "180\n");
  Backlight b(t.path);
  CHECK(b.off());
  CHECK_EQ(trim(t.get("class/backlight/panel0/brightness")), std::string("0"));
  CHECK(b.is_off());
  CHECK(b.off());  // idempotent, must not overwrite the saved value with 0
  CHECK(b.on());
  CHECK_EQ(trim(t.get("class/backlight/panel0/brightness")), std::string("180"));
  Backlight missing(t / "nope");
  CHECK(!missing.off());
}

int main() {
  test_ini_config();
  test_scan_history();
  test_model();
  test_touch();
  test_sysinfo();
  TEST_MAIN_END();
}
