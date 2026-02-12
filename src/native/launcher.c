/* launcher.c - Faithful recreation of GOT.EXE launcher / main menu
 *
 * Reverse-engineered from GOT.EXE (16-bit DOS):
 *   sub_15DA8  - real main (opening sequence + menu loop)
 *   sub_16783  - opening screen 2 (Adept Software)
 *   sub_1643F  - title screen (God of Thunder + shake)
 *   sub_163F7  - shake animation (105 frames, random VGA scroll)
 *   sub_16807  - credits with shadow fade effect
 *   sub_1B2F9  - shadow sprite blit (remap through 256-byte LUT)
 *   sub_1B203  - opaque sprite blit (inline pixel data)
 *   sub_1A00B  - draw shadow sprite at position
 *   sub_1A0BC  - draw opaque sprite at position
 *   sub_190C1  - main menu loop
 *   sub_17E86  - draw tiled menu background (chunks 0x1A, 0x1B)
 *   sub_184AE  - generic menu system (MakeStringList)
 *   sub_18B24  - episode selection
 *   sub_1A744  - palette fade in (64 steps)
 *   sub_1A78D  - palette fade out (64 steps)
 *
 * Uses raylib for rendering.  GRAPHICS.GOT provides all imagery.
 */
#include "launcher.h"
#include "launcher_extras.h"
#include "graphics_got.h"
#include "raylib.h"

#include "digisnd.h"
#include "mu_man.h"
#include "mixer.h"
#include "res_man.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── constants (from RE) ── */

enum {
    SCALE    = 3,

    /* Chunk indices in GRAPHICS.GOT */
    GG_FONT            = 1,    /* 0x01 - bitmap font data */
    GG_PALETTE_MENU     = 6,    /* 0x06 - menu palette */
    GG_TILE_BG          = 26,   /* 0x1A - 32x32 background tile */
    GG_TILE_BOTTOM      = 27,   /* 0x1B - 32x8 bottom strip tile */
    GG_TITLE_PALETTE    = 35,   /* 0x23 - title screen palette */
    GG_TITLE_PLANE0     = 36,   /* 0x24 - title Mode X plane 0 */
    GG_TITLE_PLANE1     = 37,   /* 0x25 - title Mode X plane 1 */
    GG_TITLE_PLANE2     = 38,   /* 0x26 - title Mode X plane 2 */
    GG_TITLE_PLANE3     = 39,   /* 0x27 - title Mode X plane 3 */
    GG_TITLE_STRIP      = 40,   /* 0x28 - title bottom strip (320x80 Mode X) */
    GG_CREDITS_PAL      = 41,   /* 0x29 - credits palette */
    GG_CREDITS_BG       = 42,   /* 0x2A - credits background 320x200 */
    GG_CREDIT_NAME0     = 43,   /* 0x2B - first name shadow sprite */
    GG_CREDIT_REMAP0    = 52,   /* 0x34 - first name remap table (15 levels) */
    GG_CREDIT_TITLE0    = 67,   /* 0x43 - first title shadow sprite */
    GG_CREDIT_REMAP2_0  = 76,   /* 0x4C - first title remap table (15 levels) */
    GG_OPEN2_PAL        = 91,   /* 0x5B - opening screen 2 palette */
    GG_OPEN2_BG         = 92,   /* 0x5C - opening screen 2 background */
    GG_OPEN2_ANIM       = 93,   /* 0x5D - opening screen 2 compiled sprite anim */
    GG_FRAME_TL         = 7,    /* 0x07 - frame top-left corner (32x32) */
    GG_MENU_MUSIC       = 34,   /* 0x22 - AdLib OPL2 opening/menu music */
    GG_CURSOR0          = 29,   /* 0x1D - animated selector frame 0 */
    GG_THUNDER_VOC      = 104,  /* 0x68 - thunder SFX (VOC) for title shake */

    /* Menu text colors (from select_option / sub_17C6F / sub_17CB8) */
    MENU_TITLE_COLOR    = 54,   /* 0x36 - title text color */
    MENU_SEL_COLOR      = 24,   /* 0x18 - selected item color */
    MENU_NORM_COLOR     = 14,   /* 0x0E - unselected item color */
    MENU_SHADOW_COLOR   = 223,  /* 0xDF - text shadow color */
    MENU_BG_COLOR       = 215,  /* 0xD7 - menu box fill color */

    NUM_CREDITS         = 9,
    NUM_FADE_STEPS      = 15,   /* credit fade levels per direction */
    FADE_STEPS          = 64,   /* palette fade steps (matching original) */
    SHAKE_FRAMES        = 105,  /* title shake duration (sub_163F7) */
};

/* Episode names (from GOT.EXE at 0x2125A) */
static const char *episode_names[] = {
    "Part 1: Serpent Surprise!",
    "Part 2: Non-stick Nognir",
    "Part 3: Lookin' for Loki",
    NULL
};

/* Menu items (from GOT.EXE at 0x21638, plus Extras for re-release) */
static const char *main_menu_items[] = {
    "Play Game",
    "Credits",
    "Extras",
    "Quit",
    NULL
};

/* ── state ── */

graphics_got_t  s_gg;
int             s_gg_loaded;
static Texture2D       s_fb_tex;
uint8_t         s_screen[SCREEN_W * SCREEN_H];    /* 8-bit indexed fb */
uint8_t         s_pal6[768];                       /* target palette, VGA 6-bit */
uint8_t         s_pal_rgb[256][4];                 /* current RGBA for display */

/* Bitmap font from GRAPHICS.GOT chunk 1.
 * Format: 6-byte header (count u16, w u16, h u16), then count*w*h bytes.
 * Each byte: 0=transparent, 0xFF=draw.  Row-major 8x9 per glyph. */
static uint8_t        *s_font;          /* raw glyph data (after header) */
int             s_font_w;        /* glyph width (8) */
int             s_font_h;        /* glyph height (9) */
static int             s_font_count;    /* number of glyphs (94) */

/* Menu frame sprites from GRAPHICS.GOT (chunks 7-15, 32x32 opaque sprites).
 * Layout from sub_17939:
 *   [0]=TL corner  [1]=top border  [2]=TR corner
 *   [3]=left border [4]=fill       [5]=right border
 *   [6]=BL corner  [7]=bot border  [8]=BR corner */
uint8_t        *s_frame[9];

/* Cursor animation sprites (chunks 29-32, 16x16 opaque sprites) */
uint8_t        *s_cursor[4];
static int             s_cursor_frame;     /* 0-3 cycling animation */
static double          s_cursor_time;      /* time of last frame advance */

/* Audio state */
int             s_audio_ready;
static uint8_t        *s_music_buf;       /* kept alive while music plays */

/* Menu sound effects loaded from GOTRES.DAT "DIGSOUND" resource.
 * WOOP plays on menu open / arrow navigation; CLANG on selection. */
static uint8_t        *s_snd_woop;
static int             s_snd_woop_len;
static uint8_t        *s_snd_clang;
static int             s_snd_clang_len;

/* ── helpers ── */

static uint16_t rd16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* Check if user wants to skip (any key or mouse click). */
int check_skip(void) {
    return IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)
        || IsKeyPressed(KEY_ESCAPE) || GetKeyPressed() != 0
        || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* Drain any stale input events that accumulated during window creation.
 * Also renders a few black frames to let the window fully initialize. */
