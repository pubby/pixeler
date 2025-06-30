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

wxImage png_to_image(std::uint8_t const* png, std::size_t size)
{
    unsigned width, height;
    std::vector<unsigned char> pixels; //the raw pixels
    lodepng::State state;
    unsigned error;
    unsigned char* alloc;

    if((error = lodepng_inspect(&width, &height, &state, png, size)))
        goto fail;

    state.info_raw.colortype = LCT_RGB;
    if((error = lodepng::decode(pixels, width, height, state, png, size)))
        goto fail;

    alloc = (unsigned char*)std::malloc(pixels.size());
    std::memcpy(alloc, pixels.data(), pixels.size());
    return wxImage(width, height, alloc);
fail:
    throw std::runtime_error(std::string("png decoder error: ") + lodepng_error_text(error));
}
