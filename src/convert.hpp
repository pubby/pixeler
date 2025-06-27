#ifndef CONVERT_HPP
#define CONVERT_HPP

#include "wx/wx.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>

#include "2d/geometry.hpp"

#include "guard.hpp"
#include "graphics.hpp"
#include "nes_colors.hpp"

using namespace i2d;

std::vector<std::uint8_t> read_binary_file(char const* filename);
std::vector<std::uint8_t> read_binary_file(FILE* fp);
wxImage png_to_image(std::uint8_t const* png, std::size_t size);

#endif
