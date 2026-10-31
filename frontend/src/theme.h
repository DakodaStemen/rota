#pragma once
#include <SDL.h>

namespace rotate::theme {
// One palette. Contrast against kBg: kText 15.7:1, kDim 8.4:1 (7.3:1 on kPanel), kBg on kAccent 10.1:1 (WCAG AA needs 4.5:1).
constexpr SDL_Color kBg{0x10, 0x14, 0x18, 255};
constexpr SDL_Color kPanel{0x1b, 0x22, 0x2b, 255};
constexpr SDL_Color kText{0xe8, 0xed, 0xf2, 255};
constexpr SDL_Color kDim{0xa5, 0xb0, 0xbc, 255};
constexpr SDL_Color kAccent{0xff, 0xb0, 0x00, 255};
}  // namespace rotate::theme
