#include "model.hpp"

#include <wx/mstream.h>

#include "flat/flat_map.hpp"
#include "flat/flat_set.hpp"

#include "z1.png.inc"
#include "cz332.png.inc"
#include "brix.png.inc"
#include "custom.png.inc"

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

    // Read the dithers:
 
    auto const make_img = [&](char const* name, unsigned char const* data, std::size_t size) -> wxImage
    {
        wxMemoryInputStream stream(data, size);
        wxImage img;
        if(!img.LoadFile(stream, wxBITMAP_TYPE_PNG))
            throw std::runtime_error(name);
        return img;
    };

#define MAKE_IMG(x) make_img(#x, x, x##_size)
    dither_images[0] = MAKE_IMG(dither_z1_png);
    dither_images[1] = MAKE_IMG(dither_cz332_png);
    dither_images[2] = MAKE_IMG(dither_brix_png);
}

void model_t::update()
{
    if(!base_image.IsOk())
        return;

    // Create the picker image
    picker_image = base_image.Copy();
    if(picker_image.IsOk())
    {
        picker_image.Rescale(512, 512, wxIMAGE_QUALITY_NEAREST);
        picker_bitmap = wxBitmap(picker_image);
    }
    else
        std::fprintf(stderr, "Bad picker image\n");

    // Scale the base image:
    wxImage scaled = base_image.Copy();
    if(scaled.IsOk())
    {
        scaled.Rescale(w, h, wxIMAGE_QUALITY_BOX_AVERAGE);
        base_bitmap = wxBitmap(scaled);

        output_image = scaled.Copy();
        if(!output_image.IsOk())
        {
            std::fprintf(stderr, "Bad output image\n");
            return;
        }
    }
    else
    {
        std::fprintf(stderr, "Bad scaled image\n");
        return;
    }

    // Temporary output:
    if(output_image.IsOk())
        output_bitmap = wxBitmap(output_image);
    else
        std::fprintf(stderr, "Bad output bitmap\n");

    // Dither size
    auto const& dither_image = dither_images[std::max(dither_style, FIRST_MASK) - FIRST_MASK];
    unsigned const dw = dither_image.GetWidth();  // dither width
    unsigned const dh = dither_image.GetHeight(); // dither height

    // To downscale the source image, we'll compare pixels from regions:

    unsigned bw = base_image.GetWidth();  // base width
    unsigned bh = base_image.GetHeight(); // base height
    unsigned rw = std::max<unsigned>(1, bw / w); // region width
    unsigned rh = std::max<unsigned>(1, bh / h); // region height

    scaled = base_image.Copy();
    scaled.Rescale(rw * w, rh * h);

    bw = scaled.GetWidth();
    bh = scaled.GetHeight();

    // Then identify the best color set for each 8x8 region:

    std::vector<qerr_t> qerrs(w * h);
    std::vector<float> color_scores;
    std::vector<float> region_scores;
    std::vector<qerr_t> region_q;
    std::vector<int> region_q_count;
    unsigned char* const src_ptr = scaled.GetData();
    unsigned char* const dst_ptr = output_image.GetData();
    unsigned char* const dither_ptr = dither_image.GetData();
    std::vector<std::uint8_t> dst_nes(w * h);

    auto const at_dst_nes = [&](int x, int y) -> std::uint8_t&
    {
        return dst_nes[x + y*w];
    };

    float const dscale = 1.0f / std::pow(1.11f, dither_scale);
    float const iscale = (dither_cutoff + 8) / 8.0f;

    auto const get_src = [&](unsigned x, unsigned y) -> rgb_t
    {
        unsigned i = (x+y*bw)*3;
        return rgb_t{ src_ptr[i+0], src_ptr[i+1], src_ptr[i+2] };
    };

    auto const set_dst = [&](unsigned x, unsigned y, rgb_t color)
    {
        unsigned i = (x+y*w)*3;
        dst_ptr[i+0] = color.r;
        dst_ptr[i+1] = color.g;
        dst_ptr[i+2] = color.b;
    };

    auto const get_dither = [&](unsigned x, unsigned y) -> rgb_t
    {
        unsigned i = ((x % dw)+(y % dh)*dw)*3;
        return rgb_t{ dither_ptr[i+0], dither_ptr[i+1], dither_ptr[i+2] };
    };

    auto const get_dither_lerp = [&](unsigned x, unsigned y) -> rgb_t
    {
        float fx = float(x) / (iscale);
        float fy = float(y) / (iscale);

        float dx = std::fmod(fx, 1.0f);
        float dy = std::fmod(fy, 1.0f);

        x = std::floor(fx);
        y = std::floor(fy);

        rgb_t nw = get_dither(x+0, y+0);
        rgb_t ne = get_dither(x+1, y+0);
        rgb_t sw = get_dither(x+0, y+1);
        rgb_t se = get_dither(x+1, y+1);

        rgb_t n, s;

        n.r = nw.r*(1.0f - dx) + ne.r*dx;
        n.g = nw.g*(1.0f - dx) + ne.g*dx;
        n.b = nw.b*(1.0f - dx) + ne.b*dx;

        s.r = sw.r*(1.0f - dx) + se.r*dx;
        s.g = sw.g*(1.0f - dx) + se.g*dx;
        s.b = sw.b*(1.0f - dx) + se.b*dx;

        rgb_t a;
        a.r = n.r*(1.0f - dy) + s.r*dy;
        a.g = n.g*(1.0f - dy) + s.g*dy;
        a.b = n.b*(1.0f - dy) + s.b*dy;

        return a;
    };

    for(int py = 0; py < h; py += 1)
    for(int px = 0; px < w; px += 1)
    {
        region_scores.clear();
        region_scores.resize(color_knobs.size());

        region_q.clear();
        region_q.resize(color_knobs.size());

        region_q_count.clear();
        region_q_count.resize(color_knobs.size());

        for(int sy = py * rh; sy < std::min<int>(py * rh + rh, bh); sy += 1)
        for(int sx = px * rw; sx < std::min<int>(px * rw + rw, bw); sx += 1)
        {
            color_scores.clear();
            color_scores.resize(color_knobs.size() * MAP_SIZE, INFINITY);

            float score = INFINITY;
            unsigned best_knob = 0;
            qerr_t best_q = {};

            for(unsigned k = 0; k < color_knobs.size(); k += 1)
            {
                auto const& knob = color_knobs[k];

                if(knob.nes_color >= 64)
                    continue;

                for(unsigned i = 0; i < knob.map_colors.size(); i += 1)
                {
                    if(!knob.map_enable[i])
                        continue;

                    qerr_t q = qerr(knob.map_colors[i], get_src(sx, sy));

                    q.r *= knob.greedf();
                    q.g *= knob.greedf();
                    q.b *= knob.greedf();

                    if(dither_style)
                    {
                        if(dither_style <= LAST_DIFFUSION)
                        {
                            q.r += qerrs[px + py*w].r * dscale;
                            q.g += qerrs[px + py*w].g * dscale;
                            q.b += qerrs[px + py*w].b * dscale;
                        }
                        else if(dither_image.IsOk())
                        {
                            rgb_t d = get_dither_lerp(px, py);
                            float s = (40 - dither_scale) / 40.0f;
                            q.r += std::round(float(int(d.r) - 128) * s);
                            q.g += std::round(float(int(d.g) - 128) * s);
                            q.b += std::round(float(int(d.b) - 128) * s);
                        }
                    }

                    float const dist = distance(q);

                    float new_score = std::min<float>(score, dist);
                    if(new_score < score)
                    {
                        score = new_score;
                        best_knob = k;
                        best_q = q;
                    }
                }
            }

            region_scores[best_knob] += color_knobs[best_knob].bleedf() / std::max<float>(score, 1);
            region_q[best_knob].r += best_q.r;
            region_q[best_knob].g += best_q.g;
            region_q[best_knob].b += best_q.b;
            region_q_count[best_knob] += 1;
        }

        auto it = std::ranges::max_element(region_scores.begin(), region_scores.end());
        unsigned const best_index = it - region_scores.begin();
        color_knob_t const& best_knob = color_knobs[best_index];

        if(best_knob.nes_color < 64)
        {
            at_dst_nes(px, py) = best_knob.nes_color;

            if(dither_style)
            {
                // Calculate the average error:
                qerr_t q = region_q[best_index];
                q.r /= region_q_count[best_index];
                q.g /= region_q_count[best_index];
                q.b /= region_q_count[best_index];

                if(std::abs(q.r) < dither_cutoff * 8)
                    q.r = 0;
                if(std::abs(q.g) < dither_cutoff * 8)
                    q.g = 0;
                if(std::abs(q.b) < dither_cutoff * 8)
                    q.b = 0;

                auto const distribute = [&](int x, int y, float scale)
                {
                    x += px;
                    y += py;
                    if(x < 0 || x >= w || y < 0 || y >= h)
                        return;
                    qerrs[x + y*w].r += q.r * scale;
                    qerrs[x + y*w].g += q.g * scale;
                    qerrs[x + y*w].b += q.b * scale;
                };

                auto const distribute_chunky = [&](int x, int y, float scale)
                {
                    for(int i = 0; i < 2; i += 1)
                    for(int j = 0; j < 2; j += 1)
                        distribute(x*2 + i, y*2 + j, scale * 0.25);
                };

                // Diffuse the error:
                switch(dither_style)
                {
                default:
                    break;
                case DITHER_WAVES:
                    distribute(0, 1, 0.75);
                    distribute(1, 1, 0.25);
                    break;
                case DITHER_FLOYD:
                    distribute( 1, 0, 7.0 / 16.0);
                    distribute(-1, 1, 3.0 / 16.0);
                    distribute( 0, 1, 5.0 / 16.0);
                    distribute( 0, 2, 1.0 / 16.0);
                    break;
                case DITHER_HORIZONTAL:
                    if(py & 1)
                    {
                        distribute(0, 1, 0.75);
                        distribute(1, 1, 0.25);
                    }
                    else
                    {
                        distribute(1, 0, 0.25);
                        distribute(2, 0, 0.75);
                    }
                    break;
                case DITHER_VAN_GOGH:
                    distribute_chunky( 1, 0, 7.0 / 16.0);
                    distribute_chunky(-1, 1, 3.0 / 16.0);
                    distribute_chunky( 0, 1, 5.0 / 16.0);
                    distribute_chunky( 0, 2, 1.0 / 16.0);
                    break;
                }
            }
        }
    }

    // Cellular automata:
    for(int i = 0; i < 1; i += 1)
    {
        if(cull_zags)
        {
            for(int py = 0; py < h - 1; py += 1)
            for(int px = 0; px < w - 2; px += 1)
            {
                std::uint8_t tl = at_dst_nes(px+0, py+0);
                std::uint8_t tc = at_dst_nes(px+1, py+0);
                std::uint8_t tr = at_dst_nes(px+2, py+0);
                std::uint8_t bl = at_dst_nes(px+0, py+1);
                std::uint8_t bc = at_dst_nes(px+1, py+1);
                std::uint8_t br = at_dst_nes(px+2, py+1);

                if(tc == bc)
                    continue;

                int eq = 0;
                eq += tl == bc;
                eq += bl == tc;
                eq += tr == bc;
                eq += br == tc;

                eq += tl != tc;
                eq += bl != bc;
                eq += tr != tc;
                eq += br != bc;

                if(eq < 7)
                    continue;

                std::swap(at_dst_nes(px+1, py+0), at_dst_nes(px+1, py+1));
            }
        }

        if(cull_dots)
        {
            fc::vector_map<std::uint8_t, int> c_map;
            std::vector<std::uint8_t> new_nes = dst_nes;

            for(int py = 0; py < h; py += 1)
            for(int px = 0; px < w; px += 1)
            {
                c_map.clear();
                std::uint8_t color = at_dst_nes(px, py);

                auto const get_neighbor = [&](int x, int y) -> std::uint8_t
                {
                    x += px;
                    y += py;
                    if(x < 0 || y < 0 || x >= w || y >= h)
                        return 0xFF;
                    return at_dst_nes(x, y);
                };

                auto const check_neighbor = [&](int x, int y, int weight)
                {
                    auto n = get_neighbor(x, y);
                    if(n < 64)
                        c_map[n] += weight;
                };

                check_neighbor(-1, -1, 1);
                check_neighbor( 1, -1, 1);
                check_neighbor(-1,  1, 1);
                check_neighbor( 1,  1, 1);

                check_neighbor(-1,  0, 16);
                check_neighbor( 1,  0, 16);
                check_neighbor( 0, -1, 8);
                check_neighbor( 0,  1, 8);

                if(c_map[color] == 0)
                {
                    auto it = std::ranges::max_element(c_map.container.begin(), c_map.container.end(), 
                                                       [&](auto const& a, auto const& b) { return a.second < b.second; });
                    if(it->second >= 32)
                        color = it->first;
                }

                new_nes[px + py * w] = color;
            }

            std::swap(dst_nes, new_nes);
        }

        if(cull_pipes)
        {
            fc::vector_map<std::uint8_t, int> c_map;
            std::vector<std::uint8_t> new_nes = dst_nes;

            for(int py = 0; py < h-1; py += 1)
            for(int px = 0; px < w; px += 1)
            {
                std::uint8_t color = at_dst_nes(px, py);

                if(color != at_dst_nes(px, py+1))
                    continue;

                c_map.clear();

                auto const get_neighbor = [&](int x, int y) -> std::uint8_t
                {
                    x += px;
                    y += py;
                    if(x < 0 || y < 0 || x >= w || y >= h)
                        return 0xFF;
                    return at_dst_nes(x, y);
                };

                auto const check_neighbor = [&](int x, int y, int weight)
                {
                    auto n = get_neighbor(x, y);
                    if(n < 64)
                        c_map[n] += weight;
                };

                check_neighbor(-1, -1, 1);
                check_neighbor( 1, -1, 1);
                check_neighbor(-1,  2, 1);
                check_neighbor( 1,  2, 1);

                check_neighbor(-1,  0, 16);
                check_neighbor( 1,  0, 16);
                check_neighbor(-1,  1, 16);
                check_neighbor( 1,  1, 16);
                check_neighbor( 0, -1, 8);
                check_neighbor( 0,  2, 8);

                if(c_map[color] == 0)
                {
                    auto it = std::ranges::max_element(c_map.container.begin(), c_map.container.end(), 
                                                       [&](auto const& a, auto const& b) { return a.second < b.second; });
                    if(it->second >= 64)
                        color = it->first;
                }

                new_nes[px + py * w] = color;
                new_nes[px + (py+1) * w] = color;
            }

            std::swap(dst_nes, new_nes);
        }

        std::array<std::uint8_t, 4> matched;
        std::vector<int> a_x, a_y, b_x, b_y;

        if(clean_lines)
        {
            for(int py = 0; py < h; py += 1)
            for(int px = 0; px < w; px += 1)
            {
                auto const pattern_match = [&](int pw, int ph, bool flip_x, bool flip_y, char const* pattern)
                {
                    for(int my = 0; my <= int(flip_y); my += 1)
                    for(int mx = 0; mx <= int(flip_x); mx += 1)
                    {
                        a_x.clear();
                        a_y.clear();
                        b_x.clear();
                        b_y.clear();
                        matched.fill(0xFF);

                        for(int iy = 0; iy < ph; iy += 1)
                        for(int ix = 0; ix < pw; ix += 1)
                        {
                            int x, y;

                            if(mx)
                                x = px + pw - ix - 1;
                            else
                                x = px + ix;

                            if(my)
                                y = py + ph - iy - 1;
                            else
                                y = py + iy;

                            if(x >= w)
                                goto next_iter;
                            if(y >= h)
                                goto next_iter;

                            std::uint8_t p = pattern[ix + iy*pw];
                            if(p == 'A')
                            {
                                a_x.push_back(x);
                                a_y.push_back(y);
                                p = 0;
                            }
                            else if(p == 'B')
                            {
                                b_x.push_back(x);
                                b_y.push_back(y);
                                p = 0;
                            }
                            else
                                p -= '0';
                            std::uint8_t const c = at_dst_nes(x, y);

                            if(p < matched.size())
                            {
                                if(matched[p] >= 64)
                                    matched[p] = c;
                                else if(matched[p] != c)
                                    goto next_iter;
                            }
                        }

                        if(matched[0] >= 64)
                            goto next_iter;

                        for(unsigned i = 1; i < matched.size(); i += 1)
                            if(matched[0] == matched[i])
                                goto next_iter;

                        if(matched[1] < 64)
                        {
                            for(int i = 0; i < a_x.size(); i += 1)
                            {
                                assert(at_dst_nes(a_x[i], a_y[i]) != matched[1]);
                                at_dst_nes(a_x[i], a_y[i]) = matched[1];
                            }
                        }

                        if(matched[2] < 64)
                        {
                            for(int i = 0; i < b_x.size(); i += 1)
                            {
                                assert(at_dst_nes(b_x[i], b_y[i]) != matched[2]);
                                at_dst_nes(b_x[i], b_y[i]) = matched[2];
                            }
                        }

                    next_iter:;
                    }
                };

                pattern_match(
                    4, 4, true, true,
                    ".022"
                    "1A02"
                    "11A0"
                    ".11.");

                pattern_match(
                    3, 4, true, false,
                    "111"
                    "0A1"
                    "200"
                    "222");

                pattern_match(
                    4, 3, false, true,
                    "1022"
                    "1A02"
                    "1102");

                pattern_match(
                    4, 4, true, false,
                    "1111"
                    "00A1"
                    "2B00"
                    "2222");

                pattern_match(
                    4, 4, false, true,
                    "1022"
                    "10B2"
                    "1A02"
                    "1102");
            }
        }
    }

    for(int py = 0; py < h; py += 1)
    for(int px = 0; px < w; px += 1)
        set_dst(px, py, nes_colors[at_dst_nes(px, py)]);

    if(output_image.IsOk())
        output_bitmap = wxBitmap(output_image);
    else
        std::fprintf(stderr, "Unable to bitmap output image.\n");
}

