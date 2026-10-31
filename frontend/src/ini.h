#pragma once
#include <map>
#include <string>
#include <vector>

namespace rotate {

// Minimal INI: [section], key = value, full-line comments with # or ;.
// Values may be wrapped in double quotes. No inline comments, no escapes.
class Ini {
 public:
  static bool load(const std::string& path, Ini& out, std::string* err = nullptr);
  void parse(const std::string& text);
  std::string get(const std::string& section, const std::string& key, const std::string& def = "") const;
  bool has(const std::string& section, const std::string& key) const;
  const std::vector<std::string>& sections() const { return order_; }

 private:
  std::vector<std::string> order_;
  std::map<std::string, std::map<std::string, std::string>> data_;
};

}  // namespace rotate
