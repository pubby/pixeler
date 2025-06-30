#ifndef MODEL_HPP
#define MODEL_HPP

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <variant>
#include <set>
#include <filesystem>
#include <vector>
#include <charconv>
#include <cmath>

#include <wx/wx.h>

#include "nes_colors.hpp"

using color_triad_t = std::array<std::uint8_t, 3>;
using color_quad_t = std::array<std::uint8_t, 4>;

inline std::string color_string(std::uint8_t color)
{
    if(color >= 64)
        return "N/A";
    std::string result = "$00";
    snprintf(&result[1], 3, "%02X", color);
    return result;
}

constexpr unsigned MAP_SIZE = 4;

struct color_knob_t
{
    std::uint8_t nes_color = 0xFF;
    std::array<rgb_t, MAP_SIZE> map_colors = {};
    std::array<bool, MAP_SIZE> map_enable = {};
    int greed = 0;
    int bleed = 0;

    bool any_enabled() const 
    {
        for(bool b : map_enable)
            if(b)
                return true;
        return false;
    }

    bool set_greed(int v)
    {
        if(greed == v)
            return false;
        greed = v;
        return true;
    }

    bool set_bleed(int v)
    {
        if(bleed == v)
            return false;
        bleed = v;
        return true;
    }

    float greedf() const { return std::pow(1.1, -greed); }
    float bleedf() const { return std::pow(2.0, bleed); }

    auto operator<=>(color_knob_t const&) const = default;
};

enum dither_style_t
{
    DITHER_NONE,
    DITHER_WAVES,
    DITHER_FLOYD,
    DITHER_HORIZONTAL,
    DITHER_VAN_GOGH,
    DITHER_Z1,
    DITHER_CZ2,
    DITHER_BRIX,
    DITHER_CUSTOM,
    NUM_DITHER,
    LAST_DIFFUSION = DITHER_VAN_GOGH,
    FIRST_MASK = DITHER_Z1,
    NUM_MASK_DITHERS = NUM_DITHER - FIRST_MASK,
};

struct model_t
{
    model_t();

    int w = 256;
    int h = 256;

    bool display = false;
    bool cull_dots = false;
    bool cull_pipes = false;
    bool cull_zags = false;
    bool clean_lines = false;

    dither_style_t dither_style = DITHER_NONE;
    int dither_scale = 0;
    int dither_cutoff = 0;

    std::array<wxBitmap, 65> color_bitmaps = {}; 
    std::array<color_knob_t, 16> color_knobs = {};

    wxStatusBar* status_bar = nullptr;

    wxImage base_image;
    wxBitmap base_bitmap;

    wxImage picker_image;
    wxBitmap picker_bitmap;

    std::array<wxImage, NUM_MASK_DITHERS> dither_images;

    std::filesystem::path output_image_path;
    wxImage output_image;
    wxBitmap output_bitmap;

    std::string save_path;

    void update();
    void update_bitmaps();

    void auto_color(unsigned count, bool map);
};

#endif
