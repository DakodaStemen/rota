#include "library.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>

#include "util.h"

namespace fs = std::filesystem;

namespace rotate {

std::string clean_title(const std::string& filename) {
  std::string s = fs::path(filename).stem().string();
  // strip every trailing "(...)" / "[...]" group
  for (;;) {
    s = trim(s);
    if (s.empty()) break;
    char close = s.back();
    char open = close == ')' ? '(' : close == ']' ? '[' : 0;
    if (!open) break;
    auto p = s.rfind(open);
    if (p == std::string::npos || p == 0) break;
    s.erase(p);
  }
  return s.empty() ? fs::path(filename).stem().string() : s;
}

static void scan_dir(const fs::path& dir, const SystemDef& sys, int depth, std::vector<Game>& out) {
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const auto& p = it->path();
    std::string fname = p.filename().string();
    if (fname.empty() || fname[0] == '.') continue;
    if (it->is_directory(ec)) {
      if (depth < 3) scan_dir(p, sys, depth + 1, out);
      continue;
    }
    std::string ext = to_lower(p.extension().string());
    if (!ext.empty()) ext.erase(0, 1);
    if (std::find(sys.exts.begin(), sys.exts.end(), ext) == sys.exts.end()) continue;
    out.push_back({sys.id, clean_title(fname), p.string()});
  }
}

std::vector<SystemEntry> scan_library(const Config& cfg) {
  std::vector<SystemEntry> lib;
  for (const auto& sys : cfg.systems) {
    SystemEntry e{sys, {}};
    scan_dir(fs::path(cfg.roms_root) / sys.id, sys, 0, e.games);
    if (e.games.empty()) continue;
    std::sort(e.games.begin(), e.games.end(), [](const Game& a, const Game& b) {
      auto la = to_lower(a.name), lb = to_lower(b.name);
      return la != lb ? la < lb : a.path < b.path;
    });
    lib.push_back(std::move(e));
  }
  std::sort(lib.begin(), lib.end(), [](const SystemEntry& a, const SystemEntry& b) {
    return to_lower(a.def.name) < to_lower(b.def.name);
  });
  return lib;
}

void History::load() {
  entries_.clear();
  auto text = read_file(path_);
  if (!text) return;
  std::istringstream is(*text);
  std::string line;
  while (std::getline(is, line)) {
    auto a = line.find('\t');
    auto b = a == std::string::npos ? a : line.find('\t', a + 1);
    if (b == std::string::npos) continue;
    HistoryEntry e;
    e.time = std::atoll(line.substr(0, a).c_str());
    e.system = line.substr(a + 1, b - a - 1);
    e.path = line.substr(b + 1);
    entries_.push_back(e);
  }
  std::stable_sort(entries_.begin(), entries_.end(),
                   [](const HistoryEntry& x, const HistoryEntry& y) { return x.time > y.time; });
  if (entries_.size() > cap_) entries_.resize(cap_);
}

bool History::save() const {
  std::ostringstream os;
  for (const auto& e : entries_) os << e.time << '\t' << e.system << '\t' << e.path << '\n';
  auto slash = path_.rfind('/');
  if (slash != std::string::npos) make_dirs(path_.substr(0, slash));
  std::string tmp = path_ + ".tmp";
  if (!write_file(tmp, os.str())) return false;
  return std::rename(tmp.c_str(), path_.c_str()) == 0;
}

void History::record(const std::string& system, const std::string& rom_path, long long when) {
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const HistoryEntry& e) { return e.path == rom_path; }),
                 entries_.end());
  entries_.insert(entries_.begin(), HistoryEntry{system, rom_path, when});
  if (entries_.size() > cap_) entries_.resize(cap_);
}

std::vector<Game> recent_games(const History& h, const std::vector<SystemEntry>& library) {
  std::vector<Game> out;
  for (const auto& e : h.entries()) {
    for (const auto& s : library) {
      if (s.def.id != e.system) continue;
      auto it = std::find_if(s.games.begin(), s.games.end(), [&](const Game& g) { return g.path == e.path; });
      if (it != s.games.end()) out.push_back(*it);
    }
  }
  return out;
}

}  // namespace rotate
