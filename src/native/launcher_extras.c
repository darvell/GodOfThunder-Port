/* launcher_extras.c - Extras menu viewers for the native launcher
 *
 * Re-release bonus features: Sound Test, Music Player, Sprite Viewer,
 * and Map Viewer.  Uses launcher's existing UI framework.
 */
#include "launcher_extras.h"
#include "graphics_got.h"
#include "raylib.h"

#include "digisnd.h"
#include "mu_man.h"
#include "res_man.h"
#include "level.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── from mu_man.c ── */
extern long MU_TicksElapsed;
extern long MU_DataLeft;

/* ── from launcher.c / platform ── */
void got_platform_pump(void);

/* Menu palette chunk index (from launcher.c) */
#define GG_PALETTE_MENU 6

/* Menu colors (matching launcher.c) */
#define MENU_TITLE_COLOR  54
#define MENU_NORM_COLOR   14
#define MENU_SHADOW_COLOR 223

/* ── ACTOR_NFO: on-disk actor metadata (first 40 bytes at resource+5120) ── */
/* Matches the ACTOR struct from game_define.h */
typedef struct {
    uint8_t move;
    uint8_t width;
    uint8_t height;
    uint8_t directions;
    uint8_t frames;
    uint8_t frame_speed;
    uint8_t frame_sequence[4];
    uint8_t speed;
    uint8_t size_x;
    uint8_t size_y;
    uint8_t strength;
    uint8_t health;
    uint8_t num_moves;
    uint8_t shot_type;
    uint8_t shot_pattern;
    uint8_t shots_allowed;
    uint8_t solid;
    uint8_t flying;
    uint8_t rating;
    uint8_t type;
    char    name[9];
    uint8_t func_num;
    uint8_t func_pass;
    int16_t magic_hurts;
    uint8_t future1[4];
} actor_nfo_t;

/* ── resource loading helper ── */

static uint8_t *res_load(const char *name, long *out_size) {
    char *lzss_buf;
    uint8_t *data;
    int idx;

    lzss_buf = (char *)malloc(18000);
    if (!lzss_buf) return NULL;

    res_init(lzss_buf);
    if (res_open("GOTRES.DAT") < 0) {
        free(lzss_buf);
        return NULL;
    }

    idx = res_find_name(name);
    if (idx < 0) {
        res_close();
        free(lzss_buf);
        return NULL;
    }
    if (out_size) *out_size = (long)res_header[idx].original_size;

    data = (uint8_t *)res_falloc_read(name);
    res_close();
    free(lzss_buf);
    return data;
}

/* ── planar sprite renderer ── */

/* Blit Mode X planar sprite into s_screen.
 * Format: 4 planes of (w_bytes * h) bytes each, contiguous.
 * Treats colors 0 and 15 as transparent (matching game's xput). */
static void blit_planar(const uint8_t *planes, int w_bytes, int h,
                        int dx, int dy) {
    int plane_sz = w_bytes * h;
    int p, row, bx;
    for (p = 0; p < 4; p++) {
        const uint8_t *plane = planes + p * plane_sz;
        for (row = 0; row < h; row++) {
            int py = dy + row;
            if (py < 0 || py >= SCREEN_H) continue;
            for (bx = 0; bx < w_bytes; bx++) {
                uint8_t v = plane[row * w_bytes + bx];
                int px = dx + bx * 4 + p;
                if (px < 0 || px >= SCREEN_W) continue;
                if (v == 0 || v == 15) continue;
                s_screen[py * SCREEN_W + px] = v;
            }
        }
    }
}

/* Blit Mode X planar sprite OPAQUE (no transparency skip) into s_screen. */
static void blit_planar_opaque(const uint8_t *planes, int w_bytes, int h,
                               int dx, int dy) {
    int plane_sz = w_bytes * h;
    int p, row, bx;
    for (p = 0; p < 4; p++) {
        const uint8_t *plane = planes + p * plane_sz;
        for (row = 0; row < h; row++) {
            int py = dy + row;
            if (py < 0 || py >= SCREEN_H) continue;
            for (bx = 0; bx < w_bytes; bx++) {
                uint8_t v = plane[row * w_bytes + bx];
                int px = dx + bx * 4 + p;
                if (px < 0 || px >= SCREEN_W) continue;
                s_screen[py * SCREEN_W + px] = v;
            }
        }
    }
}

/* ── chunky → planar conversion (matches make_mask() from game/image.c) ── */

/* Convert chunky (row-major) sprite data to Mode X planar format.
 * Caller must free the returned buffer. Returns NULL on failure.
 * Output is 4 planes of (w_bytes * h) bytes each. */
static uint8_t *chunky_to_planar(const uint8_t *chunky, int w, int h) {
    int w_bytes = (w + 3) / 4;
    int plane_sz = w_bytes * h;
    uint8_t *planes;
    int p, row, bx;

    planes = (uint8_t *)malloc((size_t)plane_sz * 4);
    if (!planes) return NULL;

    for (p = 0; p < 4; p++) {
        for (row = 0; row < h; row++) {
            for (bx = 0; bx < w_bytes; bx++) {
                int px = bx * 4 + p;
                uint8_t v = 0;
                if (px < w)
                    v = chunky[row * w + px];
                planes[p * plane_sz + row * w_bytes + bx] = v;
            }
        }
    }
    return planes;
}

/* ── chunky sprite renderer (for actor data, with scaling) ── */

/* Blit chunky 16x16 sprite scaled NxN into s_screen.
 * Transparency: skip pixels with value 0 or 15. */
