#include "model.hpp"

#include "convert.hpp"

#include "flat/flat_map.hpp"
#include "flat/flat_set.hpp"

model_t::model_t()
{
    constexpr unsigned W = 16;
    constexpr unsigned H = 16;
    
    // Color bitmaps:
    for(unsigned i = 0; i < 64; i += 1)
    {
        unsigned char* alloc = (unsigned char*)std::malloc(W*H*3);
        for(unsigned j = 0; j < W*H; j += 1)
        {
            alloc[j*3 + 0] = nes_colors[i].r;
            alloc[j*3 + 1] = nes_colors[i].g;
            alloc[j*3 + 2] = nes_colors[i].b;
        }
        
        wxImage image(W, H, alloc);
        color_bitmaps[i] = wxBitmap(image);
    }

    unsigned char* alloc = (unsigned char*)std::calloc(W*H*3, 1);
    for(unsigned y = 0; y < H; y += 1)
    for(unsigned x = 0; x < W; x += 1)
        if(x == y || (W-x) == y)
            alloc[(x+y*W)*3] = 0xFF;
    wxImage image(W, H, alloc);
    color_bitmaps[64] = wxBitmap(image);
}

void model_t::update()
{
    // Scale the base image:
    wxImage scaled = base_image;
    if(scaled.IsOk())
    {
        scaled.Rescale(w, h, wxIMAGE_QUALITY_BOX_AVERAGE);
        base_bitmap = wxBitmap(scaled);

        output_image = scaled;
        if(!output_image.IsOk())
        {
            std::fprintf(stderr, "Bad output image\n");
            return;
        }
    }
    else
        return;

    // Temporary output:
    if(output_image.IsOk())
        output_bitmap = wxBitmap(output_image);

    // Calculate unique color sets:

    fc::vector_map<fc::vector_set<color_knob_t>, unsigned> map;

    for(unsigned s = 0; s < 4; s += 1)
    {
        for(unsigned b = 0; b < 4; b += 1)
        {
            fc::vector_set<color_knob_t> set;

            auto const& add_color = [&](color_knob_t const& knob)
            {
                if(knob.color < 64)
                    set.insert(knob);
            };

            add_color(color_knobs[24]);
            unsigned const pre_size = set.size();

            for(unsigned i = 0; i < 3; i += 1)
            {
                add_color(color_knobs[ 0 + b*3 + i]);
                add_color(color_knobs[12 + s*3 + i]);
            }

            unsigned const post_size = set.size();

            if(!set.empty() && pre_size != post_size)
                map.emplace(std::move(set), b + s*4);
        }
    }

    for(auto it = map.begin(); it != map.end();)
    {
        for(auto jt = map.begin(); jt != map.end(); jt += 1)
        {
            if(it == jt)
                continue;

            for(color_knob_t const& knob : it->first)
            {
                if(jt->first.count(knob) == 0)
                    goto next_iter;
            }

            it = map.erase(it);
            goto erased;
        }

    next_iter:
        ++it;
    erased:;
    }

    if(map.empty())
    {
        std::fprintf(stderr, "Empty map.\n");
        return;
    }

    // To downscale the source image, we'll compare pixels from regions:

    unsigned bw = base_image.GetWidth();  // base width
    unsigned bh = base_image.GetHeight(); // base height
    unsigned rw = bw / w; // region width
    unsigned rh = bh / h; // region height

    if(rw == 0 || rh == 0) // TODO: handle source image smaller than destination
    {
        std::fprintf(stderr, "Zero region %i %i %i %i.\n", bw, bh, w, h);
        return;
    }

    // Then identify the best color set for each 8x8 region:

    auto const pixel_score = [](std::uint8_t color, rgb_t rgb) -> float
    {
        rgb_t const nes = nes_colors[color];
        unsigned score = 0;
        score += (nes.r - rgb.r) * (nes.r - rgb.r);
        score += (nes.g - rgb.g) * (nes.g - rgb.g);
        score += (nes.b - rgb.b) * (nes.b - rgb.b);
        int hue_diff = hue(nes) - hue(rgb);
        score += std::abs(hue_diff);
        return std::sqrt(float(score));
    };

    std::vector<float> scores(map.size());
    std::vector<float> color_scores;
    std::vector<float> region_scores;
    std::vector<float> attr_scores;
    std::vector<std::array<std::uint8_t, 64>> chosen_pixels(map.size());
    unsigned char const* const src_ptr = base_image.GetData();
    unsigned char const* const scaled_ptr = scaled.GetData();
    unsigned char* const dst_ptr = output_image.GetData();

    auto const get_src = [&](unsigned x, unsigned y) -> rgb_t
    {
        unsigned i = (x+y*bw)*3;
        return rgb_t{ src_ptr[i+0], src_ptr[i+1], src_ptr[i+2] };
    };

    auto const get_scaled = [&](unsigned x, unsigned y) -> rgb_t
    {
        unsigned i = (x+y*w)*3;
        return rgb_t{ scaled_ptr[i+0], scaled_ptr[i+1], scaled_ptr[i+2] };
    };

    auto const set_dst = [&](unsigned x, unsigned y, rgb_t color)
    {
        unsigned i = (x+y*w)*3;
        dst_ptr[i+0] = color.r;
        dst_ptr[i+1] = color.g;
        dst_ptr[i+2] = color.b;
    };

    for(unsigned iy = 0; iy < h; iy += 8)
    for(unsigned ix = 0; ix < w; ix += 8)
    {
        attr_scores.clear();
        attr_scores.resize(map.size());

        for(auto const& pair : map)
        {
            unsigned const map_i = &pair - &map.container[0];

            for(unsigned py = iy; py < std::min<unsigned>(iy+8, h); py += 1)
            for(unsigned px = ix; px < std::min<unsigned>(ix+8, w); px += 1)
            {
                region_scores.clear();
                region_scores.resize(pair.first.size());

                for(unsigned sy = py * rh; sy < std::min<unsigned>(py * rh + rh, bh); sy += 1)
                for(unsigned sx = px * rw; sx < std::min<unsigned>(px * rw + rw, bw); sx += 1)
                {
                    color_scores.clear();
                    for(color_knob_t const& knob : pair.first)
                        color_scores.push_back(pixel_score(knob.color, get_src(sx, sy)) - knob.greedf());

                    // OK! Found the best color for this src pixel.
                    auto it = std::ranges::min_element(color_scores.begin(), color_scores.end());
                    unsigned const best_index = it - color_scores.begin();
                    color_knob_t const& best_knob = pair.first.container[best_index];

                    // Record the score of the src region:
                    float score = 1.0 / std::max<float>(0.125, *it);
                    region_scores[best_index] += best_knob.bleedf() * score;
                }

                // Find the best color for this src region:
                auto it = std::ranges::max_element(region_scores.begin(), region_scores.end());
                unsigned const best_index = it - region_scores.begin();
                color_knob_t const& best_knob = pair.first.container[best_index];

                // Record it:
                chosen_pixels[map_i][(px % 8) + (py % 8)*8] = best_knob.color;

                // Track it on attr_scores:
                attr_scores[map_i] += pixel_score(best_knob.color, get_scaled(px, py)) - best_knob.greedf();
            }
        }

        // Find the best attribute combo for this dst region:
        auto it = std::ranges::min_element(attr_scores.begin(), attr_scores.end());
        unsigned const best_index = it - attr_scores.begin();
        unsigned const attr_pair = map.container[best_index].second;

        // Write the pixels to the destination:
        for(unsigned py = iy; py < std::min<unsigned>(iy+8, h); py += 1)
        for(unsigned px = ix; px < std::min<unsigned>(ix+8, w); px += 1)
            set_dst(px, py, nes_colors[chosen_pixels[best_index][(px % 8) + (py % 8)*8]]);
    }

    if(output_image.IsOk())
        output_bitmap = wxBitmap(output_image);
    else
        std::fprintf(stderr, "Unable to bitmap output image.\n");
}

