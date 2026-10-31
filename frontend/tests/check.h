#pragma once
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "util.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                  \
  do {                                                                            \
    ++g_checks;                                                                   \
    if (!(c)) { ++g_fail; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
  } while (0)
#define CHECK_EQ(a, b)                                                            \
  do {                                                                            \
    ++g_checks;                                                                   \
    auto va_ = (a); auto vb_ = (b);                                               \
    if (!(va_ == vb_)) {                                                          \
      ++g_fail;                                                                   \
      std::ostringstream o_; o_ << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #a " == " #b "  (" << va_ << " vs " << vb_ << ")\n"; \
      std::fputs(o_.str().c_str(), stderr);                                       \
    }                                                                             \
  } while (0)
#define TEST_MAIN_END() \
  do { std::printf("%s: %d checks, %d failed\n", __FILE__, g_checks, g_fail); return g_fail ? 1 : 0; } while (0)

struct TempDir {
  std::string path;
  TempDir() {
    std::string base = rotate::env_or("TMPDIR", "/tmp") + "/rotate-test-XXXXXX";
    std::string t = base;
    path = mkdtemp(&t[0]);
  }
  ~TempDir() { std::string c = "rm -rf '" + path + "'"; (void)!std::system(c.c_str()); }
  std::string operator/(const std::string& s) const { return path + "/" + s; }
  void put(const std::string& rel, const std::string& data) const {
    std::string full = path + "/" + rel;
    rotate::make_dirs(full.substr(0, full.rfind('/')));
    rotate::write_file(full, data);
  }
  std::string get(const std::string& rel) const { return rotate::read_file(path + "/" + rel).value_or("<missing>"); }
};
