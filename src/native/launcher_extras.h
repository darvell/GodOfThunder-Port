/* launcher_extras.h - Extras menu for the native launcher
 *
 * Bonus "re-release edition" features: Map Viewer, Sprite/Character Viewer,
 * Music Player, and Sound Effect Tester.  Uses the launcher's existing UI
 * framework (framebuffer rendering, menu system, frame borders, cursor
 * animation, sound effects).
 */
#ifndef GOT_LAUNCHER_EXTRAS_H
#define GOT_LAUNCHER_EXTRAS_H

#include <stdint.h>
#include "graphics_got.h"

/* ── Extras entry point (called from launcher.c menu loop) ── */
void launcher_extras_menu(void);

/* ── Launcher internals shared with launcher_extras.c ── */

#define SCREEN_W 320
#define SCREEN_H 200

/* State */
extern uint8_t         s_screen[SCREEN_W * SCREEN_H];
extern uint8_t         s_pal6[768];
extern uint8_t         s_pal_rgb[256][4];
extern graphics_got_t  s_gg;
extern int             s_gg_loaded;
extern int             s_audio_ready;
extern int             s_font_w;
extern int             s_font_h;
extern uint8_t        *s_frame[9];
extern uint8_t        *s_cursor[4];

/* Font/text */
void font_print(int dx, int dy, const char *str, uint8_t color);
void font_center(int dy, const char *str, uint8_t color);
int  font_width(const char *str);
void font_print_shadow(int dx, int dy, const char *str,
                       uint8_t color, uint8_t shadow);

/* Drawing */
void fill_rect(int x1, int y1, int x2, int y2, uint8_t color);
void blit8(const uint8_t *src, int sw, int sh, int dx, int dy);
void sprite_blit(const uint8_t *data, int dx, int dy, int width, int height);
void draw_menu_bg(void);
void draw_menu_frame(int x1, int y1, int x2, int y2);

/* Palette */
void set_brightness(float b);
void load_pal6_raw(const uint8_t *p);

/* Presentation */
void present(void);
int  do_fade_in(void);
int  do_fade_out(void);

/* Menu system */
int  run_menu(const char *title, const char **items, int initial_sel);
int  check_skip(void);

/* Audio */
void launcher_play_woop(void);
void launcher_play_clang(void);

#endif /* GOT_LAUNCHER_EXTRAS_H */