static void blit_chunky_scaled(const uint8_t *data, int w, int h,
                                int dx, int dy, int scale) {
    int row, col, sy, sx;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            uint8_t v = data[row * w + col];
            if (v == 0 || v == 15) continue;
            for (sy = 0; sy < scale; sy++) {
                int py = dy + row * scale + sy;
                if (py < 0 || py >= SCREEN_H) continue;
                for (sx = 0; sx < scale; sx++) {
                    int px = dx + col * scale + sx;
                    if (px < 0 || px >= SCREEN_W) continue;
                    s_screen[py * SCREEN_W + px] = v;
                }
            }
        }
    }
}

/* ── palette helpers ── */

static void restore_menu_palette(void) {
    uint8_t *d = gg_decompress_alloc(&s_gg, GG_PALETTE_MENU);
    if (d) { load_pal6_raw(d); free(d); }
}

static int load_game_palette(void) {
    uint8_t *pal;
    int i;
    pal = res_load("palette", NULL);
    if (!pal) {
        fprintf(stderr, "[extras] Failed to load game palette\n");
        return 0;
    }
    for (i = 0; i < 768; i++)
        pal[i] >>= 2;
    load_pal6_raw(pal);
    free(pal);
    return 1;
}

/* ── key buffer drain (prevents ESC cascade between screens) ── */

static void drain_keys(void) {
    while (GetKeyPressed() != 0) {}
}

/* ── common draw helpers ── */

/* Viewer frame: content rect sized to fit inside 320x200 with 32px borders.
 * Content area: x=48..271, y=40..167 → 224x128.
 * Borders extend 32px outside, reaching x=16..303, y=8..199. */
static void draw_viewer_bg(const char *title) {
    draw_menu_bg();
    draw_menu_frame(48, 40, 271, 167);
    font_print_shadow((SCREEN_W - font_width(title)) / 2, 44, title,
                      MENU_TITLE_COLOR, MENU_SHADOW_COLOR);
}

static void draw_nav_hints(const char *hints) {
    font_print_shadow((SCREEN_W - font_width(hints)) / 2, 190,
                      hints, MENU_NORM_COLOR, MENU_SHADOW_COLOR);
}

/* ════════════════════════════════════════════════════════════════════
 * 1. SOUND TEST
 * ════════════════════════════════════════════════════════════════════ */

static const char *snd_names[16] = {
    "OW", "GULP", "SWISH", "YAH", "ELECTRIC", "THUNDER",
    "DOOR", "FALL", "ANGEL", "WOOP", "DEAD", "BRAAPP",
    "WIND", "PUNCH1", "CLANG", "EXPLODE"
};

static void extras_sound_test(void) {
    uint8_t *snd_data;
    uint8_t *sounds[16];
    int      snd_lens[16];
    int      i, cur = 0, key, faded_in = 0;
    uint32_t offset;

    snd_data = res_load("DIGSOUND", NULL);
    if (!snd_data) {
        fprintf(stderr, "[extras] Can't load DIGSOUND\n");
        return;
    }

    /* Parse HEADER: 16 entries of {uint32 offset, uint32 length} = 128 bytes */
    {
        uint8_t *p = snd_data;
        uint32_t lengths[16];
        for (i = 0; i < 16; i++) {
            p += 4;
            memcpy(&lengths[i], p, 4);
            p += 4;
        }
        offset = 0;
        for (i = 0; i < 16; i++) {
            snd_lens[i] = (int)lengths[i];
            sounds[i] = (uint8_t *)malloc(snd_lens[i]);
            if (sounds[i]) memcpy(sounds[i], p + offset, snd_lens[i]);
            offset += lengths[i];
        }
    }
    free(snd_data);

    restore_menu_palette();
    set_brightness(0.0f);

    while (!WindowShouldClose()) {
        char buf[64];

        draw_viewer_bg("Sound Test");

        sprintf(buf, "Sound %d / 16", cur + 1);
        font_center(70, buf, MENU_NORM_COLOR);

        font_print_shadow((SCREEN_W - font_width(snd_names[cur])) / 2, 90,
                          snd_names[cur], MENU_TITLE_COLOR, MENU_SHADOW_COLOR);

        font_center(120, "ENTER/SPACE: Play", MENU_NORM_COLOR);
        draw_nav_hints("LEFT/RIGHT: Browse   ESC: Back");

        if (!faded_in) {
            set_brightness(0.0f);
            present();
            do_fade_in();
            faded_in = 1;
            drain_keys();
            continue;
        }

        present();

        key = GetKeyPressed();
        if (key == KEY_LEFT) {
            cur = (cur + 15) % 16;
            launcher_play_woop();
        }
        if (key == KEY_RIGHT) {
            cur = (cur + 1) % 16;
            launcher_play_woop();
        }
        if (key == KEY_ENTER || key == KEY_SPACE) {
            if (s_audio_ready && sounds[cur])
                SB_PlayVoc(sounds[cur], true);
        }
        if (key == KEY_ESCAPE) break;
    }

    SB_StopSound();
    for (i = 0; i < 16; i++) free(sounds[i]);
    drain_keys();
}

