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

struct color_knob_t
{
    std::uint8_t color = 0xFF;
    int greed = 0;
    int bleed = 0;

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

    float greedf() const { return greed * 8.0f; }
    float bleedf() const { return std::powf(2.0, bleed); }

    auto operator<=>(color_knob_t const&) const = default;
};

struct model_t
{
    model_t();

    bool display = false;
    int w = 256;
    int h = 256;

    std::array<wxBitmap, 65> color_bitmaps; 
    std::array<color_knob_t, 25> color_knobs;

    color_knob_t u_color;
    std::vector<std::array<color_knob_t, 3>> b_colors;
    std::vector<std::array<color_knob_t, 3>> s_colors;

    wxStatusBar* status_bar = nullptr;

    std::filesystem::path base_image_path;
    wxImage base_image;
    wxBitmap base_bitmap;

    std::filesystem::path output_image_path;
    wxImage output_image;
    wxBitmap output_bitmap;

    void update();
    void update_bitmaps();

    void read_file(FILE* fp);
    void write_file(FILE* fp) const;
};

#endif
