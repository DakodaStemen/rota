#include "png.h"

#include <zlib.h>

#include <cstdio>
#include <vector>

namespace rotate {

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
}

static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> hdr;
  put32(hdr, static_cast<uint32_t>(data.size()));
  std::fwrite(hdr.data(), 1, 4, f);
  std::fwrite(type, 1, 4, f);
  uLong crc = crc32(0, reinterpret_cast<const Bytef*>(type), 4);
  if (!data.empty()) {
    std::fwrite(data.data(), 1, data.size(), f);
    crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
  }
  std::vector<uint8_t> tail;
  put32(tail, static_cast<uint32_t>(crc));
  std::fwrite(tail.data(), 1, 4, f);
}

bool write_png_rgb(const std::string& path, int w, int h, const uint8_t* rgb) {
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(h) * (static_cast<size_t>(w) * 3 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);  // filter: none
    raw.insert(raw.end(), rgb + static_cast<size_t>(y) * w * 3, rgb + static_cast<size_t>(y + 1) * w * 3);
  }
  uLongf cap = compressBound(static_cast<uLong>(raw.size()));
  std::vector<uint8_t> z(cap);
  if (compress2(z.data(), &cap, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) return false;
  z.resize(cap);
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  std::fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  put32(ihdr, static_cast<uint32_t>(w));
  put32(ihdr, static_cast<uint32_t>(h));
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit, truecolor
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  return std::fclose(f) == 0;
}

}  // namespace rotate