/* ════════════════════════════════════════════════════════════════════
 * 2. MUSIC PLAYER
 * ════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *res_name;
    const char *display;
} track_info_t;

static const track_info_t tracks[] = {
    { "OPENSONG",  "Opening / Menu"      },
    { "SONG1",     "Ep1: Overworld 1"    },
    { "SONG2",     "Ep1: Overworld 2"    },
    { "SONG3",     "Ep1: Overworld 3"    },
    { "SONG4",     "Ep1: Cave"           },
    { "BOSSSONG",  "Boss Battle"         },
    { "WINSONG",   "Victory"             },
    { "SONG21",    "Ep2: Overworld 1"    },
    { "SONG22",    "Ep2: Overworld 2"    },
    { "SONG23",    "Ep2: Overworld 3"    },
    { "SONG24",    "Ep2: Overworld 4"    },
    { "SONG25",    "Ep2: Overworld 5"    },
    { "SONG31",    "Ep3: Overworld 1"    },
    { "SONG32",    "Ep3: Overworld 2"    },
    { "SONG33",    "Ep3: Story"          },
    { "SONG34",    "Ep3: Overworld 4"    },
    { "SONG35",    "Ep3: Overworld 5"    },
    { "SONG36",    "Ep3: Prison"         },
};
#define NUM_TRACKS ((int)(sizeof(tracks) / sizeof(tracks[0])))

static int viz_loaded = 0;

static void viz_free(void) {
    viz_loaded = 0;
}

static void extras_music_player(void) {
    int cur = 0, key, playing = 0, faded_in = 0;
    uint8_t *song_buf = NULL;

    restore_menu_palette();
    set_brightness(0.0f);

    (void)viz_loaded;

    while (!WindowShouldClose()) {
        char buf[80];
        int bar_w;

        draw_viewer_bg("Music Player");

        sprintf(buf, "Track %d / %d", cur + 1, NUM_TRACKS);
        font_center(58, buf, MENU_NORM_COLOR);

        font_print_shadow((SCREEN_W - font_width(tracks[cur].display)) / 2, 72,
                          tracks[cur].display, MENU_TITLE_COLOR, MENU_SHADOW_COLOR);

        if (MU_MusicPlaying()) {
            font_center(90, ">> PLAYING >>", MENU_TITLE_COLOR);
            /* Progress bar */
            {
                int bar_x = 90, bar_y = 104, bar_max = 140;
                float pct = 0.0f;
                if (MU_DataLeft > 0 && MU_TicksElapsed > 0) {
                    long total = MU_DataLeft + MU_TicksElapsed * 3;
                    pct = 1.0f - (float)MU_DataLeft / (float)total;
                }
                if (pct < 0.0f) pct = 0.0f;
                if (pct > 1.0f) pct = 1.0f;
                bar_w = (int)(pct * bar_max);
                fill_rect(bar_x, bar_y, bar_x + bar_max, bar_y + 6, MENU_SHADOW_COLOR);
                if (bar_w > 0)
                    fill_rect(bar_x, bar_y, bar_x + bar_w, bar_y + 6, MENU_TITLE_COLOR);
            }

        } else {
            font_center(96, "-- STOPPED --", MENU_NORM_COLOR);
            /* Auto-advance to next track if we were playing */
            if (playing) {
                cur = (cur + 1) % NUM_TRACKS;
                {
                    long sz = 0;
                    uint8_t *data = res_load(tracks[cur].res_name, &sz);
                    if (data) {
                        free(song_buf);
                        song_buf = data;
                        MU_StartMusic((char *)song_buf, sz);
                    } else {
                        playing = 0;
                    }
                }
            }
        }

        font_center(140, "ENTER/SPACE: Play/Stop", MENU_NORM_COLOR);
        draw_nav_hints("LEFT/RIGHT: Browse   ESC: Back");

        if (!faded_in) {
            set_brightness(0.0f);
            present();
            do_fade_in();
            faded_in = 1;
            drain_keys();
            continue;
        }

        present();

        key = GetKeyPressed();
        if (key == KEY_LEFT) {
            cur = (cur + NUM_TRACKS - 1) % NUM_TRACKS;
            launcher_play_woop();
            /* If playing, switch to new track immediately */
            if (playing) {
                long sz = 0;
                uint8_t *data = res_load(tracks[cur].res_name, &sz);
                if (data) {
                    MU_MusicOff();
                    free(song_buf);
                    song_buf = data;
                    MU_StartMusic((char *)song_buf, sz);
                }
            }
        }
        if (key == KEY_RIGHT) {
            cur = (cur + 1) % NUM_TRACKS;
            launcher_play_woop();
            if (playing) {
                long sz = 0;
                uint8_t *data = res_load(tracks[cur].res_name, &sz);
                if (data) {
                    MU_MusicOff();
                    free(song_buf);
                    song_buf = data;
                    MU_StartMusic((char *)song_buf, sz);
                }
            }
        }
        if (key == KEY_ENTER || key == KEY_SPACE) {
            if (playing) {
                MU_MusicOff();
                free(song_buf);
                song_buf = NULL;
                playing = 0;
            } else {
                long sz = 0;
                uint8_t *data = res_load(tracks[cur].res_name, &sz);
                if (data) {
                    MU_MusicOff();
                    free(song_buf);
                    song_buf = data;
                    MU_StartMusic((char *)song_buf, sz);
                    playing = 1;
                }
            }
            launcher_play_clang();
        }
        if (key == KEY_ESCAPE) break;
    }

    MU_MusicOff();
    free(song_buf);
    drain_keys();
}

/* ════════════════════════════════════════════════════════════════════
 * 3. SPRITE VIEWER
 * ════════════════════════════════════════════════════════════════════ */

static const char *dir_names[] = { "Up", "Down", "Left", "Right" };