static void drain_input(void) {
    int i;
    for (i = 0; i < 5; i++) {
        BeginDrawing();
        ClearBackground(BLACK);
        EndDrawing();
        while (GetKeyPressed() != 0) {}
    }
}

/* ── audio helpers ── */

/* External: declared in audio_raylib.c / platform_raylib.c */
int  sbfx_init(void);
void sbfx_exit(void);
void got_platform_pump(void);

/* Start music from GRAPHICS.GOT chunk. Keeps buffer alive.
 *
 * GOT.EXE's launcher runs its PIT timer at 140 Hz (TML_StartupService pushes
 * 0x8C = 140 to TML_PCSetRate), while the in-game engine (_G*.EXE) runs at
 * 120 Hz.  The music delay values in GRAPHICS.GOT are therefore calibrated for
 * 140 Hz ticks.  Since our native engine ticks MU_Service at 120 Hz, we scale
 * every delay byte by 120/140 = 6/7 so that each note plays at the correct
 * wall-clock time. */
static void launcher_start_music(int chunk_idx) {
    uint8_t *data;
    int size, i, note_count;
    uint8_t *p;
    if (!s_audio_ready || !s_gg_loaded) {
        fprintf(stderr, "[launcher] start_music: skipped (audio=%d gg=%d)\n",
                s_audio_ready, s_gg_loaded);
        return;
    }
    if (chunk_idx >= s_gg.chunk_count) {
        fprintf(stderr, "[launcher] start_music: chunk %d out of range\n", chunk_idx);
        return;
    }
    size = s_gg.descs[chunk_idx].out_size;
    data = gg_decompress_alloc(&s_gg, chunk_idx);
    if (!data) {
        fprintf(stderr, "[launcher] start_music: decompress failed for chunk %d\n",
                chunk_idx);
        return;
    }

    /* Scale delay values from 140 Hz to 120 Hz: new_delay = delay * 6 / 7 */
    note_count = (size - 2) / 3;   /* 2-byte header, 3 bytes per note */
    p = data + 2;
    for (i = 0; i < note_count; i++) {
        int d = p[0];
        p[0] = (uint8_t)((d * 6 + 3) / 7);  /* round to nearest */
        p += 3;
    }

    /* Stop previous music and free its buffer */
    MU_MusicOff();
    free(s_music_buf);
    s_music_buf = data;
    MU_StartMusic((char *)data, (long)size);
    fprintf(stderr, "[launcher] start_music: chunk %d, %d bytes, playing=%d\n",
            chunk_idx, size, MU_MusicPlaying());
}

static void launcher_stop_music(void) {
    MU_MusicOff();
    free(s_music_buf);
    s_music_buf = NULL;
}

/* Play a VOC sound effect from GRAPHICS.GOT chunk. */
static void launcher_play_sound(int chunk_idx) {
    uint8_t *data;
    if (!s_audio_ready || !s_gg_loaded) return;
    data = gg_decompress_alloc(&s_gg, chunk_idx);
    if (!data) return;
    SB_PlayVoc(data, true);
    free(data); /* mixer has its own copy of decoded PCM */
}

/* ── menu sound effects (from GOTRES.DAT "DIGSOUND" resource) ── */

/* Sound indices within the DIGSOUND resource (matches game_define.h enum) */
enum { SND_WOOP = 9, SND_CLANG = 14, SND_NUM = 16 };

/* Load WOOP and CLANG VOC data from GOTRES.DAT for menu SFX.
 * Uses the res_man API to read the "DIGSOUND" resource, then extracts
 * individual sound entries by walking the HEADER array. */
static void launcher_load_menu_sounds(void) {
    char *lzss_buf;
    uint8_t *snd_data;
    uint32_t lengths[SND_NUM];
    uint8_t *p;
    int i;
    uint32_t offset;

    lzss_buf = (char *)malloc(18000);
    if (!lzss_buf) return;

    res_init(lzss_buf);
    if (res_open("GOTRES.DAT") < 0) {
        fprintf(stderr, "[launcher] Can't open GOTRES.DAT for menu sounds\n");
        free(lzss_buf);
        return;
    }

    snd_data = (uint8_t *)res_falloc_read("DIGSOUND");
    res_close();
    free(lzss_buf);

    if (!snd_data) {
        fprintf(stderr, "[launcher] Can't read DIGSOUND from GOTRES.DAT\n");
        return;
    }

    /* Parse HEADER array: 16 entries of {uint32_t offset, uint32_t length} */
    p = snd_data;
    for (i = 0; i < SND_NUM; i++) {
        p += 4; /* skip offset field (unused for sequential layout) */
        memcpy(&lengths[i], p, 4);
        p += 4;
    }
    /* p now points to start of sound data (after 128-byte header) */

    /* Walk to WOOP (index 9) and CLANG (index 14), copy their VOC data */
    offset = 0;
    for (i = 0; i < SND_NUM; i++) {
        if (i == SND_WOOP) {
            s_snd_woop_len = (int)lengths[i];
            s_snd_woop = (uint8_t *)malloc(s_snd_woop_len);
            if (s_snd_woop) memcpy(s_snd_woop, p + offset, s_snd_woop_len);
        }
        if (i == SND_CLANG) {
            s_snd_clang_len = (int)lengths[i];
            s_snd_clang = (uint8_t *)malloc(s_snd_clang_len);
            if (s_snd_clang) memcpy(s_snd_clang, p + offset, s_snd_clang_len);
        }
        offset += lengths[i];
    }

    free(snd_data);
    fprintf(stderr, "[launcher] Menu sounds loaded: WOOP=%d bytes, CLANG=%d bytes\n",
            s_snd_woop_len, s_snd_clang_len);
}

void launcher_play_woop(void) {
    if (s_audio_ready && s_snd_woop)
        SB_PlayVoc(s_snd_woop, true);
}

void launcher_play_clang(void) {
    if (s_audio_ready && s_snd_clang)
        SB_PlayVoc(s_snd_clang, true);
}

/* ── bitmap font ── */

/* Load font from GRAPHICS.GOT chunk 1.
 * Header: count(u16) width(u16) height(u16), then count*w*h bytes of glyph data.
 * Glyph format: row-major 8x9, each byte 0=transparent or 0xFF=draw. */
static void font_load(void) {
    uint8_t *raw = gg_decompress_alloc(&s_gg, GG_FONT);
    if (!raw) return;
    s_font_count = rd16le(raw + 0);
    s_font_w     = rd16le(raw + 2);
    s_font_h     = rd16le(raw + 4);
    {
        int sz = s_font_count * s_font_w * s_font_h;
        s_font = (uint8_t *)malloc(sz);
        if (s_font) memcpy(s_font, raw + 6, sz);
    }
    free(raw);
}

/* Draw a single character at (dx,dy) in palette color `color`. No shadow. */
static void font_putch(int dx, int dy, int ch, uint8_t color) {
    int glyph_size, row, col;
    const uint8_t *g;
    if (!s_font || ch < 32 || ch > 31 + s_font_count) return;
    glyph_size = s_font_w * s_font_h;
    g = s_font + (ch - 32) * glyph_size;
    for (row = 0; row < s_font_h; row++) {
        int py = dy + row;
        if (py < 0 || py >= SCREEN_H) continue;
        for (col = 0; col < s_font_w; col++) {
            if (g[row * s_font_w + col]) {
                int px = dx + col;
                if (px >= 0 && px < SCREEN_W)
                    s_screen[py * SCREEN_W + px] = color;
            }
        }
    }
}

