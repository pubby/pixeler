#ifndef NES_COLORS_HPP
#define NES_COLORS_HPP

#include <array>
#include <cmath>

struct [[gnu::packed]] rgb_t
{
    unsigned char r, g, b;
    auto operator<=>(rgb_t const&) const = default;
};
static_assert(sizeof(rgb_t) == 3);

struct qerr_t
{
    int r, g, b;
    auto operator<=>(qerr_t const&) const = default;
};

constexpr rgb_t RED = { 255, 0, 0 };
constexpr rgb_t BLACK = { 0, 0, 0 };
constexpr rgb_t WHITE = { 255, 255, 255 };
constexpr rgb_t GREY = { 127, 127, 127 };
inline rgb_t invert(rgb_t a) { return { 255 - a.r, 255 - a.g, 255 - a.b }; }

inline qerr_t qerr(rgb_t a, rgb_t b)
{
    return { int(a.r) - int(b.r), int(a.g) - int(b.g), int(a.b) - int(b.b) };
}

inline float distance(qerr_t q)
{
    return std::sqrt(float(q.r*q.r + q.g*q.g + q.b*q.b));
}

inline float distance(rgb_t a, rgb_t b)
{
    return distance(qerr(a, b));
}

inline int hue(rgb_t a) 
{
    int min = std::min({ a.r, a.g, a.b });
    int max = std::max({ a.r, a.g, a.b });

    if(min == max)
        return 0;

    float hue = 0.0f;
    if(max == a.r)
        hue = float(a.g - a.b) / float(max - min);
    else if(max == a.g)
        hue = float(a.b - a.r) / float(max - min);
    else
        hue = float(a.r - a.g) / float(max - min);

    hue *= 60.0f;
    if(hue < 0.0f) 
        hue += 360.0f;

    return std::round(hue);
}

constexpr std::array<rgb_t, 64> nes_colors = {{
    { 101, 101, 101 },
    { 0, 43, 155 },
    { 17, 14, 192 },
    { 63, 0, 188 },
    { 102, 0, 143 },
    { 123, 0, 69 },
    { 121, 1, 0 },
    { 96, 28, 0 },
    { 54, 56, 0 },
    { 8, 79, 0 },
    { 0, 90, 0 },
    { 0, 87, 2 },
    { 0, 69, 85 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 174, 174, 174 },
    { 7, 97, 245 },
    { 62, 59, 255 },
    { 124, 29, 255 },
    { 175, 14, 229 },
    { 203, 19, 131 },
    { 200, 42, 21 },
    { 167, 77, 0 },
    { 111, 114, 0 },
    { 50, 145, 0 },
    { 0, 159, 0 },
    { 0, 155, 42 },
    { 0, 132, 152 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 255, 255, 255 },
    { 86, 177, 255 },
    { 142, 139, 255 },
    { 204, 108, 255 },
    { 255, 93, 255 },
    { 255, 98, 212 },
    { 255, 121, 100 },
    { 248, 157, 6 },
    { 192, 195, 0 },
    { 129, 226, 0 },
    { 77, 241, 22 },
    { 48, 236, 122 },
    { 52, 213, 234 },
    { 78, 78, 78 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 255, 255, 255 },
    { 186, 223, 255 },
    { 209, 208, 255 },
    { 235, 195, 255 },
    { 255, 189, 255 },
    { 255, 191, 238 },
    { 255, 200, 192 },
    { 252, 215, 153 },
    { 229, 231, 132 },
    { 204, 243, 135 },
    { 182, 249, 160 },
    { 170, 248, 201 },
    { 172, 238, 247 },
    { 183, 183, 183 },
    { 0, 0, 0 },
    { 0, 0, 0 },
}};

#endif