static void extras_sprite_viewer(void) {
    int valid_ids[110];
    int num_valid = 0;
    int cur = 0, frame = 0;
    int animating = 1, key, faded_in = 0;
    double anim_time;
    uint8_t *actor_data = NULL;
    actor_nfo_t nfo;
    int cur_loaded = -1;
    int i;

    if (!load_game_palette()) return;
    set_brightness(0.0f);

    /* Pre-scan valid actor IDs (ACTOR1 through ACTOR110) */
    {
        char *lzss_buf = (char *)malloc(18000);
        if (!lzss_buf) return;
        res_init(lzss_buf);
        if (res_open("GOTRES.DAT") < 0) {
            free(lzss_buf);
            return;
        }
        for (i = 1; i <= 110; i++) {
            char name[16];
            sprintf(name, "ACTOR%d", i);
            if (res_find_name(name) >= 0)
                valid_ids[num_valid++] = i;
        }
        res_close();
        free(lzss_buf);
    }

    if (num_valid == 0) {
        fprintf(stderr, "[extras] No actors found\n");
        restore_menu_palette();
        return;
    }

    memset(&nfo, 0, sizeof(nfo));
    anim_time = GetTime();

    while (!WindowShouldClose()) {
        char name_buf[16], info_buf[64];
        int seq_idx;
        int ndirs, nframes;

        /* Load actor on demand */
        if (cur_loaded != cur) {
            free(actor_data);
            sprintf(name_buf, "ACTOR%d", valid_ids[cur]);
            actor_data = res_load(name_buf, NULL);
            cur_loaded = cur;
            frame = 0;

            if (actor_data) {
                memcpy(&nfo, actor_data + 5120, sizeof(actor_nfo_t));
                nfo.name[8] = '\0';
                if (nfo.directions < 1) nfo.directions = 1;
                if (nfo.frames < 1) nfo.frames = 1;
            }
        }

        ndirs = nfo.directions;
        nframes = nfo.frames;

        /* Animate using frame_sequence (same as game engine) */
        if (animating && GetTime() - anim_time > 0.15) {
            frame = (frame + 1) % nframes;
            anim_time = GetTime();
        }

        seq_idx = nfo.frame_sequence[frame % 4] % nframes;

        draw_viewer_bg("Sprite Viewer");

        if (actor_data) {
            /* Show all directions side by side, each animating.
             * Scale 3x for single-dir, 2x for multi-dir. */
            int scale = (ndirs == 1) ? 3 : 2;
            int sprite_sz = 16 * scale;
            int spacing = 8;
            int total_w = ndirs * sprite_sz + (ndirs - 1) * spacing;
            int start_x = (SCREEN_W - total_w) / 2;
            int sprite_y = 60;
            int d;

            for (d = 0; d < ndirs; d++) {
                int frame_idx = d * 4 + seq_idx;
                const uint8_t *frame_data;
                int sx = start_x + d * (sprite_sz + spacing);

                if (frame_idx >= 16) frame_idx = 0;
                frame_data = actor_data + frame_idx * 256;

                blit_chunky_scaled(frame_data, 16, 16, sx, sprite_y, scale);

                /* Direction label below each sprite */
                if (ndirs > 1) {
                    static const char *dir_labels[] = {
                        "Up", "Down", "Left", "Right"
                    };
                    const char *lbl = (d < 4) ? dir_labels[d] : "?";
                    int lw = font_width(lbl);
                    font_print(sx + (sprite_sz - lw) / 2,
                               sprite_y + sprite_sz + 2,
                               lbl, MENU_NORM_COLOR);
                }
            }

            /* Actor info below the sprites */
            {
                int ty = sprite_y + sprite_sz + (ndirs > 1 ? 14 : 4);
                sprintf(info_buf, "%s  (ID %d)", nfo.name, valid_ids[cur]);
                font_center(ty, info_buf, MENU_TITLE_COLOR);
                ty += 12;

                sprintf(info_buf, "HP:%d  STR:%d  SPD:%d  Type:%d",
                        nfo.health, nfo.strength, nfo.speed, nfo.type);
                font_center(ty, info_buf, MENU_NORM_COLOR);
            }
        }

        sprintf(info_buf, "Actor %d / %d", cur + 1, num_valid);
        font_center(148, info_buf, MENU_NORM_COLOR);

        draw_nav_hints("LEFT/RIGHT: Browse  SPC: Anim  ESC: Back");

        if (!faded_in) {
            set_brightness(0.0f);
            present();
            do_fade_in();
            faded_in = 1;
            drain_keys();
            continue;
        }

        present();

        key = GetKeyPressed();
        if (key == KEY_LEFT) {
            cur = (cur + num_valid - 1) % num_valid;
            launcher_play_woop();
        }
        if (key == KEY_RIGHT) {
            cur = (cur + 1) % num_valid;
            launcher_play_woop();
        }
        if (key == KEY_SPACE) {
            animating = !animating;
            launcher_play_clang();
        }
        if (key == KEY_ESCAPE) break;
    }

    free(actor_data);
    restore_menu_palette();
    drain_keys();
}

/* ════════════════════════════════════════════════════════════════════
 * 4. MAP VIEWER
 * ════════════════════════════════════════════════════════════════════ */

/* Each episode has 3 areas/chapters (area 1, 2, 3).
 * SDAT{area} contains 120 levels × 512 bytes.
 * Episode 1 always uses BPICS1; ep2/3 use BPICS{area}. */

#define NUM_CHAPTERS 3
#define LEVELS_PER_CHAPTER 120

/* Simple actor sprite cache for map viewer */
#define ACTOR_CACHE_SIZE 32
typedef struct {
    int actor_id;
    uint8_t *planar;     /* 4-plane 16x16 planar data (256 bytes) */
} cached_actor_t;