/* Draw a string at (dx,dy). Supports ~X color codes (hex digit). */
void font_print(int dx, int dy, const char *str, uint8_t color) {
    uint8_t cur_color = color;
    int x = dx;
    while (*str) {
        if (*str == '~' && str[1]) {
            int h = str[1];
            if (h >= '0' && h <= '9') cur_color = (uint8_t)(h - '0');
            else if (h >= 'a' && h <= 'f') cur_color = (uint8_t)(10 + h - 'a');
            else if (h >= 'A' && h <= 'F') cur_color = (uint8_t)(10 + h - 'A');
            str += 2;
            continue;
        }
        font_putch(x, dy, (unsigned char)*str, cur_color);
        x += s_font_w;
        str++;
    }
}

/* Measure string width in pixels. */
int font_width(const char *str) {
    int w = 0;
    while (*str) {
        if (*str == '~' && str[1]) { str += 2; continue; }
        w += s_font_w;
        str++;
    }
    return w;
}

/* Draw string centered horizontally at given y. */
void font_center(int dy, const char *str, uint8_t color) {
    int w = font_width(str);
    font_print((SCREEN_W - w) / 2, dy, str, color);
}

/* Draw text with 1-pixel shadow at (x+1,y+1), matching GOT.EXE sub_17BE3.
 * Draws full string in shadow color offset by (+1,+1), then main color on top. */
void font_print_shadow(int dx, int dy, const char *str,
                               uint8_t color, uint8_t shadow) {
    font_print(dx + 1, dy + 1, str, shadow);
    font_print(dx, dy, str, color);
}

/* Draw centered text with 1-pixel shadow. */
static void font_center_shadow(int dy, const char *str,
                                uint8_t color, uint8_t shadow) {
    int w = font_width(str);
    int dx = (SCREEN_W - w) / 2;
    font_print_shadow(dx, dy, str, color, shadow);
}

/* ── palette management ── */

/* Load a 768-byte VGA palette chunk into s_pal6. */
static void load_palette(int chunk_idx) {
    uint8_t *d = gg_decompress_alloc(&s_gg, chunk_idx);
    if (d) { memcpy(s_pal6, d, 768); free(d); }
}

/* Convert 6-bit VGA value to 8-bit. */
static uint8_t vga6_to_8(uint8_t v) {
    return (uint8_t)((v * 255 + 31) / 63);
}

/* Load a raw 768-byte VGA palette into s_pal6 (for game resources). */
void load_pal6_raw(const uint8_t *p) {
    memcpy(s_pal6, p, 768);
}

/* Set display palette to target at given brightness (0.0–1.0). */
void set_brightness(float b) {
    int i;
    if (b < 0.0f) b = 0.0f;
    if (b > 1.0f) b = 1.0f;
    for (i = 0; i < 256; i++) {
        s_pal_rgb[i][0] = (uint8_t)(vga6_to_8(s_pal6[i*3+0]) * b);
        s_pal_rgb[i][1] = (uint8_t)(vga6_to_8(s_pal6[i*3+1]) * b);
        s_pal_rgb[i][2] = (uint8_t)(vga6_to_8(s_pal6[i*3+2]) * b);
        s_pal_rgb[i][3] = 255;
    }
}

/* ── rendering ── */

/* Convert indexed framebuffer to RGBA and present with optional pixel offset. */
static void present_ex(int offset_x, int offset_y) {
    static Color rgba[SCREEN_W * SCREEN_H];
    float scale, sx, sy;
    Rectangle src_rec, dst_rec;
    int i;

    /* Tick music/sound/timers at 120Hz (same path as the game) */
    got_platform_pump();

    for (i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint8_t idx = s_screen[i];
        rgba[i].r = s_pal_rgb[idx][0];
        rgba[i].g = s_pal_rgb[idx][1];
        rgba[i].b = s_pal_rgb[idx][2];
        rgba[i].a = 255;
    }
    UpdateTexture(s_fb_tex, rgba);

    sx = (float)GetScreenWidth()  / SCREEN_W;
    sy = (float)GetScreenHeight() / SCREEN_H;
    scale = (sx < sy) ? sx : sy;

    src_rec.x = 0; src_rec.y = 0;
    src_rec.width  = SCREEN_W;
    src_rec.height = SCREEN_H;

    dst_rec.width  = SCREEN_W * scale;
    dst_rec.height = SCREEN_H * scale;
    dst_rec.x = (GetScreenWidth()  - dst_rec.width)  / 2.0f + offset_x * scale;
    dst_rec.y = (GetScreenHeight() - dst_rec.height) / 2.0f + offset_y * scale;

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(s_fb_tex, src_rec, dst_rec, (Vector2){0,0}, 0, WHITE);
    EndDrawing();
}

void present(void) { present_ex(0, 0); }

/* Fill a rectangle in the indexed framebuffer. */
void fill_rect(int x1, int y1, int x2, int y2, uint8_t color) {
    int x, y;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
    for (y = y1; y <= y2; y++)
        for (x = x1; x <= x2; x++)
            s_screen[y * SCREEN_W + x] = color;
}

/* ── framebuffer primitives ── */

/* Blit raw 8bpp image to framebuffer. */
void blit8(const uint8_t *src, int sw, int sh, int dx, int dy) {
    int y, x;
    for (y = 0; y < sh; y++) {
        int py = dy + y;
        if (py < 0 || py >= SCREEN_H) continue;
        for (x = 0; x < sw; x++) {
            int px = dx + x;
            if (px < 0 || px >= SCREEN_W) continue;
            s_screen[py * SCREEN_W + px] = src[y * sw + x];
        }
    }
}

/* Draw opaque sprite (sub_1B203 format: inline pixel data).
 * Format: [height words: byte_count_per_line] [interleaved commands + pixels]
 *   byte < 0x80: skip `byte` pixels
 *   byte >= 0x80: draw (byte & 0x7F) pixels from inline data */
void sprite_blit(const uint8_t *data, int dx, int dy,
                        int width, int height) {
    int line;
    const uint8_t *cmds = data + height * 2;

    for (line = 0; line < height; line++) {
        int py = dy + line;
        uint16_t total = rd16le(data + line * 2);
        int px = dx;
        int remaining;

        if (total == 0) continue;
        remaining = (int)total;

        while (remaining > 0) {
            uint8_t cmd = *cmds++;
            remaining--;

            if (cmd & 0x80) {
                int count = cmd & 0x7F;
                int j;
                for (j = 0; j < count; j++) {
                    if (remaining <= 0) break;
                    if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H)
                        s_screen[py * SCREEN_W + px] = *cmds;
                    cmds++;
                    remaining--;
                    px++;
                }
            } else {
                px += cmd;
            }
        }
    }
}

/* Apply shadow/remap sprite (sub_1B2F9 format).
 * Format: [height words: cmd_count_per_line] [command bytes]
 *   byte < 0x80: skip `byte` pixels
 *   byte >= 0x80: remap (byte & 0x7F) pixels through 256-byte LUT
 * Reads destination pixel, looks up in remap table, writes back. */