void model_t::auto_color(unsigned count, bool map)
{
    color_knobs = {};
    if(count == 0)
        return;
    if(!base_image.IsOk())
        return;

    count = std::min<unsigned>(count, color_knobs.size());

    unsigned char* const src_ptr = base_image.GetData();

    unsigned bw = base_image.GetWidth();  // base width
    unsigned bh = base_image.GetHeight(); // base height

    auto const get_src = [&](unsigned x, unsigned y) -> rgb_t
    {
        unsigned i = (x+y*bw)*3;
        return rgb_t{ src_ptr[i+0], src_ptr[i+1], src_ptr[i+2] };
    };

    struct bucket_t
    {
        int range;
        std::uint8_t rgb_t::*channel;
        std::vector<rgb_t> colors;

        auto operator<=>(bucket_t const& o) const { return range <=> o.range; }
    };

    std::vector<bucket_t> buckets;

    auto const set_bucket_range = [](bucket_t& bucket)
    {
        if(bucket.colors.empty())
        {
            bucket.range = 0;
            bucket.channel = 0;
            return;
        }

        rgb_t min = { 255, 255, 255 };
        rgb_t max = { 0, 0, 0 };
        for(rgb_t const& rgb : bucket.colors)
        {
            min.r = std::min(min.r, rgb.r);
            min.g = std::min(min.g, rgb.g);
            min.b = std::min(min.b, rgb.b);

            max.r = std::max(max.r, rgb.r);
            max.g = std::max(max.g, rgb.g);
            max.b = std::max(max.b, rgb.b);
        }

        rgb_t const dist = { max.r - min.r, max.g - min.g, max.b - min.b };

        if(dist.r >= dist.g && dist.r >= dist.b)
        {
            bucket.range = dist.r;
            bucket.channel = &rgb_t::r;
        }
        else if(dist.g >= dist.r && dist.g >= dist.b)
        {
            bucket.range = dist.g;
            bucket.channel = &rgb_t::g;
        }
        else
        {
            bucket.range = dist.b;
            bucket.channel = &rgb_t::b;
        }
    };

    auto& initial = buckets.emplace_back();
    for(unsigned y = 0; y < bh; y += 1)
    for(unsigned x = 0; x < bw; x += 1)
        initial.colors.push_back(get_src(x, y));
    set_bucket_range(initial);

    while(buckets.size() < count)
    {
        buckets.reserve(buckets.size() + 1);
        auto m = std::max_element(buckets.begin(), buckets.end());

        std::sort(m->colors.begin(), m->colors.end(), [&](rgb_t const& a, rgb_t const& b)
            { return a.*(m->channel) < b.*(m->channel); });

        auto& new_bucket = buckets.emplace_back();
        new_bucket.colors.assign(m->colors.begin() + m->colors.size() / 2, m->colors.end());
        m->colors.resize(m->colors.size() / 2);
        set_bucket_range(*m);
        set_bucket_range(new_bucket);
    }

    // Now assign colors:
    for(unsigned i = 0; i < buckets.size(); i += 1)
    {
        auto const& bucket = buckets[i];

        qerr_t avg = {};
        for(rgb_t const& rgb : bucket.colors)
        {
            avg.r += rgb.r;
            avg.g += rgb.g;
            avg.b += rgb.b;
        }

        if(bucket.colors.size())
        {
            avg.r /= bucket.colors.size();
            avg.g /= bucket.colors.size();
            avg.b /= bucket.colors.size();
        }

        unsigned best_color = 0xFF;
        float best_dist = ~0u;
        for(unsigned c = 0; c < 64; c += 1)
        {

            qerr_t d = avg;
            d.r -= nes_colors[c].r;
            d.g -= nes_colors[c].g;
            d.b -= nes_colors[c].b;
            float dist = distance(d);

            for(unsigned j = 0; j < i; j += 1)
                if(nes_colors[color_knobs[j].nes_color] == nes_colors[c])
                    goto skip;

            if(dist < best_dist)
            {
                best_dist = dist;
                best_color = c;
            }
        skip:;
        }

        color_knobs[i].nes_color = best_color;
        if(map)
            color_knobs[i].map_colors[0] = rgb_t{ avg.r, avg.g, avg.b };
        else
            color_knobs[i].map_colors[0] = nes_colors[best_color];
        color_knobs[i].map_enable[0] = true;
    }
}

