#include "convert.hpp"

#include <cstring>

#include "lodepng/lodepng.h"

std::vector<std::uint8_t> read_binary_file(char const* filename)
{
    FILE* fp = std::fopen(filename, "rb");
    auto scope_guard = make_scope_guard([&]{ std::fclose(fp); });
    return read_binary_file(fp);
}

std::vector<std::uint8_t> read_binary_file(FILE* fp)
{
    if(!fp)
        return {};

    // Get the file size
    std::fseek(fp, 0, SEEK_END);
    std::size_t const file_size = ftell(fp);
    std::fseek(fp, 0, SEEK_SET);

    std::vector<std::uint8_t> data(file_size);

    if(data.empty() || std::fread(data.data(), file_size, 1, fp) != 1)
        return {};

    return data;
}

/*
std::pair<std::vector<bitmap_t>, std::vector<wxBitmap>> load_collision_file(wxString const& string)
{
    if(string.IsEmpty())
        return {};

    std::pair<std::vector<bitmap_t>, std::vector<wxBitmap>> ret;

    wxLogNull go_away;
    wxImage base(string);
    if(!base.IsOk())
        return {};

    for(coord_t c : dimen_range({8, 8}))
    {
        wxImage tile = base.Copy();
        //wxImage tile(string);
        tile.Resize({ 16, 16 }, { c.x * -16, c.y * -16 }, 255, 0, 255);
#ifdef GC_RENDER
        ret.first.emplace_back(get_renderer()->CreateBitmapFromImage(tile));
#else
        ret.first.emplace_back(tile);
#endif
        ret.second.emplace_back(tile);
    }

    return ret;
}
    */

wxImage png_to_image(std::uint8_t const* png, std::size_t size)
{
    unsigned width, height;
    std::vector<unsigned char> pixels; //the raw pixels
    lodepng::State state;
    unsigned error;
    unsigned char* alloc;

    if((error = lodepng_inspect(&width, &height, &state, png, size)))
        goto fail;

    if(width % 8 != 0)
        throw std::runtime_error("Image width is not a multiple of 8.");
    else if(height % 8 != 0)
        throw std::runtime_error("Image height is not a multiple of 8.");

    state.info_raw.colortype = LCT_RGB;
    if((error = lodepng::decode(pixels, width, height, state, png, size)))
        goto fail;

    alloc = (unsigned char*)std::malloc(pixels.size());
    std::memcpy(alloc, pixels.data(), pixels.size());
    return wxImage(width, height, alloc);
fail:
    throw std::runtime_error(std::string("png decoder error: ") + lodepng_error_text(error));
}