static cached_actor_t actor_cache[ACTOR_CACHE_SIZE];
static int actor_cache_count = 0;

static void actor_cache_clear(void) {
    int i;
    for (i = 0; i < actor_cache_count; i++)
        free(actor_cache[i].planar);
    actor_cache_count = 0;
}

/* Load an actor's first frame (dir=0, frame=0) and convert to planar.
 * Returns pointer to cached planar data, or NULL on failure. */
static const uint8_t *actor_cache_get(int actor_id) {
    int i;
    uint8_t *raw, *planar;
    char name[16];

    /* Check cache */
    for (i = 0; i < actor_cache_count; i++) {
        if (actor_cache[i].actor_id == actor_id)
            return actor_cache[i].planar;
    }

    /* Load and convert */
    sprintf(name, "ACTOR%d", actor_id);
    raw = res_load(name, NULL);
    if (!raw) return NULL;

    /* Frame 0, dir 0 = first 256 bytes (chunky 16x16) */
    planar = chunky_to_planar(raw, 16, 16);
    free(raw);
    if (!planar) return NULL;

    /* Store in cache (evict oldest if full) */
    if (actor_cache_count >= ACTOR_CACHE_SIZE) {
        free(actor_cache[0].planar);
        memmove(&actor_cache[0], &actor_cache[1],
                sizeof(cached_actor_t) * (ACTOR_CACHE_SIZE - 1));
        actor_cache_count = ACTOR_CACHE_SIZE - 1;
    }
    actor_cache[actor_cache_count].actor_id = actor_id;
    actor_cache[actor_cache_count].planar = planar;
    actor_cache_count++;
    return planar;
}

static void extras_map_viewer(void) {
    int chapter = 1, level = 0, key, i, faded_in = 0;
    uint8_t *bpics = NULL;
    uint8_t *sdat = NULL;
    long bpics_size = 0;
    int cur_chapter = -1;

    if (!load_game_palette()) return;
    set_brightness(0.0f);

    while (!WindowShouldClose()) {
        LEVEL lev;
        char buf[80];
        int row, col;

        /* Load chapter data on demand */
        if (cur_chapter != chapter) {
            char bname[16], sname[16];

            free(bpics); bpics = NULL;
            free(sdat); sdat = NULL;
            actor_cache_clear();

            sprintf(sname, "SDAT%d", chapter);
            sdat = res_load(sname, NULL);
            if (!sdat) {
                fprintf(stderr, "[extras] No %s\n", sname);
                cur_chapter = chapter;
                level = 0;
                goto draw_empty;
            }

            /* Try chapter-specific BPICS, fall back to BPICS1 */
            sprintf(bname, "BPICS%d", chapter);
            bpics = res_load(bname, &bpics_size);
            if (!bpics) {
                bpics = res_load("BPICS1", &bpics_size);
            }
            if (!bpics) {
                fprintf(stderr, "[extras] No BPICS for chapter %d\n", chapter);
                free(sdat); sdat = NULL;
                cur_chapter = chapter;
                level = 0;
                goto draw_empty;
            }

            load_game_palette();
            set_brightness(1.0f);
            cur_chapter = chapter;
            level = 0;
        }

        if (!sdat || !bpics) {
draw_empty:
            memset(s_screen, 0, SCREEN_W * SCREEN_H);
            fill_rect(0, 192, SCREEN_W - 1, SCREEN_H - 1, 0);
            sprintf(buf, "Chapter %d  (no data)", chapter);
            font_print(4, 192, buf, MENU_NORM_COLOR);

            if (!faded_in) {
                set_brightness(0.0f);
                present();
                do_fade_in();
                faded_in = 1;
                drain_keys();
            } else {
                present();
            }

            key = GetKeyPressed();
            if (key == KEY_UP) {
                chapter--;
                if (chapter < 1) chapter = NUM_CHAPTERS;
                launcher_play_woop();
            }
            if (key == KEY_DOWN) {
                chapter++;
                if (chapter > NUM_CHAPTERS) chapter = 1;
                launcher_play_woop();
            }
            if (key == KEY_ESCAPE) break;
            continue;
        }

        /* Parse LEVEL struct from SDAT (512 bytes per level) */
        memcpy(&lev, sdat + level * 512, 512);

        /* Render 20x12 tile grid (320x192).
         * Matches build_screen() from game/back.c:
         *   1) Fill with color 0
         *   2) For each non-empty cell: draw bg_color TILE (opaque),
         *      then draw icon tile on top (with transparency).
         * bg_color is a TILE INDEX into BPICS, not a palette color. */
        memset(s_screen, 0, SCREEN_W * SCREEN_H);

        for (row = 0; row < LEVEL_ROWS; row++) {
            for (col = 0; col < LEVEL_COLUMNS; col++) {
                uint8_t icon = (uint8_t)lev.icon[row][col];
                uint8_t bg_tile = (uint8_t)lev.bg_color;
                long bg_offset, tile_offset;

                if (icon == 0) continue;

                /* Each tile in BPICS = 262 bytes (6-byte xput header + 256 bytes planar) */

                /* First pass: draw bg_color tile opaque (the ground fill) */
                bg_offset = (long)bg_tile * 262 + 6;
                if (bg_offset + 256 <= bpics_size)
                    blit_planar_opaque(bpics + bg_offset, 4, 16,
                                       col * 16, row * 16);

                /* Second pass: draw icon tile with transparency on top */
                tile_offset = (long)icon * 262 + 6;
                if (tile_offset + 256 > bpics_size) continue;

                blit_planar(bpics + tile_offset, 4, 16, col * 16, row * 16);
            }
        }

        /* Draw actor sprites at their grid positions */
        for (i = 0; i < LEVEL_MAX_ACTOR; i++) {
            int atype = (uint8_t)lev.actor_type[i];
            int aloc = (uint8_t)lev.actor_loc[i];
            int ax, ay;
            const uint8_t *planar;

            if (atype == 0) continue;

            ax = (aloc % 20) * 16;
            ay = (aloc / 20) * 16;

            planar = actor_cache_get(atype);
            if (planar) {
                /* Draw planar 16x16 actor sprite with transparency */
                blit_planar(planar, 4, 16, ax, ay);
            } else {
                /* Fallback: colored dot if actor can't be loaded */
                if (ax + 8 >= 2 && ax + 8 < SCREEN_W - 2 &&
                    ay + 8 >= 2 && ay + 8 < 192 - 2)
                    fill_rect(ax + 6, ay + 6, ax + 10, ay + 10, 20);
            }
        }

        /* Info bar at bottom */
        fill_rect(0, 192, SCREEN_W - 1, SCREEN_H - 1, 0);
        sprintf(buf, "Ch.%d  Level %03d  UP/DN:Chapter  L/R:Level",
                chapter, level);
        font_print(4, 192, buf, MENU_NORM_COLOR);

        if (!faded_in) {
            set_brightness(0.0f);
            present();
            do_fade_in();
            faded_in = 1;
            drain_keys();
            continue;
        }

        present();

        key = GetKeyPressed();
        if (key == KEY_LEFT) {
            level = (level + LEVELS_PER_CHAPTER - 1) % LEVELS_PER_CHAPTER;
            launcher_play_woop();
        }
        if (key == KEY_RIGHT) {
            level = (level + 1) % LEVELS_PER_CHAPTER;
            launcher_play_woop();
        }
        if (key == KEY_UP) {
            chapter--;
            if (chapter < 1) chapter = NUM_CHAPTERS;
            launcher_play_woop();
        }
        if (key == KEY_DOWN) {
            chapter++;
            if (chapter > NUM_CHAPTERS) chapter = 1;
            launcher_play_woop();
        }
        if (key == KEY_ESCAPE) break;
    }

    free(bpics);
    free(sdat);
    actor_cache_clear();
    restore_menu_palette();
    drain_keys();
}