static void shadow_blit(const uint8_t *data, const uint8_t *remap,
                        int dx, int dy, int height, int lines) {
    int line;
    const uint8_t *cmds = data + height * 2; /* commands after header */

    for (line = 0; line < lines; line++) {
        int py = dy + line;
        uint16_t cmd_count = rd16le(data + line * 2);
        int px = dx;
        int c;

        if (cmd_count == 0) continue;

        for (c = 0; c < (int)cmd_count; c++) {
            uint8_t cmd = *cmds++;
            if (cmd & 0x80) {
                int count = cmd & 0x7F;
                int j;
                for (j = 0; j < count; j++) {
                    if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                        int idx = py * SCREEN_W + px;
                        s_screen[idx] = remap[s_screen[idx]];
                    }
                    px++;
                }
            } else {
                px += cmd;
            }
        }
    }
}

/* Draw tiled menu background (matching sub_17E86). */
void draw_menu_bg(void) {
    uint8_t *tile_bg  = gg_decompress_alloc(&s_gg, GG_TILE_BG);
    uint8_t *tile_bot = gg_decompress_alloc(&s_gg, GG_TILE_BOTTOM);
    int col, row;

    if (tile_bg) {
        for (col = 0; col < 10; col++)
            for (row = 0; row < 6; row++)
                blit8(tile_bg, 32, 32, col * 32, row * 32);
    }
    if (tile_bot) {
        for (col = 0; col < 10; col++)
            blit8(tile_bot, 32, 8, col * 32, 192);
    }

    free(tile_bg);
    free(tile_bot);
}

/* ── palette fade ── */

/* Fade from black to current palette. ~0.91 sec (64 steps at 70Hz).
 * Returns non-zero if user pressed a key. */
int do_fade_in(void) {
    double start = GetTime();
    double dur = (double)FADE_STEPS / 70.0;

    while (!WindowShouldClose()) {
        double t = (GetTime() - start) / dur;
        if (t >= 1.0) { set_brightness(1.0f); present(); break; }
        set_brightness((float)t);
        present();
        if (check_skip()) { set_brightness(1.0f); present(); return 1; }
    }
    return 0;
}

/* Fade to black. Returns non-zero if user pressed a key. */
int do_fade_out(void) {
    double start = GetTime();
    double dur = (double)FADE_STEPS / 70.0;

    while (!WindowShouldClose()) {
        double t = (GetTime() - start) / dur;
        if (t >= 1.0) { set_brightness(0.0f); present(); break; }
        set_brightness(1.0f - (float)t);
        present();
        if (check_skip()) { set_brightness(0.0f); present(); return 1; }
    }
    return 0;
}

/* Wait for keypress or timeout (in 70Hz ticks).
 * Returns non-zero if key pressed. */
static int wait_ticks(int ticks) {
    double start = GetTime();
    double dur = ticks / 70.0;

    while (!WindowShouldClose()) {
        present();
        if (check_skip()) return 1;
        if (GetTime() - start >= dur) return 0;
    }
    return 1;
}

/* ── DYSIN.GOT / Impulse logo (sub_166C2) ── */

/* Shows the Impulse/Software Creations logo from DYSIN.GOT.
 * File format: 64768 bytes = 64000 raw 8bpp pixels + 768 palette bytes.
 * Only exists in the registered version.
 * Returns non-zero if user wants to skip, 0 if shown or file not found. */
static int show_dysin(void) {
    FILE *f;
    uint8_t *data;
    long sz;

    f = fopen("DYSIN.GOT", "rb");
    if (!f) {
        fprintf(stderr, "[launcher] DYSIN.GOT not found, skipping Impulse logo\n");
        return 0;
    }

    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 64768) { fclose(f); return 0; }

    data = (uint8_t *)malloc(64768);
    if (!data) { fclose(f); return 0; }
    if (fread(data, 1, 64768, f) != 64768) {
        free(data);
        fclose(f);
        return 0;
    }
    fclose(f);

    /* Palette is at offset 64000 (768 bytes, VGA 6-bit) */
    memcpy(s_pal6, data + 64000, 768);

    /* Pixels are the first 64000 bytes (320x200 raw 8bpp) */
    memcpy(s_screen, data, 64000);
    free(data);

    set_brightness(0.0f);
    if (do_fade_in()) return 1;

    /* Wait ~5 sec (0x15E = 350 ticks at 70Hz) matching sub_166C2 */
    if (wait_ticks(350)) return 1;

    do_fade_out();
    return 0;
}

/* ── compiled sprite interpreter ── */

/* Execute one compiled sprite frame into s_screen (Mode 13h layout).
 *
 * Compiled sprites are x86 machine code that GOT.EXE calls with ES:DI = VGA.
 * Each frame is structured as:
 *   Preamble: xor cx,cx; add si,CODE_SIZE; mov bx,di  (8 bytes)
 *   Code:     instructions that write pixels to ES:DI
 *   Data:     inline pixel data consumed by rep movsw / movsb
 *   retf
 *
 * Instruction set (15 opcodes):
 *   33 C9         xor cx,cx       - clear count register
 *   81 C6 xxxx    add si,imm16    - advance data pointer
 *   81 C7 xxxx    add di,imm16    - advance screen position
 *   81 EE xxxx    sub si,imm16    - rewind data pointer (reuse rows)
 *   8B DF         mov bx,di       - save position (ignored)
 *   B8 xxxx       mov ax,imm16    - set pixel pair
 *   B1 xx         mov cl,imm8     - set repeat count
 *   B0 xx         mov al,imm8     - set single pixel
 *   F3 AB         rep stosw       - fill cx words
 *   F3 A5         rep movsw       - copy cx words from data
 *   AB            stosw           - write 2 pixels
 *   AA            stosb           - write 1 pixel
 *   A5            movsw           - copy 2 bytes from data
 *   A4            movsb           - copy 1 byte from data
 *   CB            retf            - end of frame
 */
