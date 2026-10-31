#pragma once
#include <string>
#include <vector>

#include "library.h"
#include "power.h"

namespace rotate {

enum class Screen { Systems, Games, Power, Touch };
enum class Nav { Up, Down, PageUp, PageDown, Accept, Back, Power };

struct Outcome {
  enum Kind { None, Launch, SetProfile } kind = None;
  Game game;
  Profile profile = Profile::Balanced;
};

struct Row {
  std::string title;
  std::string subtitle;
};

// Menu state with no SDL dependency: systems -> games -> launch, plus power profile list.
class MenuModel {
 public:
  static constexpr int kVisibleRows = 8;

  void set_library(std::vector<SystemEntry> systems, std::vector<Game> recent);
  void set_profile(Profile p) { profile_ = p; }
  Outcome input(Nav n);
  void enter_touch();
  void leave_touch();

  Screen screen() const { return screen_; }
  Profile profile() const { return profile_; }
  std::vector<Row> rows() const;
  int selected() const;
  int first_visible() const;
  int total() const { return static_cast<int>(rows().size()); }
  std::string title() const;
  std::string hint() const;
  bool library_empty() const { return systems_.empty() && recent_.empty(); }

 private:
  bool has_recent() const { return !recent_.empty(); }
  const std::vector<Game>* current_games() const;
  int& sel();
  std::vector<SystemEntry> systems_;
  std::vector<Game> recent_;
  Profile profile_ = Profile::Balanced;
  Screen screen_ = Screen::Systems;
  Screen before_ = Screen::Systems;  // where Back / leave_touch returns to
  int sys_sel_ = 0, game_sel_ = 0, power_sel_ = 1;
  int sys_open_ = 0;  // index into the Systems rows currently opened in Games
};

}  // namespace rotate