/* ════════════════════════════════════════════════════════════════════
 * 5. SCRIPT VIEWER
 * ════════════════════════════════════════════════════════════════════ */

/* Script index entry */
typedef struct {
    int script_id;
    int start_offset;   /* byte offset of first line after |<id>\r */
    int end_offset;     /* byte offset of |STOP or next marker */
} script_entry_t;

/* Known script commands (from game/script.c scr_command[]) */
static const char *scr_commands[] = {
    "END", "GOTO", "GOSUB", "RETURN", "FOR", "NEXT",
    "IF", "ELSE", "RUN",
    "ADDJEWELS", "ADDHEALTH", "ADDMAGIC", "ADDKEYS",
    "ADDSCORE", "SAY", "ASK", "SOUND", "PLACETILE",
    "ITEMGIVE", "ITEMTAKE", "ITEMSAY", "SETFLAG", "LTOA",
    "PAUSE", "TEXT", "EXEC", "VISIBLE", "RANDOM",
    NULL
};

/* Syntax color palette indices (menu palette) */
#define SCR_COLOR_DEFAULT  14
#define SCR_COLOR_COMMAND  54
#define SCR_COLOR_STRING   32
#define SCR_COLOR_VARIABLE 44

static const char *speak_names[] = { "SPEAK1", "SPEAK2", "SPEAK3" };

/* Check if word at position is a known command */
static int is_script_command(const char *word, int len) {
    int i;
    for (i = 0; scr_commands[i]; i++) {
        if ((int)strlen(scr_commands[i]) == len &&
            strncmp(word, scr_commands[i], len) == 0)
            return 1;
    }
    return 0;
}

/* Parse SPEAK resource data into script entries.
 * Returns number of scripts found. Caller must free *out_entries. */
static int parse_speak_scripts(const uint8_t *data, long size,
                               script_entry_t **out_entries) {
    int capacity = 64, count = 0;
    script_entry_t *entries;
    long pos = 0;

    entries = (script_entry_t *)malloc(capacity * sizeof(script_entry_t));
    if (!entries) { *out_entries = NULL; return 0; }

    while (pos < size) {
        if (data[pos] == '|') {
            long num_start = pos + 1;
            long p = num_start;
            int script_id;

            /* Find end of this token */
            while (p < size && data[p] != 13 && data[p] != 10)
                p++;

            /* |EOF — end of resource */
            if (p - num_start == 3 &&
                strncmp((const char *)data + num_start, "EOF", 3) == 0)
                break;

            /* |STOP — closes previous script */
            if (p - num_start == 4 &&
                strncmp((const char *)data + num_start, "STOP", 4) == 0) {
                if (count > 0 && entries[count - 1].end_offset == 0)
                    entries[count - 1].end_offset = (int)pos;
                while (pos < size && data[pos] != 13) pos++;
                if (pos < size && data[pos] == 13) pos++;
                if (pos < size && data[pos] == 10) pos++;
                continue;
            }

            /* Try to parse number */
            script_id = 0;
            {
                long d = num_start;
                int got_digit = 0;
                while (d < p) {
                    if (data[d] >= '0' && data[d] <= '9') {
                        script_id = script_id * 10 + (data[d] - '0');
                        got_digit = 1;
                    } else {
                        got_digit = 0;
                        break;
                    }
                    d++;
                }
                if (!got_digit) {
                    while (pos < size && data[pos] != 13) pos++;
                    if (pos < size && data[pos] == 13) pos++;
                    if (pos < size && data[pos] == 10) pos++;
                    continue;
                }
            }

            /* Close previous script if still open */
            if (count > 0 && entries[count - 1].end_offset == 0)
                entries[count - 1].end_offset = (int)pos;

            /* Skip past the |<number>\r\n line */
            while (pos < size && data[pos] != 13) pos++;
            if (pos < size && data[pos] == 13) pos++;
            if (pos < size && data[pos] == 10) pos++;

            /* Grow if needed */
            if (count >= capacity) {
                capacity *= 2;
                entries = (script_entry_t *)realloc(entries,
                            capacity * sizeof(script_entry_t));
                if (!entries) { *out_entries = NULL; return 0; }
            }

            entries[count].script_id = script_id;
            entries[count].start_offset = (int)pos;
            entries[count].end_offset = 0;
            count++;
        } else {
            /* Skip to next line */
            while (pos < size && data[pos] != 13) pos++;
            if (pos < size && data[pos] == 13) pos++;
            if (pos < size && data[pos] == 10) pos++;
        }
    }

    /* Close last script if still open */
    if (count > 0 && entries[count - 1].end_offset == 0)
        entries[count - 1].end_offset = (int)size;

    *out_entries = entries;
    return count;
}