static void exec_compiled_sprite(const uint8_t *frame, int frame_size) {
    int si, di, cx, ax;
    int pos = 0;
    int screen_size = SCREEN_W * SCREEN_H;

    /* The preamble always starts with:
     *   33 C9        xor cx,cx
     *   81 C6 xx xx  add si, CODE_SIZE
     *   8B DF        mov bx, di
     * We parse it to find where inline data starts (CODE_SIZE). */
    if (frame_size < 9) return;

    cx = 0;
    ax = 0;
    di = 0;

    /* Parse add si, CODE_SIZE from bytes 2-5 */
    if (frame[2] == 0x81 && frame[3] == 0xC6)
        si = frame[4] | (frame[5] << 8);
    else
        si = frame_size;  /* no data section */

    pos = 8;  /* skip preamble */

    while (pos < frame_size) {
        uint8_t op = frame[pos];

        if (op == 0xCB) {  /* retf */
            break;
        } else if (op == 0x81 && pos + 3 < frame_size) {
            uint8_t reg = frame[pos + 1];
            int imm = frame[pos + 2] | (frame[pos + 3] << 8);
            if (reg == 0xC7)      di += imm;       /* add di, imm16 */
            else if (reg == 0xC6) si = imm;         /* add si, imm16 (re-set) */
            else if (reg == 0xEE) si -= imm;        /* sub si, imm16 */
            pos += 4;
        } else if (op == 0xB8 && pos + 2 < frame_size) {  /* mov ax, imm16 */
            ax = frame[pos + 1] | (frame[pos + 2] << 8);
            pos += 3;
        } else if (op == 0xB1 && pos + 1 < frame_size) {  /* mov cl, imm8 */
            cx = (cx & 0xFF00) | frame[pos + 1];
            pos += 2;
        } else if (op == 0xB0 && pos + 1 < frame_size) {  /* mov al, imm8 */
            ax = (ax & 0xFF00) | frame[pos + 1];
            pos += 2;
        } else if (op == 0xF3 && pos + 1 < frame_size) {  /* rep prefix */
            uint8_t op2 = frame[pos + 1];
            if (op2 == 0xAB) {  /* rep stosw */
                int i;
                for (i = 0; i < cx; i++) {
                    if (di >= 0 && di + 1 < screen_size) {
                        s_screen[di]     = (uint8_t)(ax & 0xFF);
                        s_screen[di + 1] = (uint8_t)(ax >> 8);
                    }
                    di += 2;
                }
                cx = 0;
            } else if (op2 == 0xA5) {  /* rep movsw */
                int i;
                for (i = 0; i < cx; i++) {
                    if (di >= 0 && di + 1 < screen_size &&
                        si >= 0 && si + 1 < frame_size) {
                        s_screen[di]     = frame[si];
                        s_screen[di + 1] = frame[si + 1];
                    }
                    di += 2;
                    si += 2;
                }
                cx = 0;
            }
            pos += 2;
        } else if (op == 0xAB) {  /* stosw */
            if (di >= 0 && di + 1 < screen_size) {
                s_screen[di]     = (uint8_t)(ax & 0xFF);
                s_screen[di + 1] = (uint8_t)(ax >> 8);
            }
            di += 2;
            pos++;
        } else if (op == 0xAA) {  /* stosb */
            if (di >= 0 && di < screen_size)
                s_screen[di] = (uint8_t)(ax & 0xFF);
            di++;
            pos++;
        } else if (op == 0xA5) {  /* movsw */
            if (di >= 0 && di + 1 < screen_size &&
                si >= 0 && si + 1 < frame_size) {
                s_screen[di]     = frame[si];
                s_screen[di + 1] = frame[si + 1];
            }
            di += 2;
            si += 2;
            pos++;
        } else if (op == 0xA4) {  /* movsb */
            if (di >= 0 && di < screen_size &&
                si >= 0 && si < frame_size) {
                s_screen[di] = frame[si];
            }
            di++;
            si++;
            pos++;
        } else if (op == 0x33 && pos + 1 < frame_size && frame[pos+1] == 0xC9) {
            cx = 0;  /* xor cx, cx */
            pos += 2;
        } else if (op == 0x8B && pos + 1 < frame_size && frame[pos+1] == 0xDF) {
            pos += 2;  /* mov bx, di — ignored */
        } else {
            pos++;  /* unknown, skip */
        }
    }
}

/* Play a compiled sprite animation from a GRAPHICS.GOT chunk.
 *
 * Chunk format (matching sub_192BE):
 *   uint16le  frame_count
 *   frame_count * { uint16le frame_size, uint16le reserved }
 *   frame data (frame_count compiled sprite blobs, sizes from table)
 *
 * ticks_per_frame: delay between frames in 70Hz ticks (original uses 3).
 * Returns non-zero if user skipped. */
static int play_compiled_anim(int chunk_idx, int ticks_per_frame) {
    uint8_t *anim;
    int frame_count, i;
    int table_start, data_start, frame_offset;
    double frame_dur, next_time;

    anim = gg_decompress_alloc(&s_gg, chunk_idx);
    if (!anim) return 0;

    frame_count = rd16le(anim);
    if (frame_count <= 0) { free(anim); return 0; }

    table_start = 2;
    data_start = table_start + frame_count * 4;
    frame_offset = data_start;
    frame_dur = ticks_per_frame / 70.0;
    next_time = GetTime();

    for (i = 0; i < frame_count; i++) {
        int frame_size = rd16le(anim + table_start + i * 4);

        /* Wait until it's time for this frame */
        while (GetTime() < next_time) {
            present();
            got_platform_pump();
            if (WindowShouldClose()) { free(anim); return 1; }
            if (check_skip()) { free(anim); return 1; }
        }

        if (frame_offset + frame_size <= (int)s_gg.descs[chunk_idx].out_size)
            exec_compiled_sprite(anim + frame_offset, frame_size);

        present();
        frame_offset += frame_size;
        next_time += frame_dur;
    }

    free(anim);
    return 0;
}

/* ── opening screen 2 (sub_16783) ── */

/* Shows the Impulse logo from GRAPHICS.GOT with lightning animation.
 * Palette chunk 0x5B, image chunk 0x5C, animation chunk 0x5D.
 * Returns non-zero if user wants to skip. */
static int show_opening_screen(void) {
    uint8_t *bg;

    load_palette(GG_OPEN2_PAL);

    bg = gg_decompress_alloc(&s_gg, GG_OPEN2_BG);
    if (!bg) {
        fprintf(stderr, "[launcher] Failed to decompress chunk 0x5C, skipping opening screen\n");
        return 0;
    }
    fprintf(stderr, "[launcher] Showing opening screen (Impulse logo)\n");
    memcpy(s_screen, bg, SCREEN_W * SCREEN_H);
    free(bg);

    set_brightness(0.0f);
    if (do_fade_in()) return 1;

    /* Play compiled sprite animation (sub_192BE with chunk 0x5D, 3 ticks/frame).
     * This is a lightning bolt effect across the middle of the logo. */
    if (play_compiled_anim(GG_OPEN2_ANIM, 3)) return 1;

    /* Wait ~3 sec (0xD2 = 210 ticks at 70Hz) matching sub_16783 */
    if (wait_ticks(210)) return 1;

    do_fade_out();
    return 0;
}

/* ── title screen (sub_1643F) ── */

/* Deplanarize Mode X data where planes are 4 contiguous blocks. */
static void deplanar_blocked(const uint8_t *data, int stride, int lines,
                             uint8_t *linear) {
    int plane_size = stride * lines;
    const uint8_t *planes[4];
    planes[0] = data;
    planes[1] = data + plane_size;
    planes[2] = data + plane_size * 2;
    planes[3] = data + plane_size * 3;
    gg_deplanar(planes, stride, lines, linear);
}

