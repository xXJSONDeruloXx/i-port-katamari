#include "atc_decompress.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

static int failures = 0;

static void make_color(std::uint8_t *block)
{
    /* Flat white RGB endpoint 0, selected for every texel. */
    block[8] = 0xff;
    block[9] = 0x7f;
    block[10] = 0;
    block[11] = 0;
    block[12] = block[13] = block[14] = block[15] = 0;
}

static void set_selectors(std::uint8_t *block, const unsigned idx[16])
{
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i)
        bits |= (std::uint64_t)(idx[i] & 7u) << (3 * i);
    for (int i = 0; i < 6; ++i)
        block[2 + i] = (std::uint8_t)(bits >> (8 * i));
}

static void check(const char *name, std::uint8_t a0, std::uint8_t a1,
                  const unsigned idx[16], const unsigned expected[8])
{
    std::uint8_t block[16] = {};
    block[0] = a0;
    block[1] = a1;
    set_selectors(block, idx);
    make_color(block);

    std::uint8_t rgba[64] = {};
    if (!atc::decode_rgba_interpolated(block, sizeof(block), 4, 4, rgba)) {
        std::printf("FAIL %s: decoder rejected block\n", name);
        ++failures;
        return;
    }

    for (int i = 0; i < 16; ++i) {
        const unsigned got = rgba[i * 4 + 3];
        const unsigned want = expected[idx[i] & 7u];
        if (got != want) {
            std::printf("FAIL %s pixel %d: alpha=%u expected=%u (selector=%u)\n",
                        name, i, got, want, idx[i] & 7u);
            ++failures;
            return;
        }
    }
    std::printf("ok   %s\n", name);
}

int main()
{
    const unsigned idx[16] = {0,1,2,3,4,5,6,7,7,6,5,4,3,2,1,0};

    /* BC3/ATC eight-alpha mode: a0 > a1. */
    const unsigned eight[8] = {200, 60, 180, 160, 140, 120, 100, 80};
    check("interpolated 8-alpha", 200, 60, idx, eight);

    /* Six-alpha mode adds the fixed 0 and 255 entries. */
    const unsigned six[8] = {10, 110, 30, 50, 70, 90, 0, 255};
    check("interpolated 6-alpha", 10, 110, idx, six);

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