/* Count lines in a script (CR-delimited) */
static int count_script_lines(const uint8_t *data, int start, int end) {
    int lines = 0, pos = start;
    while (pos < end) {
        if (data[pos] == 13) lines++;
        pos++;
    }
    if (end > start && data[end - 1] != 13) lines++;
    return lines;
}

/* Get byte offset of the Nth line (0-indexed) within a script */
static int get_line_offset(const uint8_t *data, int start, int end,
                           int line_num) {
    int pos = start, line = 0;
    while (pos < end && line < line_num) {
        if (data[pos] == 13) {
            line++;
            pos++;
            if (pos < end && data[pos] == 10) pos++;
            continue;
        }
        pos++;
    }
    return pos;
}

/* Extract a line from script data into buf (stripping CR/LF) */
static int extract_line(const uint8_t *data, int pos, int end,
                        char *buf, int bufsize) {
    int len = 0;
    while (pos < end && data[pos] != 13) {
        if (data[pos] != 10 && len < bufsize - 1)
            buf[len++] = (char)data[pos];
        pos++;
    }
    buf[len] = '\0';
    return len;
}

/* Render a line of script text with syntax coloring */
static void render_script_line(int x, int y, const char *line,
                               int max_chars) {
    int i = 0, len = (int)strlen(line);
    int cx = x;
    char ch[2] = {0, 0};

    while (i < len && (cx - x) / 8 < max_chars) {
        uint8_t color = SCR_COLOR_DEFAULT;

        if (line[i] == '"') {
            /* Quoted string */
            color = SCR_COLOR_STRING;
            ch[0] = line[i];
            font_print(cx, y, ch, color);
            cx += 8; i++;
            while (i < len && line[i] != '"' && (cx - x) / 8 < max_chars) {
                ch[0] = line[i];
                font_print(cx, y, ch, color);
                cx += 8; i++;
            }
            if (i < len && line[i] == '"' && (cx - x) / 8 < max_chars) {
                ch[0] = line[i];
                font_print(cx, y, ch, color);
                cx += 8; i++;
            }
        } else if (line[i] == '@') {
            /* @variable */
            color = SCR_COLOR_VARIABLE;
            while (i < len && (line[i] == '@' ||
                   (line[i] >= 'A' && line[i] <= 'Z') ||
                   (line[i] >= 'a' && line[i] <= 'z') ||
                   (line[i] >= '0' && line[i] <= '9') ||
                   line[i] == '_') && (cx - x) / 8 < max_chars) {
                ch[0] = line[i];
                font_print(cx, y, ch, color);
                cx += 8; i++;
            }
        } else if (line[i] >= 'A' && line[i] <= 'Z') {
            /* Possible command keyword */
            int ws = i;
            while (i < len && ((line[i] >= 'A' && line[i] <= 'Z') ||
                               (line[i] >= 'a' && line[i] <= 'z')))
                i++;
            if (is_script_command(line + ws, i - ws))
                color = SCR_COLOR_COMMAND;
            {
                int j;
                for (j = ws; j < i && (cx - x) / 8 < max_chars; j++) {
                    ch[0] = line[j];
                    font_print(cx, y, ch, color);
                    cx += 8;
                }
            }
        } else {
            ch[0] = line[i];
            font_print(cx, y, ch, color);
            cx += 8; i++;
        }
    }
}

#define SCRIPT_VISIBLE_LINES 18
#define SCRIPT_MAX_CHARS     36
#define SCRIPT_TEXT_X        16
#define SCRIPT_TEXT_Y        36

