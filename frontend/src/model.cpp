#include "model.h"

#include <algorithm>

namespace rotate {

static const char* kProfileDesc[3] = {
    "Powersave governor, capped clocks. Longest battery.",
    "Default governor and clocks.",
    "Performance governor, no cap. Shortest battery.",
};

void MenuModel::set_library(std::vector<SystemEntry> systems, std::vector<Game> recent) {
  systems_ = std::move(systems);
  recent_ = std::move(recent);
  sys_sel_ = std::min(sys_sel_, std::max(0, total() - 1));
  if (screen_ == Screen::Games) screen_ = Screen::Systems;
}

const std::vector<Game>* MenuModel::current_games() const {
  if (has_recent() && sys_open_ == 0) return &recent_;
  int idx = sys_open_ - (has_recent() ? 1 : 0);
  if (idx < 0 || idx >= static_cast<int>(systems_.size())) return nullptr;
  return &systems_[idx].games;
}

std::vector<Row> MenuModel::rows() const {
  std::vector<Row> r;
  switch (screen_) {
    case Screen::Systems:
      if (has_recent()) r.push_back({"Recently Played", std::to_string(recent_.size()) + " games"});
      for (const auto& s : systems_) r.push_back({s.def.name, std::to_string(s.games.size()) + " games"});
      break;
    case Screen::Games: {
      bool recent = has_recent() && sys_open_ == 0;
      if (auto* g = current_games()) {
        for (const auto& game : *g) {
          std::string sub;
          if (recent) {
            for (const auto& s : systems_)
              if (s.def.id == game.system) sub = s.def.name;
          }
          r.push_back({game.name, sub});
        }
      }
      break;
    }
    case Screen::Power:
      for (int i = 0; i < 3; ++i) r.push_back({profile_label(static_cast<Profile>(i)), kProfileDesc[i]});
      break;
    case Screen::Touch: break;
  }
  return r;
}

int& MenuModel::sel() {
  switch (screen_) {
    case Screen::Games: return game_sel_;
    case Screen::Power: return power_sel_;
    default: return sys_sel_;
  }
}

int MenuModel::selected() const { return const_cast<MenuModel*>(this)->sel(); }

int MenuModel::first_visible() const {
  int n = total(), s = selected();
  int first = s - kVisibleRows / 2;
  return std::max(0, std::min(first, std::max(0, n - kVisibleRows)));
}

std::string MenuModel::title() const {
  switch (screen_) {
    case Screen::Systems: return "Games";
    case Screen::Games:
      if (has_recent() && sys_open_ == 0) return "Recently Played";
      return systems_.at(sys_open_ - (has_recent() ? 1 : 0)).def.name;
    case Screen::Power: return "Power Profile";
    case Screen::Touch: return "Touch Mode";
  }
  return "";
}

std::string MenuModel::hint() const {
  switch (screen_) {
    case Screen::Systems: return "A Open    Y Power Profile    D-pad Move";
    case Screen::Games: return "A Play    B Back    Y Power Profile";
    case Screen::Power: return "A Apply    B Back";
    case Screen::Touch: return "";
  }
  return "";
}

Outcome MenuModel::input(Nav n) {
  Outcome out;
  if (screen_ == Screen::Touch) return out;
  int count = total();
  int& s = sel();
  switch (n) {
    case Nav::Up: if (count) s = (s + count - 1) % count; break;
    case Nav::Down: if (count) s = (s + 1) % count; break;
    case Nav::PageUp: s = std::max(0, s - kVisibleRows); break;
    case Nav::PageDown: s = std::max(0, std::min(count - 1, s + kVisibleRows)); break;
    case Nav::Power:
      if (screen_ != Screen::Power) {
        before_ = screen_;
        screen_ = Screen::Power;
        power_sel_ = static_cast<int>(profile_);
      }
      break;
    case Nav::Back:
      if (screen_ == Screen::Games) screen_ = Screen::Systems;
      else if (screen_ == Screen::Power) screen_ = before_;
      break;
    case Nav::Accept:
      if (screen_ == Screen::Systems && count) {
        sys_open_ = sys_sel_;
        game_sel_ = 0;
        screen_ = Screen::Games;
      } else if (screen_ == Screen::Games && count) {
        out.kind = Outcome::Launch;
        out.game = (*current_games())[game_sel_];
      } else if (screen_ == Screen::Power) {
        profile_ = static_cast<Profile>(power_sel_);
        out.kind = Outcome::SetProfile;
        out.profile = profile_;
      }
      break;
  }
  return out;
}

void MenuModel::enter_touch() {
  if (screen_ == Screen::Touch) return;
  before_ = screen_;
  screen_ = Screen::Touch;
}

void MenuModel::leave_touch() {
  if (screen_ == Screen::Touch) screen_ = before_;
}

}  // namespace rotate