static int show_title_screen(void) {
    uint8_t *planes[4];
    uint8_t *linear;
    uint8_t *strip_data, *strip_linear;
    int i, skipped;

    fprintf(stderr, "[launcher] Showing title screen\n");

    /* Load and set title palette */
    load_palette(GG_TITLE_PALETTE);
    set_brightness(0.0f);

    /* Load Mode X planes and deplanarize to get 320x400 image */
    for (i = 0; i < 4; i++) {
        planes[i] = gg_decompress_alloc(&s_gg, GG_TITLE_PLANE0 + i);
        if (!planes[i]) {
            int j;
            for (j = 0; j < i; j++) free(planes[j]);
            return 0;
        }
    }

    /* Each plane is 80 bytes/line * 400 lines = 32000 bytes.
     * The original uses Mode X 320x400 (no double-scan, CRTC reg 9 = 0x40),
     * displaying 328 scan lines of scenery + 72 lines of split screen bar.
     *
     * For our 320x200 display, deplanarize all 400 lines and sample every
     * other line (2:1 vertical scale) to approximate the CRT appearance.
     * Lines 0-163 show scenery (328 scan lines / 2), lines 164-199 are
     * the split screen area (filled with a solid color in the original). */
    linear = (uint8_t *)calloc(1, 320 * 400);
    if (linear) {
        int y;
        gg_deplanar((const uint8_t **)planes, 80, 400, linear);
        /* Sample every other line: display row y = source line y*2 */
        for (y = 0; y < 200; y++)
            memcpy(s_screen + y * 320, linear + (y * 2) * 320, 320);
        free(linear);
    }
    for (i = 0; i < 4; i++) free(planes[i]);

    /* Fade in title scenery (~0.91 sec) */
    skipped = do_fade_in();
    if (skipped) return 1;

    /* Wait ~7.8 sec (0x8C = 140 ticks) for the scenery to be admired */
    if (wait_ticks(140)) return 1;

    /* Load the title strip (chunk 0x28): 320x80 Mode X, 4 blocked planes.
     * 25600 bytes = 4 planes * 6400 bytes (80 stride * 80 lines).
     * This is the "God of Thunder" logo that overlays the top 80 source lines.
     * With 2:1 vertical scaling, it maps to display lines 0-39. */
    strip_data = gg_decompress_alloc(&s_gg, GG_TITLE_STRIP);
    if (strip_data) {
        strip_linear = (uint8_t *)calloc(1, 320 * 80);
        if (strip_linear) {
            int y;
            deplanar_blocked(strip_data, 80, 80, strip_linear);
            /* Sample every other line into top 40 display lines */
            for (y = 0; y < 40; y++)
                memcpy(s_screen + y * 320, strip_linear + (y * 2) * 320, 320);
            free(strip_linear);
        }
        free(strip_data);
    }

    /* Play thunder sound effect (sub_1643F @ 0x165D7), then shake. */
    launcher_play_sound(GG_THUNDER_VOC);

    /* SHAKE animation (sub_163F7): 105 frames of random screen offset.
     * The original shakes via VGA hardware scroll registers.
     * We simulate by offsetting the rendered texture position. */
    srand((unsigned)GetTime());
    for (i = 0; i < SHAKE_FRAMES; i++) {
        int ox = (rand() % 7) - 3;  /* -3 to +3 pixels */
        int oy = (rand() % 7) - 3;
        present_ex(ox, oy);
        if (WindowShouldClose()) return 1;
        if (check_skip()) return 1;
    }
    /* Return to stable position */
    present();

    /* Start music after shake (sub_1638C @ 0x16645). */
    if (!MU_MusicPlaying())
        launcher_start_music(GG_MENU_MUSIC);

    /* Wait ~3.9 sec (0x46 = 70 ticks) after shake */
    if (wait_ticks(70)) return 1;

    /* Fade out */
    do_fade_out();
    return 0;
}

/* ── credits (sub_16807) ── */

/* Show credits with shadow fade-in/out effect.
 * Each credit has a name sprite (chunks 43-51) and title sprite (67-75).
 * Fade tables: name remaps at chunks 52-66, title remaps at chunks 76-90.
 * Both sprites are drawn at position (16, 40) as shadow overlays on the
 * credits background (chunk 0x2A).
 *
 * Animation per credit:
 *   Slide in:  15 steps from lightest to darkest remap (fade=0..14)
 *   Hold:      ~7.8 sec (140 ticks)
 *   Slide out: 15 steps from darkest to lightest remap (fade=14..0)
 *   Pause:     ~1.9 sec (35 ticks)
 */
static int show_credits(void) {
    uint8_t *bg_data;
    int credit_idx;
    fprintf(stderr, "[launcher] Showing credits\n");

    /* Load credits palette and background */
    load_palette(GG_CREDITS_PAL);

    bg_data = gg_decompress_alloc(&s_gg, GG_CREDITS_BG);
    if (!bg_data) return 0;

    memcpy(s_screen, bg_data, SCREEN_W * SCREEN_H);
    set_brightness(0.0f);
    if (do_fade_in()) { free(bg_data); return 1; }

    for (credit_idx = 0; credit_idx < NUM_CREDITS; credit_idx++) {
        int name_chunk  = GG_CREDIT_NAME0  + credit_idx;
        int title_chunk = GG_CREDIT_TITLE0 + credit_idx;
        uint8_t *name_data  = gg_decompress_alloc(&s_gg, name_chunk);
        uint8_t *title_data = gg_decompress_alloc(&s_gg, title_chunk);
        int name_h  = (name_data  && name_chunk < s_gg.chunk_count) ? s_gg.descs[name_chunk].height : 0;
        int title_h = (title_data && title_chunk < s_gg.chunk_count) ? s_gg.descs[title_chunk].height : 0;
        int step;

        if (!name_data || !title_data) {
            free(name_data);
            free(title_data);
            continue;
        }

        /* Slide in: step 0 (lightest shadow) to 14 (darkest shadow) */
        for (step = 0; step < NUM_FADE_STEPS; step++) {
            int name_remap_chunk  = GG_CREDIT_REMAP0   + step;
            int title_remap_chunk = GG_CREDIT_REMAP2_0 + step;
            uint8_t *name_remap  = gg_decompress_alloc(&s_gg, name_remap_chunk);
            uint8_t *title_remap = gg_decompress_alloc(&s_gg, title_remap_chunk);

            /* Restore background */
            memcpy(s_screen, bg_data, SCREEN_W * SCREEN_H);

            /* Apply shadow sprites */
            if (name_remap)
                shadow_blit(name_data, name_remap, 16, 40, name_h, name_h);
            if (title_remap)
                shadow_blit(title_data, title_remap, 16, 40, title_h, title_h);

            free(name_remap);
            free(title_remap);

            present();

            /* 4 ticks (~57ms) per step */
            if (wait_ticks(4)) {
                free(name_data); free(title_data); free(bg_data);
                return 1;
            }
        }

        /* Hold at darkest shadow: ~7.8 sec (0x8C = 140 ticks) */
        {
            uint8_t *nr = gg_decompress_alloc(&s_gg, GG_CREDIT_REMAP0 + NUM_FADE_STEPS - 1);
            uint8_t *tr = gg_decompress_alloc(&s_gg, GG_CREDIT_REMAP2_0 + NUM_FADE_STEPS - 1);
            memcpy(s_screen, bg_data, SCREEN_W * SCREEN_H);
            if (nr) shadow_blit(name_data, nr, 16, 40, name_h, name_h);
            if (tr) shadow_blit(title_data, tr, 16, 40, title_h, title_h);
            free(nr); free(tr);
            present();

            if (wait_ticks(140)) {
                free(name_data); free(title_data); free(bg_data);
                return 1;
            }
        }

        /* Slide out: step 14 (darkest) to 0 (lightest) */
        for (step = NUM_FADE_STEPS - 1; step >= 0; step--) {
            uint8_t *name_remap  = gg_decompress_alloc(&s_gg, GG_CREDIT_REMAP0 + step);
            uint8_t *title_remap = gg_decompress_alloc(&s_gg, GG_CREDIT_REMAP2_0 + step);

            memcpy(s_screen, bg_data, SCREEN_W * SCREEN_H);
            if (name_remap)
                shadow_blit(name_data, name_remap, 16, 40, name_h, name_h);
            if (title_remap)
                shadow_blit(title_data, title_remap, 16, 40, title_h, title_h);

            free(name_remap);
            free(title_remap);

            present();

            /* 3 ticks (~43ms) per step */
            if (wait_ticks(3)) {
                free(name_data); free(title_data); free(bg_data);
                return 1;
            }
        }

        free(name_data);
        free(title_data);

        /* Clear to background and pause ~1.9 sec (0x23 = 35 ticks) */
        memcpy(s_screen, bg_data, SCREEN_W * SCREEN_H);
        present();
        if (wait_ticks(35)) { free(bg_data); return 1; }
    }

    /* Final hold after all credits: ~3.9 sec */
    memcpy(s_screen, bg_data, SCREEN_W * SCREEN_H);
    present();
    if (wait_ticks(70)) { free(bg_data); return 1; }

    free(bg_data);
    do_fade_out();
    return 0;
}

