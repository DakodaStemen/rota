#include "ini.h"

#include <algorithm>
#include <sstream>

#include "util.h"

namespace rotate {

bool Ini::load(const std::string& path, Ini& out, std::string* err) {
  auto text = read_file(path);
  if (!text) {
    if (err) *err = "cannot read " + path;
    return false;
  }
  out.parse(*text);
  return true;
}

void Ini::parse(const std::string& text) {
  std::istringstream is(text);
  std::string line, section;
  while (std::getline(is, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(line.substr(1, line.size() - 2));
      if (std::find(order_.begin(), order_.end(), section) == order_.end()) order_.push_back(section);
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
    if (std::find(order_.begin(), order_.end(), section) == order_.end()) order_.push_back(section);
    data_[section][key] = val;
  }
}

std::string Ini::get(const std::string& section, const std::string& key, const std::string& def) const {
  auto s = data_.find(section);
  if (s == data_.end()) return def;
  auto k = s->second.find(key);
  return k == s->second.end() ? def : k->second;
}

bool Ini::has(const std::string& section, const std::string& key) const {
  auto s = data_.find(section);
  return s != data_.end() && s->second.count(key) > 0;
}

}  // namespace rotate
