/* SPDX-License-Identifier: OFL-1.1
 * Renamed bitmap derivative of Monocraft; see Monocraft-OFL.txt and README.md.
 */
/*******************************************************************************
 * Size: 14 px
 * Bpp: 1
 * Opts: --font Monocraft.ttf --range 0x20-0x7E --size 14 --bpp 1 --format lvgl --no-compress --no-kerning --lv-font-name passport_pixel_14 --lv-include lvgl.h --output passport_pixel_14.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef PASSPORT_PIXEL_14
#define PASSPORT_PIXEL_14 1
#endif

#if PASSPORT_PIXEL_14

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0xff, 0xc,

    /* U+0022 "\"" */
    0xde, 0xf6,

    /* U+0023 "#" */
    0x6c, 0x6c, 0x6c, 0xff, 0x6c, 0x6c, 0xff, 0x6c,
    0x6c, 0x6c, 0x6c,

    /* U+0024 "$" */
    0x18, 0x18, 0x3f, 0xc0, 0x40, 0x3c, 0x3, 0x4,
    0xfc, 0x18, 0x18,

    /* U+0025 "%" */
    0xc3, 0xc4, 0xc4, 0x4, 0x4, 0x18, 0x20, 0x20,
    0x23, 0x43, 0xc3,

    /* U+0026 "&" */
    0x18, 0x28, 0x24, 0x18, 0x18, 0x3b, 0xdc, 0xdc,
    0xc4, 0xc, 0x3b,

    /* U+0027 "'" */
    0xfc,

    /* U+0028 "(" */
    0xc, 0x42, 0x30, 0xc3, 0xc, 0x30, 0x20, 0x40,
    0xc0,

    /* U+0029 ")" */
    0xc1, 0x2, 0x7, 0xc, 0x30, 0xc3, 0x31, 0xc,
    0x0,

    /* U+002A "*" */
    0xcc, 0xc5, 0x33,

    /* U+002B "+" */
    0x18, 0x18, 0x18, 0xff, 0x18, 0x18, 0x18, 0x18,

    /* U+002C "," */
    0x2b, 0x0,

    /* U+002D "-" */
    0xff,

    /* U+002E "." */
    0xc0,

    /* U+002F "/" */
    0x3, 0x4, 0x4, 0x4, 0x4, 0x18, 0x20, 0x20,
    0x20, 0x40, 0xc0,

    /* U+0030 "0" */
    0x3c, 0xc3, 0xc3, 0xc7, 0xcb, 0xdb, 0xe3, 0xc3,
    0xc3, 0x4, 0x3c,

    /* U+0031 "1" */
    0x18, 0x18, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0xff,

    /* U+0032 "2" */
    0x3c, 0x44, 0xc3, 0x3, 0x3, 0x1c, 0x20, 0xc3,
    0xc3, 0xc3, 0xff,

    /* U+0033 "3" */
    0x3c, 0x44, 0xc3, 0x3, 0x3, 0x1c, 0x3, 0x3,
    0xc3, 0x44, 0x3c,

    /* U+0034 "4" */
    0x7, 0xb, 0x1b, 0x23, 0xc3, 0xc3, 0xff, 0x3,
    0x3, 0x3, 0x3,

    /* U+0035 "5" */
    0xff, 0xc0, 0xc0, 0xfc, 0x3, 0x3, 0x3, 0x3,
    0xc3, 0x44, 0x3c,

    /* U+0036 "6" */
    0x1c, 0x20, 0x20, 0xc0, 0xc0, 0xfc, 0xc3, 0xc3,
    0xc3, 0x4, 0x3c,

    /* U+0037 "7" */
    0xff, 0xc3, 0xc3, 0x3, 0x3, 0x4, 0x18, 0x18,
    0x18, 0x18, 0x18,

    /* U+0038 "8" */
    0x3c, 0x44, 0xc3, 0xc3, 0xc3, 0x3c, 0xc3, 0xc3,
    0xc3, 0x4, 0x3c,

    /* U+0039 "9" */
    0x3c, 0x44, 0xc3, 0xc3, 0xc3, 0x3f, 0x3, 0x3,
    0x4, 0x8, 0x38,

    /* U+003A ":" */
    0xc0, 0x3,

    /* U+003B ";" */
    0x20, 0x0, 0x1, 0x58,

    /* U+003C "<" */
    0x4, 0x21, 0x88, 0x43, 0x2, 0x8, 0x18, 0x20,
    0x40,

    /* U+003D "=" */
    0xff, 0x0, 0x0, 0x0, 0xff,

    /* U+003E ">" */
    0xc1, 0x2, 0x6, 0x8, 0x11, 0x88, 0x21, 0xc,
    0x0,

    /* U+003F "?" */
    0x3c, 0x44, 0xc3, 0x3, 0x3, 0x4, 0x18, 0x0,
    0x0, 0x0, 0x18,

    /* U+0040 "@" */
    0x3c, 0xc3, 0xc3, 0xdb, 0xdb, 0xdb, 0xdb, 0xdf,
    0xc0, 0x40, 0x3f,

    /* U+0041 "A" */
    0x3c, 0x4, 0xc3, 0xff, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+0042 "B" */
    0xfc, 0xc4, 0xc3, 0xfc, 0xc4, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0xfc,

    /* U+0043 "C" */
    0x3c, 0x44, 0xc3, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc3, 0x44, 0x3c,

    /* U+0044 "D" */
    0xfc, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc0, 0xfc,

    /* U+0045 "E" */
    0xff, 0xc0, 0xc0, 0xf8, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc0, 0xff,

    /* U+0046 "F" */
    0xff, 0xc0, 0xc0, 0xf8, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc0, 0xc0,

    /* U+0047 "G" */
    0x3f, 0xc0, 0xc0, 0xc7, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0x44, 0x3c,

    /* U+0048 "H" */
    0xc3, 0xc3, 0xc3, 0xff, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+0049 "I" */
    0xfb, 0x18, 0xc6, 0x31, 0x8c, 0x63, 0x3e,

    /* U+004A "J" */
    0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3,
    0xc3, 0x44, 0x3c,

    /* U+004B "K" */
    0xc3, 0xc4, 0xc4, 0xf8, 0xc8, 0xc4, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+004C "L" */
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc0, 0xff,

    /* U+004D "M" */
    0xc3, 0xc3, 0xe7, 0xdb, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+004E "N" */
    0xc3, 0xc3, 0xe3, 0xdb, 0xcb, 0xc7, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+004F "O" */
    0x3c, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0x4, 0x3c,

    /* U+0050 "P" */
    0xfc, 0xc4, 0xc3, 0xfc, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc0, 0xc0,

    /* U+0051 "Q" */
    0x3c, 0x44, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc4, 0xc, 0x3b,

    /* U+0052 "R" */
    0xfc, 0xc4, 0xc3, 0xfc, 0xc4, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+0053 "S" */
    0x3f, 0x40, 0xc0, 0x3c, 0x3, 0x3, 0x3, 0x3,
    0xc3, 0x44, 0x3c,

    /* U+0054 "T" */
    0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18,

    /* U+0055 "U" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0x44, 0x3c,

    /* U+0056 "V" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x24, 0x24,
    0x24, 0x28, 0x18,

    /* U+0057 "W" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xdb, 0xeb,
    0xe7, 0xc3, 0xc3,

    /* U+0058 "X" */
    0xc3, 0x44, 0x24, 0x18, 0x28, 0x24, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+0059 "Y" */
    0xc3, 0x44, 0x24, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18,

    /* U+005A "Z" */
    0xff, 0x3, 0x3, 0x4, 0x8, 0x18, 0x20, 0xc0,
    0xc0, 0xc0, 0xff,

    /* U+005B "[" */
    0xfe, 0x31, 0x8c, 0x63, 0x18, 0xc6, 0x3e,

    /* U+005C "\\" */
    0xc0, 0x40, 0x20, 0x20, 0x20, 0x18, 0x4, 0x4,
    0x4, 0x4, 0x3,

    /* U+005D "]" */
    0xf8, 0xc6, 0x31, 0x8c, 0x63, 0x18, 0xfe,

    /* U+005E "^" */
    0x18, 0x28, 0x24, 0x44, 0xc3,

    /* U+005F "_" */
    0xff,

    /* U+0060 "`" */
    0xc8, 0x80,

    /* U+0061 "a" */
    0x3c, 0x3, 0x3, 0x3f, 0x43, 0xc3, 0x3, 0x3f,

    /* U+0062 "b" */
    0xc0, 0xc0, 0xc0, 0xdc, 0xc4, 0xe3, 0xc3, 0xc3,
    0xc3, 0xc3, 0xfc,

    /* U+0063 "c" */
    0x3c, 0x44, 0xc3, 0xc0, 0xc0, 0xc3, 0x44, 0x3c,

    /* U+0064 "d" */
    0x3, 0x3, 0x3, 0x3b, 0x4b, 0xc7, 0xc3, 0xc3,
    0xc3, 0xc3, 0x3f,

    /* U+0065 "e" */
    0x3c, 0x4, 0xc3, 0xff, 0xc0, 0xc0, 0xc0, 0x3f,

    /* U+0066 "f" */
    0x1c, 0x86, 0x3f, 0x61, 0x86, 0x18, 0x61, 0x86,
    0x0,

    /* U+0067 "g" */
    0x3f, 0x43, 0xc3, 0xc3, 0xc3, 0x3, 0x3f, 0x3,
    0x4, 0xfc,

    /* U+0068 "h" */
    0xc0, 0xc0, 0xc0, 0xdc, 0xe4, 0xe3, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+0069 "i" */
    0x30, 0x0, 0x7, 0x83, 0x6, 0xc, 0x18, 0x30,
    0x60, 0x38,

    /* U+006A "j" */
    0x3, 0x0, 0x0, 0x3, 0x3, 0x3, 0x3, 0x3,
    0xc3, 0xc3, 0xc3, 0x44, 0x3c,

    /* U+006B "k" */
    0xc3, 0xc, 0x31, 0xcb, 0x6e, 0x38, 0xdb, 0x2c,
    0x40,

    /* U+006C "l" */
    0xf0, 0x60, 0xc1, 0x83, 0x6, 0xc, 0x18, 0x30,
    0x60, 0x38,

    /* U+006D "m" */
    0xe4, 0xec, 0xdb, 0xdb, 0xdb, 0xc3, 0xc3, 0xc3,

    /* U+006E "n" */
    0xfc, 0xc4, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,

    /* U+006F "o" */
    0x3c, 0x44, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x3c,

    /* U+0070 "p" */
    0xdc, 0xc4, 0xe3, 0xc3, 0xc3, 0xc4, 0xfc, 0xc0,
    0xc0, 0xc0,

    /* U+0071 "q" */
    0x3b, 0x4b, 0xc7, 0xc3, 0xc3, 0x3, 0x3f, 0x3,
    0x3, 0x3,

    /* U+0072 "r" */
    0xdc, 0xe4, 0xe3, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,

    /* U+0073 "s" */
    0x3f, 0x40, 0xc0, 0x3c, 0x4, 0x3, 0x4, 0xfc,

    /* U+0074 "t" */
    0x61, 0x86, 0x3e, 0x61, 0x86, 0x18, 0x60, 0x81,
    0xc0,

    /* U+0075 "u" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x43, 0x3f,

    /* U+0076 "v" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x24, 0x28, 0x18,

    /* U+0077 "w" */
    0xc3, 0xc3, 0xc3, 0xdb, 0xdb, 0xdb, 0x5b, 0x3f,

    /* U+0078 "x" */
    0xc3, 0x44, 0x24, 0x18, 0x28, 0x24, 0x44, 0xc3,

    /* U+0079 "y" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x43, 0x3f, 0x3,
    0x4, 0xfc,

    /* U+007A "z" */
    0xff, 0x4, 0x4, 0x18, 0x60, 0x60, 0x60, 0xff,

    /* U+007B "{" */
    0x4, 0x21, 0x86, 0x1b, 0x81, 0x86, 0x18, 0x20,
    0x40,

    /* U+007C "|" */
    0xff, 0xff, 0xfc,

    /* U+007D "}" */
    0xc0, 0x80, 0x81, 0x2, 0x3, 0xcc, 0x10, 0x20,
    0x83, 0x0,

    /* U+007E "~" */
    0x23, 0xdb, 0xcc, 0xc4
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 149, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 149, .box_w = 2, .box_h = 11, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 149, .box_w = 5, .box_h = 3, .ofs_x = 2, .ofs_y = 8},
    {.bitmap_index = 6, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 17, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 28, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 39, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 50, .adv_w = 149, .box_w = 2, .box_h = 3, .ofs_x = 3, .ofs_y = 8},
    {.bitmap_index = 51, .adv_w = 149, .box_w = 6, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 60, .adv_w = 149, .box_w = 6, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 69, .adv_w = 149, .box_w = 6, .box_h = 4, .ofs_x = 2, .ofs_y = 7},
    {.bitmap_index = 72, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 80, .adv_w = 149, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 82, .adv_w = 149, .box_w = 8, .box_h = 1, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 83, .adv_w = 149, .box_w = 2, .box_h = 1, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 95, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 106, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 117, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 128, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 139, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 150, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 161, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 172, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 183, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 194, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 205, .adv_w = 149, .box_w = 2, .box_h = 8, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 207, .adv_w = 149, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 211, .adv_w = 149, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 220, .adv_w = 149, .box_w = 8, .box_h = 5, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 225, .adv_w = 149, .box_w = 6, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 234, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 245, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 256, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 267, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 278, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 289, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 300, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 311, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 322, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 333, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 344, .adv_w = 149, .box_w = 5, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 351, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 362, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 373, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 384, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 395, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 406, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 417, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 428, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 439, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 450, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 461, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 472, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 483, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 494, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 505, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 516, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 527, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 538, .adv_w = 149, .box_w = 5, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 545, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 556, .adv_w = 149, .box_w = 5, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 563, .adv_w = 149, .box_w = 8, .box_h = 5, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 568, .adv_w = 149, .box_w = 8, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 569, .adv_w = 149, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 8},
    {.bitmap_index = 571, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 579, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 590, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 598, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 609, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 617, .adv_w = 149, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 626, .adv_w = 149, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 636, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 647, .adv_w = 149, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 657, .adv_w = 149, .box_w = 8, .box_h = 13, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 670, .adv_w = 149, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 679, .adv_w = 149, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 689, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 697, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 705, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 713, .adv_w = 149, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 723, .adv_w = 149, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 733, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 741, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 749, .adv_w = 149, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 758, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 766, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 774, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 782, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 790, .adv_w = 149, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 800, .adv_w = 149, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 808, .adv_w = 149, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 817, .adv_w = 149, .box_w = 2, .box_h = 11, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 820, .adv_w = 149, .box_w = 7, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 830, .adv_w = 149, .box_w = 8, .box_h = 4, .ofs_x = 0, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t passport_pixel_14 = {
#else
lv_font_t passport_pixel_14 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 14,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if PASSPORT_PIXEL_14*/