/* ── menu system (matching in-game select_option from panel.c) ── */

/* Compute menu box geometry for 32x32 frame tiles.
 * Content area is sized from items (title floats over frame border).
 * Both w and h are exact multiples of 32 so frame tiles align. */
static void menu_calc(const char *title, const char **items, int count,
                      int *ox1, int *oy1, int *ox2, int *oy2) {
    int w, h, i, tw;

    /* Width from items: 24px cursor zone + text pixels.
     * Cursor zone: 4px left pad + 16px cursor + 4px gap to text. */
    w = 0;
    for (i = 0; i < count; i++) {
        int l = (int)strlen(items[i]);
        if (l > w) w = l;
    }
    w = (w * 8) + 24;

    /* Widen for title if needed (GOT.EXE sub_18271 uses max of items+pad, title) */
    tw = (int)strlen(title) * 8;
    if (tw > w) w = tw;

    h = (count * 16) + 32;   /* 16px per item, 16px top/bottom padding */

    /* Round up to multiples of 32 for tile alignment */
    w = ((w + 31) / 32) * 32;
    h = ((h + 31) / 32) * 32;

    /* Center in 320x192 (bottom 8px is tile strip area) */
    if (ox1) *ox1 = (320 - w) / 2;
    if (oy1) *oy1 = (192 - h) / 2;
    if (ox2) *ox2 = (320 - w) / 2 + w - 1;
    if (oy2) *oy2 = (192 - h) / 2 + h - 1;
}

/* Draw 9-patch frame around content area using cached 32x32 sprites.
 * Layout from sub_17939: TL/Top/TR / Left/Fill/Right / BL/Bot/BR.
 * The border extends 32 pixels outside the content area on every side. */
void draw_menu_frame(int x1, int y1, int x2, int y2) {
    int cw = x2 - x1 + 1;
    int ch = y2 - y1 + 1;
    int cols = (cw + 31) / 32;
    int rows = (ch + 31) / 32;
    int fw = cols * 32;
    int fh = rows * 32;
    int i, j;

    /* Fallback to flat fill if frame sprites not loaded */
    for (i = 0; i < 9; i++)
        if (!s_frame[i]) { fill_rect(x1, y1, x2, y2, MENU_BG_COLOR); return; }

    /* Interior fill (chunk 11) */
    for (j = 0; j < rows; j++)
        for (i = 0; i < cols; i++)
            sprite_blit(s_frame[4], x1 + i * 32, y1 + j * 32, 32, 32);

    /* Top border (chunk 8) */
    for (i = 0; i < cols; i++)
        sprite_blit(s_frame[1], x1 + i * 32, y1 - 32, 32, 32);

    /* Bottom border (chunk 14) */
    for (i = 0; i < cols; i++)
        sprite_blit(s_frame[7], x1 + i * 32, y1 + fh, 32, 32);

    /* Left border (chunk 10) */
    for (j = 0; j < rows; j++)
        sprite_blit(s_frame[3], x1 - 32, y1 + j * 32, 32, 32);

    /* Right border (chunk 12) */
    for (j = 0; j < rows; j++)
        sprite_blit(s_frame[5], x1 + fw, y1 + j * 32, 32, 32);

    /* Corners */
    sprite_blit(s_frame[0], x1 - 32, y1 - 32, 32, 32);   /* TL */
    sprite_blit(s_frame[2], x1 + fw, y1 - 32, 32, 32);   /* TR */
    sprite_blit(s_frame[6], x1 - 32, y1 + fh, 32, 32);   /* BL */
    sprite_blit(s_frame[8], x1 + fw, y1 + fh, 32, 32);   /* BR */
}

/* Draw the menu box, title, and items into s_screen (without cursor).
 * Returns the box coordinates via pointers for cursor positioning.
 * Matches the in-game select_option() layout from panel.c. */
static void draw_menu_base(const char *title, const char **items,
                           int count, int sel,
                           int *out_x1, int *out_y1) {
    int x1, y1, x2, y2, i, tw;

    draw_menu_bg();
    menu_calc(title, items, count, &x1, &y1, &x2, &y2);
    draw_menu_frame(x1, y1, x2, y2);

    /* Title: centered horizontally at y1+4 (no shadow — matches select_option) */
    tw = font_width(title);
    font_print((320 - tw) / 2, y1 + 4, title, MENU_TITLE_COLOR);

    /* Items at x1+24, y1+28+i*16 — all same color, cursor shows selection.
     * 24px offset = 4px pad + 16px cursor + 4px gap. */
    for (i = 0; i < count; i++) {
        font_print(x1 + 24, (y1 + 28) + (i * 16), items[i], MENU_NORM_COLOR);
    }

    if (out_x1) *out_x1 = x1;
    if (out_y1) *out_y1 = y1;
}

/* Draw the full menu scene including animated cursor. */
static void draw_menu_scene(const char *title, const char **items,
                            int count, int sel) {
    int x1, y1, cx, cy;
    double now;

    draw_menu_base(title, items, count, sel, &x1, &y1);

    /* Animated cursor at x1+2, y1+28+sel*16 */
    cx = x1 + 2;
    cy = y1 + 28 + (sel * 16);
    now = GetTime();

    /* Advance frame every ~83ms (10 timer ticks at 120Hz) */
    if (now - s_cursor_time > 10.0 / 120.0) {
        s_cursor_frame = (s_cursor_frame + 1) & 3;
        s_cursor_time = now;
    }
    if (s_cursor[s_cursor_frame])
        sprite_blit(s_cursor[s_cursor_frame], cx, cy, 16, 16);
}

/* Hammer smack animation (matching panel.c hammer_smack).
 * Cursor slides right 4×2px, plays CLANG, then slides back. */
static void hammer_smack(const char *title, const char **items,
                         int count, int sel) {
    int x1, y1, cx, cy, i;

    menu_calc(title, items, count, &x1, &y1, NULL, NULL);
    cx = x1 + 2;
    cy = y1 + 28 + (sel * 16);

    /* Slide right: 4 steps, +2px each, pause 3 ticks (~25ms) */
    for (i = 0; i < 4; i++) {
        draw_menu_base(title, items, count, sel, NULL, NULL);
        cx += 2;
        if (s_cursor[0])
            sprite_blit(s_cursor[0], cx, cy, 16, 16);
        present();
        if (WindowShouldClose()) return;
        { double t0 = GetTime(); while (GetTime() - t0 < 3.0/120.0) got_platform_pump(); }
    }

    launcher_play_clang();

    /* Slide left: 4 steps, -2px each, pause 3 ticks */
    for (i = 0; i < 4; i++) {
        draw_menu_base(title, items, count, sel, NULL, NULL);
        cx -= 2;
        if (s_cursor[0])
            sprite_blit(s_cursor[0], cx, cy, 16, 16);
        present();
        if (WindowShouldClose()) return;
        { double t0 = GetTime(); while (GetTime() - t0 < 3.0/120.0) got_platform_pump(); }
    }
}