void model_t::read_file(FILE* fp)
{
    std::vector<std::uint8_t> bytes = read_binary_file(fp);
    base_image = png_to_image(bytes.data(), bytes.size());
    update();
}


void model_t::write_file(FILE* fp) const
{
    /*
    base_path.remove_filename();

    auto const write_str = [&](std::string const& str)
    {
        if(!str.empty())
            std::fwrite(str.c_str(), str.size(), 1, fp);
        std::fputc(0, fp);
    };

    auto const write8 = [&](std::uint8_t i)
    {
        std::fputc(i & 0xFF, fp);
    };

    auto const write16 = [&](std::uint16_t i)
    {
        std::fputc(i & 0xFF, fp); // Lo
        std::fputc((i >> 8) & 0xFF, fp); // Hi
    };

    std::fwrite("MapFab", 7, 1, fp);

    // Version:
    write8(SAVE_VERSION);

    // Collision file:
    write_str(std::filesystem::proximate(collision_path, base_path).generic_string());

    // CHR:
    write8(chr_files.size() & 0xFF);
    for(auto const& file : chr_files)
    {
        write_str(file.name);
        write_str(std::filesystem::proximate(file.path, base_path).generic_string());
    }

    // Palettes:
    write8(palette.color_layer.num & 0xFF);
    for(std::uint8_t data : palette.color_layer.tiles)
        write8(data);

    // Metatiles:
    write8(metatiles.size() & 0xFF);
    for(auto const& mt : metatiles)
    {
        write_str(mt->name.c_str());
        write_str(mt->chr_name.c_str());
        write8(mt->palette & 0xFF);
        write8(mt->num & 0xFF);
        for(std::uint8_t data : mt->chr_layer.tiles)
            write8(data);
        for(std::uint8_t data : mt->chr_layer.attributes)
            write8(data);
        for(std::uint8_t data : mt->collision_layer.tiles)
            write8(data);
    }

    // Object classes:
    write8(object_classes.size() & 0xFF);
    for(auto const& oc : object_classes)
    {
        write_str(oc->name.c_str());
        write8(oc->color.r & 0xFF);
        write8(oc->color.g & 0xFF);
        write8(oc->color.b & 0xFF);
        write8(oc->fields.size() & 0xFF);
        for(auto const& field : oc->fields)
        {
            write_str(field.name.c_str());
            write_str(field.type.c_str());
        }
    }

    // Levels:
    write8(levels.size() & 0xFF);
    for(auto const& level : levels)
    {
        write_str(level->name.c_str());
        write_str(level->macro_name.c_str());
        write_str(level->chr_name.c_str());
        write8(level->palette & 0xFF);
        write_str(level->metatiles_name.c_str());
        write8(level->dimen().w & 0xFF);
        write8(level->dimen().h & 0xFF);
        for(std::uint8_t data : level->metatile_layer.tiles)
            write8(data);
        write16(level->objects.size());
        for(auto const& obj : level->objects)
        {
            write_str(obj.name.c_str());
            write_str(obj.oclass.c_str());
            write16(obj.position.x);
            write16(obj.position.y);
            for(auto const& oc : object_classes)
            {
                if(oc->name == obj.oclass)
                {
                    for(auto const& field : oc->fields)
                    {
                        auto it = obj.fields.find(field.name);
                        if(it != obj.fields.end())
                            write_str(it->second);
                        else
                            write8(0);
                    }
                    break;
                }
            }
        }
    }
    */
}