static void extras_script_viewer(void) {
    int area = 0;   /* 0=SPEAK1, 1=SPEAK2, 2=SPEAK3 */
    int cur_script = 0, scroll_y = 0;
    int key, faded_in = 0;

    uint8_t *speak_data = NULL;
    long speak_size = 0;
    script_entry_t *entries = NULL;
    int num_scripts = 0, total_lines = 0;
    int cur_area = -1, cur_loaded_script = -1;

    restore_menu_palette();
    set_brightness(0.0f);

    while (!WindowShouldClose()) {
        char buf[80];
        int i;

        /* Load SPEAK resource on area change */
        if (cur_area != area) {
            free(speak_data); speak_data = NULL;
            free(entries); entries = NULL;
            num_scripts = 0;

            speak_data = res_load(speak_names[area], &speak_size);
            if (speak_data)
                num_scripts = parse_speak_scripts(speak_data, speak_size,
                                                  &entries);
            cur_area = area;
            cur_script = 0;
            scroll_y = 0;
            cur_loaded_script = -1;
        }

        /* Recount lines on script change */
        if (cur_loaded_script != cur_script && num_scripts > 0) {
            total_lines = count_script_lines(speak_data,
                entries[cur_script].start_offset,
                entries[cur_script].end_offset);
            scroll_y = 0;
            cur_loaded_script = cur_script;
        }

        /* Draw background and frame */
        draw_menu_bg();
        draw_menu_frame(8, 8, 311, 185);

        /* Title */
        sprintf(buf, "Script Viewer - %s", speak_names[area]);
        font_print_shadow((SCREEN_W - font_width(buf)) / 2, 12, buf,
                          MENU_TITLE_COLOR, MENU_SHADOW_COLOR);

        if (num_scripts == 0) {
            font_center(90, "No scripts found", MENU_NORM_COLOR);
        } else {
            int sid = entries[cur_script].script_id;

            /* Info line: decode script index */
            if (sid >= 1000) {
                int lvl = sid / 1000;
                int act = sid % 1000;
                sprintf(buf, "Script %d - Level %d, Actor %d  (%d/%d)",
                        sid, lvl, act, cur_script + 1, num_scripts);
            } else {
                sprintf(buf, "Script %d - Global  (%d/%d)",
                        sid, cur_script + 1, num_scripts);
            }
            font_print_shadow((SCREEN_W - font_width(buf)) / 2, 24, buf,
                              MENU_NORM_COLOR, MENU_SHADOW_COLOR);

            /* Render visible lines with syntax coloring */
            {
                int start = entries[cur_script].start_offset;
                int end = entries[cur_script].end_offset;
                int line_off = get_line_offset(speak_data, start, end,
                                               scroll_y);

                for (i = 0; i < SCRIPT_VISIBLE_LINES; i++) {
                    char line_buf[128];
                    int ly = SCRIPT_TEXT_Y + i * 8;

                    if (scroll_y + i >= total_lines) break;
                    if (line_off >= end) break;

                    extract_line(speak_data, line_off, end,
                                 line_buf, sizeof(line_buf));
                    render_script_line(SCRIPT_TEXT_X, ly, line_buf,
                                       SCRIPT_MAX_CHARS);

                    /* Advance to next line */
                    while (line_off < end && speak_data[line_off] != 13)
                        line_off++;
                    if (line_off < end && speak_data[line_off] == 13)
                        line_off++;
                    if (line_off < end && speak_data[line_off] == 10)
                        line_off++;
                }
            }

            /* Bottom bar: line position */
            if (total_lines > 0) {
                sprintf(buf, "Line %d/%d", scroll_y + 1, total_lines);
                font_print(SCRIPT_TEXT_X, 176, buf, MENU_NORM_COLOR);
            }
        }

        /* Nav hints */
        draw_nav_hints("L/R:Script  U/D:Scroll  TAB:Area  ESC:Back");

        if (!faded_in) {
            set_brightness(0.0f);
            present();
            do_fade_in();
            faded_in = 1;
            drain_keys();
            continue;
        }

        present();

        key = GetKeyPressed();
        if (key == KEY_LEFT && num_scripts > 0) {
            cur_script = (cur_script + num_scripts - 1) % num_scripts;
            launcher_play_woop();
        }
        if (key == KEY_RIGHT && num_scripts > 0) {
            cur_script = (cur_script + 1) % num_scripts;
            launcher_play_woop();
        }
        if (key == KEY_UP && scroll_y > 0) {
            scroll_y--;
        }
        if (key == KEY_DOWN && total_lines > SCRIPT_VISIBLE_LINES &&
            scroll_y < total_lines - SCRIPT_VISIBLE_LINES) {
            scroll_y++;
        }
        if (key == KEY_PAGE_UP) {
            scroll_y -= SCRIPT_VISIBLE_LINES;
            if (scroll_y < 0) scroll_y = 0;
        }
        if (key == KEY_PAGE_DOWN && total_lines > SCRIPT_VISIBLE_LINES) {
            scroll_y += SCRIPT_VISIBLE_LINES;
            if (scroll_y > total_lines - SCRIPT_VISIBLE_LINES)
                scroll_y = total_lines - SCRIPT_VISIBLE_LINES;
        }
        if (key == KEY_TAB) {
            area = (area + 1) % 3;
            launcher_play_woop();
        }
        if (key == KEY_ESCAPE) break;
    }

    free(speak_data);
    free(entries);
    drain_keys();
}

/* ════════════════════════════════════════════════════════════════════
 * EXTRAS SUBMENU
 * ════════════════════════════════════════════════════════════════════ */

void launcher_extras_menu(void) {
    static const char *items[] = {
        "Map Viewer", "Sprite Viewer", "Music Player", "Sound Test",
        "Script Viewer", NULL
    };

    while (!WindowShouldClose()) {
        int sel = run_menu("Extras", items, 0);
        switch (sel) {
        case 0: extras_map_viewer();    break;
        case 1: extras_sprite_viewer(); break;
        case 2: extras_music_player();  break;
        case 3: extras_sound_test();    break;
        case 4: extras_script_viewer(); break;
        case -1:
            drain_keys();
            viz_free();
            return;
        }
    }
}