/* Run a menu. If is_main_menu is true, starts/continues background music.
 * Returns 0-based index or -1 for escape/quit. */
static int run_menu_ex(const char *title, const char **items, int initial_sel,
                       int is_main_menu) {
    int count = 0, sel, key;

    while (items[count]) count++;
    sel = initial_sel;
    if (sel < 0 || sel >= count) sel = 0;

    /* Load menu palette */
    load_palette(GG_PALETTE_MENU);

    /* Start/continue menu music (chunk 0x22) if this is the main menu */
    if (is_main_menu && !MU_MusicPlaying())
        launcher_start_music(GG_MENU_MUSIC);

    /* Play WOOP on menu open (matches select_option) */
    launcher_play_woop();

    /* Initial draw + fade in */
    draw_menu_scene(title, items, count, sel);
    set_brightness(0.0f);
    do_fade_in();

    while (!WindowShouldClose()) {
        draw_menu_scene(title, items, count, sel);
        present();

        key = GetKeyPressed();
        if (key == KEY_UP || key == KEY_W) {
            sel--; if (sel < 0) sel = count - 1;
            launcher_play_woop();
        }
        if (key == KEY_DOWN || key == KEY_S) {
            sel++; if (sel >= count) sel = 0;
            launcher_play_woop();
        }
        if (key == KEY_ENTER || key == KEY_SPACE) {
            hammer_smack(title, items, count, sel);
            return sel;
        }
        if (key == KEY_ESCAPE) return -1;
    }
    return -1;
}

int run_menu(const char *title, const char **items, int initial_sel) {
    return run_menu_ex(title, items, initial_sel, 0);
}

/* Quit confirmation. */
static int confirm_quit(void) {
    static const char *yn[] = { "Yes", "No", NULL };
    int r = run_menu("Quit to DOS?", yn, 1);
    return (r == 0);
}

/* ── public API ── */

int launcher_init(void) {
    Image img;

    s_gg_loaded = (gg_load(&s_gg, "GRAPHICS.GOT") == 0);
    fprintf(stderr, "[launcher] GRAPHICS.GOT loaded: %s (%d chunks)\n",
            s_gg_loaded ? "yes" : "no", s_gg.chunk_count);

    if (!IsWindowReady()) {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(SCREEN_W * SCALE, SCREEN_H * SCALE, "God of Thunder");
    }
    SetTargetFPS(60);
    SetExitKey(0);  /* Don't let ESC close the window; we handle it ourselves */

    img = GenImageColor(SCREEN_W, SCREEN_H, BLACK);
    s_fb_tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(s_fb_tex, TEXTURE_FILTER_POINT);

    memset(s_screen, 0, sizeof(s_screen));
    memset(s_pal6, 0, sizeof(s_pal6));
    set_brightness(0.0f);

    /* Load bitmap font for menu text rendering */
    if (s_gg_loaded) font_load();

    /* Cache cursor animation sprites (chunks 29-32, 16x16 opaque) */
    if (s_gg_loaded) {
        int i;
        for (i = 0; i < 4; i++)
            s_cursor[i] = gg_decompress_alloc(&s_gg, GG_CURSOR0 + i);
    }

    /* Cache frame border sprites (chunks 7-15, 32x32 opaque) */
    if (s_gg_loaded) {
        int i;
        for (i = 0; i < 9; i++)
            s_frame[i] = gg_decompress_alloc(&s_gg, GG_FRAME_TL + i);
    }

    /* Initialize audio using the same path as the game (sbfx_init).
     * This calls SB_Init + sets music/sound flags + got_platform_audio_init. */
    if (sbfx_init()) {
        s_audio_ready = 1;
        fprintf(stderr, "[launcher] Audio initialized via sbfx_init\n");
    } else {
        fprintf(stderr, "[launcher] Audio init failed (sbfx_init returned 0)\n");
    }

    /* Load menu sound effects (WOOP, CLANG) from GOTRES.DAT */
    launcher_load_menu_sounds();

    return 0;
}

int launcher_run(void) {
    int menu_sel;

    if (!s_gg_loaded)
        goto episode_select;

    /* Drain any stale key events from window creation. */
    drain_input();
    fprintf(stderr, "[launcher] Starting opening sequence\n");

    /* ── Opening sequence (matching sub_15DA8) ──
     * Flow: DYSIN.GOT → opening screen 2 → title → credits → menu
     * Any keypress during the sequence jumps to the main menu. */
    {
        int skipped = 0;

        if (WindowShouldClose()) return 0;

        /* DYSIN.GOT / Impulse logo (sub_166C2) - only if file exists */
        if (!skipped) skipped = show_dysin();

        /* Opening screen 2 (sub_16783) */
        if (!skipped && !WindowShouldClose())
            skipped = show_opening_screen();

        /* Title screen with shake (sub_1643F) - starts music after shake */
        if (!skipped && !WindowShouldClose())
            skipped = show_title_screen();

        /* Credits (sub_16807) */
        if (!skipped && !WindowShouldClose())
            skipped = show_credits();
    }

    if (WindowShouldClose()) return 0;

    /* ── Main menu loop (sub_190C1) ── */
    while (!WindowShouldClose()) {
        menu_sel = run_menu_ex("God of Thunder Menu", main_menu_items, 0, 1);

        switch (menu_sel) {
        case 0:  /* Play Game → episode selection */
            goto episode_select;
        case 1:  /* Credits */
            show_credits();
            break;
        case 2:  /* Extras */
            launcher_extras_menu();
            break;
        case 3:  /* Quit */
        case -1: /* Escape */
            if (confirm_quit()) return 0;
            break;
        }
    }
    return 0;

episode_select:
    while (!WindowShouldClose()) {
        int ep = run_menu("Play Which Game?", episode_names, 0);
        if (ep >= 0 && ep <= 2) {
            do_fade_out();
            return ep + 1;
        }
        if (ep == -1) {
            /* Back to main menu */
            if (!s_gg_loaded) return 0;
            while (!WindowShouldClose()) {
                menu_sel = run_menu_ex("God of Thunder Menu", main_menu_items, 0, 1);
                switch (menu_sel) {
                case 0:  goto episode_select;
                case 1:  show_credits(); break;
                case 2:  launcher_extras_menu(); break;
                case 3:
                case -1: if (confirm_quit()) return 0; break;
                }
            }
        }
    }
    return 0;
}

void launcher_shutdown(void) {
    int i;

    /* Stop music and sound before releasing resources */
    launcher_stop_music();
    SB_StopSound();

    free(s_font);           s_font = NULL;
    free(s_snd_woop);       s_snd_woop = NULL;
    free(s_snd_clang);      s_snd_clang = NULL;

    for (i = 0; i < 4; i++) { free(s_cursor[i]); s_cursor[i] = NULL; }
    for (i = 0; i < 9; i++) { free(s_frame[i]); s_frame[i] = NULL; }

    if (s_gg_loaded) {
        gg_free(&s_gg);
        s_gg_loaded = 0;
    }
    UnloadTexture(s_fb_tex);
    /* Note: audio device is left open for the game to reuse. */
}
