#pragma once
#include <cstdint>
#include <string>

namespace rotate {
// Writes an 8-bit RGB PNG (rgb = w*h*3 bytes). Uses zlib for deflate and crc32.
bool write_png_rgb(const std::string& path, int w, int h, const uint8_t* rgb);
}  // namespace rotate
