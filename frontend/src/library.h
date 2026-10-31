#pragma once
#include <ctime>
#include <string>
#include <vector>

#include "config.h"

namespace rotate {

struct Game {
  std::string system;  // SystemDef::id
  std::string name;    // display title
  std::string path;
};

struct SystemEntry {
  SystemDef def;
  std::vector<Game> games;
};

// "Sonic (USA) [!].md" -> "Sonic"
std::string clean_title(const std::string& filename);
// Scans <roms_root>/<system.id> recursively (depth <= 3); skips dotfiles. Only systems with games are returned.
std::vector<SystemEntry> scan_library(const Config& cfg);

struct HistoryEntry {
  std::string system;
  std::string path;
  long long time = 0;
};

// Tab-separated file: time \t system \t path, newest first, capped.
class History {
 public:
  explicit History(std::string path, size_t cap = 20) : path_(std::move(path)), cap_(cap) {}
  void load();
  bool save() const;
  void record(const std::string& system, const std::string& rom_path, long long when);
  const std::vector<HistoryEntry>& entries() const { return entries_; }

 private:
  std::string path_;
  size_t cap_;
  std::vector<HistoryEntry> entries_;
};

// Resolves history to games that still exist in the scanned library, newest first.
std::vector<Game> recent_games(const History& h, const std::vector<SystemEntry>& library);

}  // namespace rotate
