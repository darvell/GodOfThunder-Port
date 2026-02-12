#include "got_platform.h"
#include "gui.h"

#include "raylib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#ifdef __EMSCRIPTEN__
int got_web_gamepad_is_connected(int pad);
int got_web_gamepad_button_down(int pad, int btn);
float got_web_gamepad_axis(int pad, int axis);
float got_web_gamepad_button_value(int pad, int btn);
#endif

#include <dos.h>
#include "modern.h"
#if defined(GOT_EPISODE) && GOT_EPISODE == 2
#include "2_define.h"
#include "2_proto.h"
#elif defined(GOT_EPISODE) && GOT_EPISODE == 3
#include "3_define.h"
#include "3_proto.h"
#elif defined(GOT_EPISODE)
#include "1_define.h"
#include "1_proto.h"
#else
#include "game_define.h"
#include "game_proto.h"
#endif

/* Game globals (defined in src/_g1/1_main.c) */
extern volatile char key_flag[100];
extern volatile unsigned int timer_cnt, vbl_cnt, magic_cnt, extra_cnt;
extern char slow_mode;
extern char main_loop;

/* Forward decl (implemented later in this file). */
int xsetpal(unsigned char color, unsigned char R,unsigned char G,unsigned char B);

/* Sound/mus tick services (native build provides FX_*, MU_* stubs/impls). */
void FX_ServicePC(void);
void MU_Service(void);

enum {
  GOT_W = 320,
  GOT_H = 240,
  GOT_PLAY_H = 192,
  GOT_STAT_H = 48
};

typedef struct {
  int w;
  int h;
  int stride;
  uint8_t* pix;
} Surf8;

static int g_split_mode = 0;

static uint8_t g_full_pages[3][GOT_W * GOT_H];
static uint8_t g_play_pages[3][GOT_W * GOT_PLAY_H];
static uint8_t g_stat_page[GOT_W * GOT_STAT_H];

static uint8_t g_pal6[256][3];         /* 0..63 */
static uint8_t g_pal8[256][3];         /* 0..255 */
static uint8_t g_pal_rgba[256][4];     /* r,g,b,a */

static unsigned g_last_show_pagebase = 0;

static double g_last_time_s = 0.0;
static double g_tick_accum_s = 0.0;
static double g_frame_next_s = 0.0;
static double g_present_next_s = 0.0; /* web: 60Hz present schedule */

/* Palette-cycling state ported from src/utility/g_asm.asm xshowpage.
   DOS animates a handful of palette entries as part of page-flip/vblank.
   In the native renderer palette changes must occur before index->RGBA
   conversion or they won't affect the displayed frame. */
static uint8_t g_palloop = 0;
static uint8_t g_palcnt1 = 0; /* 0..3 */
static uint8_t g_palcnt2 = 0; /* 0..3 */

typedef struct {
  uint8_t idx;
  uint8_t r, g, b; /* DAC units: 0..63 */
} PalStep;

static const PalStep g_palclr1[4] = {
  { 0xF3, 0x00, 0x00, 0x3B },
  { 0xF0, 0x00, 0x00, 0x3B },
  { 0xF1, 0x00, 0x00, 0x3B },
  { 0xF2, 0x00, 0x00, 0x3B }
};

static const PalStep g_palset1[4] = {
  { 0xF0, 0x27, 0x27, 0x3F },
  { 0xF1, 0x27, 0x27, 0x3F },
  { 0xF2, 0x27, 0x27, 0x3F },
  { 0xF3, 0x27, 0x27, 0x3F }
};

static const PalStep g_palclr2[4] = {
  { 0xF7, 0x3B, 0x00, 0x00 },
  { 0xF4, 0x3B, 0x00, 0x00 },
  { 0xF5, 0x3B, 0x00, 0x00 },
  { 0xF6, 0x3B, 0x00, 0x00 }
};

static const PalStep g_palset2[4] = {
  { 0xF4, 0x3F, 0x27, 0x27 },
  { 0xF5, 0x3F, 0x27, 0x27 },
  { 0xF6, 0x3F, 0x27, 0x27 },
  { 0xF7, 0x3F, 0x27, 0x27 }
};

static void maybe_palette_cycle_for_frame(void) {
  if (!main_loop) return;

  /* PAL_SPEED = 10 in asm. shr ax, slow_mode. */
  unsigned speed = 10u;
  unsigned sm = (unsigned)(unsigned char)slow_mode;
  if (sm < 8u) speed >>= sm;
  if (speed == 0u) return;

  g_palloop++;
  if (g_palloop > speed) {
    g_palloop = 0;
    return;
  }

  /* The asm triggers 4 palette ops near the end of the cycle. */
  if (speed < 4u) return;
  {
    unsigned t0 = speed - 4u;
    if (g_palloop == t0) {
      PalStep s = g_palclr2[g_palcnt2 & 3u];
      xsetpal(s.idx, s.r, s.g, s.b);
    } else if (g_palloop == (t0 + 1u)) {
      PalStep s = g_palset2[g_palcnt2 & 3u];
      xsetpal(s.idx, s.r, s.g, s.b);
      g_palcnt2 = (uint8_t)((g_palcnt2 + 1u) & 3u);
    } else if (g_palloop == (t0 + 2u)) {
      PalStep s = g_palclr1[g_palcnt1 & 3u];
      xsetpal(s.idx, s.r, s.g, s.b);
    } else if (g_palloop == (t0 + 3u)) {
      PalStep s = g_palset1[g_palcnt1 & 3u];
      xsetpal(s.idx, s.r, s.g, s.b);
      g_palcnt1 = (uint8_t)((g_palcnt1 + 1u) & 3u);
    }
  }
}

static int g_video_ready = 0;
static RenderTexture2D g_rt;
static Texture2D g_frame_tex;
static uint8_t g_frame_rgba[GOT_W * GOT_H * 4];

/* Mouse state, in 320x240 "game pixels". */
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static int g_mouse_buttons = 0; /* bit0=left, bit1=right, bit2=middle */

static Surf8 surf_full_idx(int idx) {
  Surf8 s;
  if (idx < 0) idx = 0;
  if (idx > 2) idx = 2;
  s.w = GOT_W;
  s.h = GOT_H;
  s.stride = GOT_W;
  s.pix = &g_full_pages[idx][0];
  return s;
}

static Surf8 surf_play_idx(int idx) {
  Surf8 s;
  if (idx < 0) idx = 0;
  if (idx > 2) idx = 2;
  s.w = GOT_W;
  s.h = GOT_PLAY_H;
  s.stride = GOT_W;
  s.pix = &g_play_pages[idx][0];
  return s;
}

static Surf8 surf_stat(void) {
  Surf8 s;
  s.w = GOT_W;
  s.h = GOT_STAT_H;
  s.stride = GOT_W;
  s.pix = &g_stat_page[0];
  return s;
}

static int nearest_play_page_idx(unsigned int pagebase) {
  /* PageBase values are DOS offsets; for our native build treat them as IDs. */
  unsigned int bases[3] = { PAGE0, PAGE1, PAGE2 };
  int best = 0;
  unsigned int best_d = (unsigned int)(pagebase > bases[0] ? pagebase - bases[0] : bases[0] - pagebase);
  int i;
  for (i = 1; i < 3; i++) {
    unsigned int d = (unsigned int)(pagebase > bases[i] ? pagebase - bases[i] : bases[i] - pagebase);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

static int nearest_full_page_idx(unsigned int pagebase) {
  /* Full-screen pages are generally addressed as 0, 19200, 38400. */
  unsigned int bases[3] = { 0u, 19200u, 38400u };
  int best = 0;
  unsigned int best_d = (unsigned int)(pagebase > bases[0] ? pagebase - bases[0] : bases[0] - pagebase);
  int i;
  for (i = 1; i < 3; i++) {
    unsigned int d = (unsigned int)(pagebase > bases[i] ? pagebase - bases[i] : bases[i] - pagebase);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

static Surf8 resolve_surf(unsigned int pagebase) {
  if (g_split_mode) {
    if (pagebase == 0u) {
      return surf_stat();
    }
    return surf_play_idx(nearest_play_page_idx(pagebase));
  }
  return surf_full_idx(nearest_full_page_idx(pagebase));
}

static void clear_surf(Surf8 s, uint8_t color) {
  int y;
  for (y = 0; y < s.h; y++) {
    memset(s.pix + y * s.stride, color, (size_t)s.w);
  }
}

static void put_pixel(Surf8 s, int x, int y, uint8_t c) {
  if ((unsigned)x >= (unsigned)s.w || (unsigned)y >= (unsigned)s.h) return;
  s.pix[y * s.stride + x] = c;
}

static uint8_t get_pixel(Surf8 s, int x, int y) {
  if ((unsigned)x >= (unsigned)s.w || (unsigned)y >= (unsigned)s.h) return 0;
  return s.pix[y * s.stride + x];
}

static void blit_rect(Surf8 src, int sx, int sy, int ex, int ey,
                      Surf8 dst, int dx, int dy) {
  int w = ex - sx;
  int h = ey - sy;
  int row;
  if (w <= 0 || h <= 0) return;

  /* Clamp on both src and dst. */
  if (sx < 0) { dx -= sx; w += sx; sx = 0; }
  if (sy < 0) { dy -= sy; h += sy; sy = 0; }
  if (dx < 0) { sx -= dx; w += dx; dx = 0; }
  if (dy < 0) { sy -= dy; h += dy; dy = 0; }

  if (sx + w > src.w) w = src.w - sx;
  if (dx + w > dst.w) w = dst.w - dx;
  if (sy + h > src.h) h = src.h - sy;
  if (dy + h > dst.h) h = dst.h - dy;
  if (w <= 0 || h <= 0) return;

  for (row = 0; row < h; row++) {
    memmove(dst.pix + (dy + row) * dst.stride + dx,
            src.pix + (sy + row) * src.stride + sx,
            (size_t)w);
  }
}

static void draw_planar_to_surf(Surf8 dst, int x, int y,
                                const uint8_t* planes, int w_bytes, int h,
                                int transparent, uint8_t invis) {
  int plane_sz = w_bytes * h;
  int p, row, bx;
  for (p = 0; p < 4; p++) {
    const uint8_t* plane = planes + p * plane_sz;
    for (row = 0; row < h; row++) {
      for (bx = 0; bx < w_bytes; bx++) {
        uint8_t v = plane[row * w_bytes + bx];
        int px = x + (bx * 4) + p;
        int py = y + row;
        if (transparent && v == invis) {
          continue;
        }
        put_pixel(dst, px, py, v);
      }
    }
  }
}

static void draw_planar_masked_to_surf(Surf8 dst, int x, int y,
                                       const uint8_t* planes, int w_bytes, int h) {
  /* Actor/sprite convention in original asm: treat 0 and 15 as transparent. */
  int plane_sz = w_bytes * h;
  int p, row, bx;
  for (p = 0; p < 4; p++) {
    const uint8_t* plane = planes + p * plane_sz;
    for (row = 0; row < h; row++) {
      for (bx = 0; bx < w_bytes; bx++) {
        uint8_t v = plane[row * w_bytes + bx];
        int px = x + (bx * 4) + p;
        int py = y + row;
        if (v == 0 || v == 15) continue;
        put_pixel(dst, px, py, v);
      }
    }
  }
}

void got_platform_set_split(int on) {
  g_split_mode = on ? 1 : 0;
}

static void compute_viewport(int* out_dx, int* out_dy, int* out_scale) {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  int cw = GOT_W;
  int sx = sw / cw;
  int sy = sh / GOT_H;
  int s = (sx < sy) ? sx : sy;
  if (s < 1) s = 1;
  if (out_scale) *out_scale = s;
  if (out_dx) *out_dx = (sw - (cw * s)) / 2;
  if (out_dy) *out_dy = (sh - (GOT_H * s)) / 2;
}

static void update_mouse_state(void) {
  Vector2 mp = GetMousePosition();
  int dx = 0, dy = 0, s = 1;
  int gx = 0, gy = 0;

  compute_viewport(&dx, &dy, &s);

  gx = ((int)mp.x - dx) / s;
  gy = ((int)mp.y - dy) / s;

  if (gx < 0) gx = 0;
  if (gy < 0) gy = 0;
  if (gx >= GOT_W) gx = GOT_W - 1;
  if (gy >= GOT_H) gy = GOT_H - 1;

  g_mouse_x = gx;
  g_mouse_y = gy;

  g_mouse_buttons = 0;
  if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_mouse_buttons |= 1;
  if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) g_mouse_buttons |= 2;
  if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) g_mouse_buttons |= 4;
}

void got_platform_mouse_get_position(int* out_x, int* out_y) {
  if (out_x) *out_x = g_mouse_x;
  if (out_y) *out_y = g_mouse_y;
}

int got_platform_mouse_button_down(int button) {
  if (button == 0) return (g_mouse_buttons & 1) ? 1 : 0;
  if (button == 1) return (g_mouse_buttons & 2) ? 1 : 0;
  if (button == 2) return (g_mouse_buttons & 4) ? 1 : 0;
  return 0;
}

int got_platform_gamepad_is_available(int gamepad) {
#ifdef __EMSCRIPTEN__
  return got_web_gamepad_is_connected(gamepad);
#else
  return IsGamepadAvailable(gamepad) ? 1 : 0;
#endif
}

static int raylib_button_from_custom(int custom) {
  switch (custom) {
    case 1: return GAMEPAD_BUTTON_RIGHT_FACE_DOWN;   /* A */
    case 2: return GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;  /* B */
    case 3: return GAMEPAD_BUTTON_RIGHT_FACE_LEFT;   /* X */
    case 4: return GAMEPAD_BUTTON_RIGHT_FACE_UP;     /* Y */
    case 5: return GAMEPAD_BUTTON_LEFT_THUMB;        /* L Stick */
    case 6: return GAMEPAD_BUTTON_RIGHT_THUMB;       /* R Stick */
    case 7: return GAMEPAD_BUTTON_MIDDLE_RIGHT;      /* Start */
    case 8: return GAMEPAD_BUTTON_MIDDLE_LEFT;       /* Back */
    case 9: return GAMEPAD_BUTTON_LEFT_TRIGGER_1;    /* LB */
    case 10: return GAMEPAD_BUTTON_RIGHT_TRIGGER_1;  /* RB */
    case 11: return GAMEPAD_BUTTON_LEFT_FACE_UP;     /* D-Up */
    case 12: return GAMEPAD_BUTTON_LEFT_FACE_DOWN;   /* D-Down */
    case 13: return GAMEPAD_BUTTON_LEFT_FACE_LEFT;   /* D-Left */
    case 14: return GAMEPAD_BUTTON_LEFT_FACE_RIGHT;  /* D-Right */
    case 17: return GAMEPAD_BUTTON_MIDDLE;           /* Guide */
    default: return GAMEPAD_BUTTON_UNKNOWN;
  }
}

int got_platform_gamepad_button_down(int gamepad, int button) {
#ifdef __EMSCRIPTEN__
  /* Browser bridge state is updated by the HTML shell. */
  if (!got_web_gamepad_is_connected(gamepad)) return 0;
  if (got_web_gamepad_button_down(gamepad, button)) return 1;
  /* Triggers: treat as down if analog value is significant. */
  if (button == 15 || button == 16) {
    return got_web_gamepad_button_value(gamepad, button) > 0.5f ? 1 : 0;
  }
  return 0;
#else
  int rb = raylib_button_from_custom(button);
  if (!IsGamepadAvailable(gamepad)) return 0;
  if (rb == GAMEPAD_BUTTON_UNKNOWN) return 0;
  return IsGamepadButtonDown(gamepad, rb) ? 1 : 0;
#endif
}

float got_platform_gamepad_axis_movement(int gamepad, int axis) {
#ifdef __EMSCRIPTEN__
  if (!got_web_gamepad_is_connected(gamepad)) return 0.0f;
  return got_web_gamepad_axis(gamepad, axis);
#else
  return GetGamepadAxisMovement(gamepad, axis);
#endif
}

void got_platform_video_init(void) {
  int i;
  Image img;

  if (g_video_ready) return;

  /* The DOS build presents around the VGA retrace cadence (~70Hz) and game
     logic is effectively coupled to page-flips. Avoid monitor-refresh vsync
     (60/120/144Hz) so gameplay speed is stable across displays. */
  if (!IsWindowReady()) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(GOT_W * 2, GOT_H * 2, "God of Thunder (native)");
  }
  SetWindowTitle("God of Thunder");

  g_rt = LoadRenderTexture(GOT_W, GOT_H);
  SetTextureFilter(g_rt.texture, TEXTURE_FILTER_POINT);

  /* Avoid raylib's internal frame limiter. We control pacing in xshowpage(). */
  SetTargetFPS(0);

  img = GenImageColor(GOT_W, GOT_H, BLACK);
  g_frame_tex = LoadTextureFromImage(img);
  UnloadImage(img);
  SetTextureFilter(g_frame_tex, TEXTURE_FILTER_POINT);

  memset(g_full_pages, 0, sizeof(g_full_pages));
  memset(g_play_pages, 0, sizeof(g_play_pages));
  memset(g_stat_page, 0, sizeof(g_stat_page));

  memset(g_pal6, 0, sizeof(g_pal6));
  memset(g_pal8, 0, sizeof(g_pal8));
  memset(g_pal_rgba, 0, sizeof(g_pal_rgba));
  for (i = 0; i < 256; i++) {
    g_pal_rgba[i][3] = 255;
  }

  g_last_time_s = GetTime();
  g_tick_accum_s = 0.0;
  g_frame_next_s = 0.0;
  g_present_next_s = 0.0;
  g_video_ready = 1;
}

void got_platform_video_shutdown(void) {
  if (!g_video_ready) return;

  UnloadTexture(g_frame_tex);
  UnloadRenderTexture(g_rt);
  CloseWindow();
  g_video_ready = 0;
}

static void got_platform_tick_120hz(void) {
  timer_cnt++;
  vbl_cnt++;
  magic_cnt++;
  extra_cnt++;
  FX_ServicePC();
  MU_Service();
}

static uint8_t g_kb_down[100];
static uint8_t g_gp_down[100];
#ifdef __EMSCRIPTEN__
static uint8_t g_web_kb_down[100];

EMSCRIPTEN_KEEPALIVE void got_web_keyboard_set_scancode(int dos, int down) {
  if (dos <= 0 || dos >= (int)sizeof(g_web_kb_down)) return;
  g_web_kb_down[dos] = down ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void got_web_keyboard_clear(void) {
  memset(g_web_kb_down, 0, sizeof(g_web_kb_down));
}
#endif

static void dos_key_update(int dos, int down) {
  /* Update key_flag[] only on make/break transitions (DOS-style).
     This preserves game-side "consume by clearing" patterns for menu keys. */
  static uint8_t prev_down[100];
  if (dos <= 0 || dos >= 100) return;
  if (down && !prev_down[dos]) {
    key_flag[dos] = 1;
    prev_down[dos] = 1;
  } else if (!down && prev_down[dos]) {
    key_flag[dos] = 0;
    prev_down[dos] = 0;
  }
}

int got_platform_map_key_to_dos_scancode(int key) {
  /* key is raylib KeyboardKey. Return DOS set 1 scancode used by this codebase. */
  switch (key) {
    case KEY_ESCAPE: return 1;
    case KEY_ONE: return 2;
    case KEY_TWO: return 3;
    case KEY_THREE: return 4;
    case KEY_FOUR: return 5;
    case KEY_FIVE: return 6;
    case KEY_SIX: return 7;
    case KEY_SEVEN: return 8;
    case KEY_EIGHT: return 9;
    case KEY_NINE: return 10;
    case KEY_ZERO: return 11;
    case KEY_MINUS: return 12;
    case KEY_EQUAL: return 13;
    case KEY_BACKSPACE: return 14;
    case KEY_TAB: return 15;
    case KEY_Q: return 16;
    case KEY_W: return 17;
    case KEY_E: return 18;
    case KEY_R: return 19;
    case KEY_T: return 20;
    case KEY_Y: return 21;
    case KEY_U: return 22;
    case KEY_I: return 23;
    case KEY_O: return 24;
    case KEY_P: return 25;
    case KEY_LEFT_BRACKET: return 26;
    case KEY_RIGHT_BRACKET: return 27;
    case KEY_ENTER: return 28;
    case KEY_LEFT_CONTROL:
    case KEY_RIGHT_CONTROL: return 29;
    case KEY_A: return 30;
    case KEY_S: return 31;
    case KEY_D: return 32;
    case KEY_F: return 33;
    case KEY_G: return 34;
    case KEY_H: return 35;
    case KEY_J: return 36;
    case KEY_K: return 37;
    case KEY_L: return 38;
    case KEY_SEMICOLON: return 39;
    case KEY_APOSTROPHE: return 40;
    case KEY_GRAVE: return 41;
    case KEY_LEFT_SHIFT: return 42;
    case KEY_BACKSLASH: return 43;
    case KEY_Z: return 44;
    case KEY_X: return 45;
    case KEY_C: return 46;
    case KEY_V: return 47;
    case KEY_B: return 48;
    case KEY_N: return 49;
    case KEY_M: return 50;
    case KEY_COMMA: return 51;
    case KEY_PERIOD: return 52;
    case KEY_SLASH: return 53;
    case KEY_RIGHT_SHIFT: return 54;
    case KEY_LEFT_ALT:
    case KEY_RIGHT_ALT: return 56;
    case KEY_SPACE: return 57;
    case KEY_F1: return 59;
    case KEY_F2: return 60;
    case KEY_F3: return 61;
    case KEY_F4: return 62;
    case KEY_F5: return 63;
    case KEY_F6: return 64;
    case KEY_F7: return 65;
    case KEY_F8: return 66;
    case KEY_F9: return 67;
    case KEY_F10: return 68;
    case KEY_HOME: return 71;
    case KEY_UP: return 72;
    case KEY_PAGE_UP: return 73;
    case KEY_LEFT: return 75;
    case KEY_RIGHT: return 77;
    case KEY_END: return 79;
    case KEY_DOWN: return 80;
    case KEY_PAGE_DOWN: return 81;
    default: return 0;
  }
}

static void apply_keyboard_state(void) {
  /* Keep this list in sync with got_platform_map_key_to_dos_scancode() above. */
  struct KeyMap { int key; int dos; };
  static const struct KeyMap map[] = {
    { KEY_ESCAPE, 1 },
    { KEY_ONE, 2 }, { KEY_TWO, 3 }, { KEY_THREE, 4 }, { KEY_FOUR, 5 },
    { KEY_FIVE, 6 }, { KEY_SIX, 7 }, { KEY_SEVEN, 8 }, { KEY_EIGHT, 9 },
    { KEY_NINE, 10 }, { KEY_ZERO, 11 }, { KEY_MINUS, 12 }, { KEY_EQUAL, 13 },
    { KEY_BACKSPACE, 14 }, { KEY_TAB, 15 },
    { KEY_Q, 16 }, { KEY_W, 17 }, { KEY_E, 18 }, { KEY_R, 19 }, { KEY_T, 20 },
    { KEY_Y, 21 }, { KEY_U, 22 }, { KEY_I, 23 }, { KEY_O, 24 }, { KEY_P, 25 },
    { KEY_LEFT_BRACKET, 26 }, { KEY_RIGHT_BRACKET, 27 },
    { KEY_ENTER, 28 },
    { KEY_LEFT_CONTROL, 29 }, { KEY_RIGHT_CONTROL, 29 },
    { KEY_A, 30 }, { KEY_S, 31 }, { KEY_D, 32 }, { KEY_F, 33 }, { KEY_G, 34 },
    { KEY_H, 35 }, { KEY_J, 36 }, { KEY_K, 37 }, { KEY_L, 38 },
    { KEY_SEMICOLON, 39 }, { KEY_APOSTROPHE, 40 }, { KEY_GRAVE, 41 },
    { KEY_LEFT_SHIFT, 42 }, { KEY_RIGHT_SHIFT, 54 },
    { KEY_BACKSLASH, 43 },
    { KEY_Z, 44 }, { KEY_X, 45 }, { KEY_C, 46 }, { KEY_V, 47 }, { KEY_B, 48 },
    { KEY_N, 49 }, { KEY_M, 50 }, { KEY_COMMA, 51 }, { KEY_PERIOD, 52 }, { KEY_SLASH, 53 },
    { KEY_LEFT_ALT, 56 }, { KEY_RIGHT_ALT, 56 },
    { KEY_SPACE, 57 },
    { KEY_F1, 59 }, { KEY_F2, 60 }, { KEY_F3, 61 }, { KEY_F4, 62 }, { KEY_F5, 63 },
    { KEY_F6, 64 }, { KEY_F7, 65 }, { KEY_F8, 66 }, { KEY_F9, 67 }, { KEY_F10, 68 },
    { KEY_HOME, 71 }, { KEY_UP, 72 }, { KEY_PAGE_UP, 73 },
    { KEY_LEFT, 75 }, { KEY_RIGHT, 77 },
    { KEY_END, 79 }, { KEY_DOWN, 80 }, { KEY_PAGE_DOWN, 81 }
  };
  int i;

  memset(g_kb_down, 0, sizeof(g_kb_down));
  for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++) {
    int dos = map[i].dos;
    int down = IsKeyDown(map[i].key) ? 1 : 0;
    if (dos > 0 && dos < (int)sizeof(g_kb_down)) {
      g_kb_down[dos] = (uint8_t)(g_kb_down[dos] | (uint8_t)down);
    }
  }
#ifdef __EMSCRIPTEN__
  /* Browser bridge fallback: explicitly supplied key state from JS events.
     This avoids focus/backend edge cases where IsKeyDown() can miss updates. */
  for (i = 1; i < (int)sizeof(g_kb_down); i++) {
    if (g_web_kb_down[i]) g_kb_down[i] = 1;
  }
#endif
}

static int g_item_cycle_dir = 0;

int got_platform_get_item_cycle(void) { return g_item_cycle_dir; }

static void apply_gamepad_state(void) {
  memset(g_gp_down, 0, sizeof(g_gp_down));

  int pad = 0;
  if (!got_platform_gamepad_is_available(pad)) return;

  /* D-pad + left stick contribute to the same logical directions. */
  int up = got_platform_gamepad_button_down(pad, 11) ? 1 : 0;
  int down = got_platform_gamepad_button_down(pad, 12) ? 1 : 0;
  int left = got_platform_gamepad_button_down(pad, 13) ? 1 : 0;
  int right = got_platform_gamepad_button_down(pad, 14) ? 1 : 0;

  /* Left stick (configurable deadzone) */
  {
    extern got_config_t g_config;
    float ax = got_platform_gamepad_axis_movement(pad, GAMEPAD_AXIS_LEFT_X);
    float ay = got_platform_gamepad_axis_movement(pad, GAMEPAD_AXIS_LEFT_Y);
    float dz = g_config.gp_deadzone / 100.0f;
    if (ax < -dz) left = 1;
    if (ax > dz) right = 1;
    if (ay < -dz) up = 1;
    if (ay > dz) down = 1;
  }

  if (UP > 0 && UP < (int)sizeof(g_gp_down)) g_gp_down[UP] = (uint8_t)up;
  if (DOWN > 0 && DOWN < (int)sizeof(g_gp_down)) g_gp_down[DOWN] = (uint8_t)down;
  if (LEFT > 0 && LEFT < (int)sizeof(g_gp_down)) g_gp_down[LEFT] = (uint8_t)left;
  if (RIGHT > 0 && RIGHT < (int)sizeof(g_gp_down)) g_gp_down[RIGHT] = (uint8_t)right;

  /* Configurable button mapping.
     Use make/break semantics so pause/menu buttons don't auto-repeat just
     because they were held for a few frames. */
  {
    extern got_config_t g_config;
    extern int key_fire, key_magic, key_select;
    int enter_down = 0;

    if (key_fire > 0 && key_fire < (int)sizeof(g_gp_down)) g_gp_down[key_fire] = (uint8_t)(got_platform_gamepad_button_down(pad, g_config.gp_fire) ? 1 : 0);
    if (key_magic > 0 && key_magic < (int)sizeof(g_gp_down)) g_gp_down[key_magic] = (uint8_t)(got_platform_gamepad_button_down(pad, g_config.gp_magic) ? 1 : 0);
    if (key_select > 0 && key_select < (int)sizeof(g_gp_down)) g_gp_down[key_select] = (uint8_t)(got_platform_gamepad_button_down(pad, g_config.gp_select) ? 1 : 0);

    enter_down |= got_platform_gamepad_button_down(pad, g_config.gp_confirm) ? 1 : 0;
    enter_down |= got_platform_gamepad_button_down(pad, g_config.gp_menu_confirm) ? 1 : 0;
    if (ENTER > 0 && ENTER < (int)sizeof(g_gp_down)) g_gp_down[ENTER] = (uint8_t)enter_down;

    if (ESC > 0 && ESC < (int)sizeof(g_gp_down)) g_gp_down[ESC] = (uint8_t)(got_platform_gamepad_button_down(pad, g_config.gp_pause) ? 1 : 0);
  }

  /* Shoulder buttons — item quick-cycle with edge detection (configurable) */
  {
    extern got_config_t g_config;
    static int lb_prev = 0, rb_prev = 0;
    int lb = got_platform_gamepad_button_down(pad, g_config.gp_item_prev);
    int rb = got_platform_gamepad_button_down(pad, g_config.gp_item_next);
    g_item_cycle_dir = 0;
    if (lb && !lb_prev) g_item_cycle_dir = -1;
    if (rb && !rb_prev) g_item_cycle_dir = 1;
    lb_prev = lb; rb_prev = rb;
  }
}

void got_platform_pump(void) {
  /* raylib updates keyboard/gamepad state when PollInputEvents() runs.
     Most of the game calls xshowpage() every frame (EndDrawing() does this),
     but some screens spin in wait loops without drawing (e.g. story_wait()).
     If we don't poll here, IsKeyDown()/WindowShouldClose() never change and
     those wait loops look like "hangs". */
  if (g_video_ready) {
    PollInputEvents();
  }

#ifdef __EMSCRIPTEN__
  /* Ensure Gamepad API state is refreshed even if raylib's internal backend
     doesn't sample it for our synchronous main loop. */
  /* The actual gamepad state is bridged from JS (navigator.getGamepads()).
     Keep this call since it is cheap and can help some browsers update the
     internal snapshot, but do not rely on raylib's IsGamepad* APIs. */
  emscripten_sample_gamepad_data();

  /* Some call sites (get_response()/wait-for-key-release loops) can spin hard
     without rendering. We must occasionally yield to the browser event loop
     or the tab will freeze, but we also must not yield every pump call (too
     expensive with ASYNCIFY). */
  {
    static double last_yield_ms = 0.0;
    double now_ms = emscripten_get_now();
    if (last_yield_ms <= 0.0) last_yield_ms = now_ms;
    if ((now_ms - last_yield_ms) > 8.0) {
      last_yield_ms = now_ms;
      emscripten_sleep(0);
    }
  }
#endif

  double now = GetTime();
  double dt = now - g_last_time_s;

  if (dt < 0.0) dt = 0.0;
  /* Cap dt to prevent a burst of ticks after long gaps (e.g. first call when
     g_last_time_s is still 0, or after loading screens/window creation). */
  if (dt > 0.1) dt = 0.1;
  g_last_time_s = now;

  g_tick_accum_s += dt;
  while (g_tick_accum_s >= (1.0 / 120.0)) {
    g_tick_accum_s -= (1.0 / 120.0);
    got_platform_tick_120hz();
  }

  apply_keyboard_state();
  apply_gamepad_state();
  {
    int dos;
    for (dos = 1; dos < 100; dos++) {
      int combined = (g_kb_down[dos] || g_gp_down[dos]) ? 1 : 0;
      dos_key_update(dos, combined);
    }
  }
  update_mouse_state();

  /* Fullscreen toggle (F11) */
  {
    static int f11_prev = 0;
    int f11_now = IsKeyDown(KEY_F11);
    if (f11_now && !f11_prev) {
      ToggleBorderlessWindowed();
    }
    f11_prev = f11_now;
  }

  /* On web raylib's WindowShouldClose() is a legacy stub that calls
     emscripten_sleep(16) and can either abort (no async support) or kill
     frame pacing. We don't support "closing the window" in web builds. */
#ifndef __EMSCRIPTEN__
  if (WindowShouldClose()) key_flag[ESC] = 1;
#endif
}

static void got_platform_wait_for_frame(void) {
  /* Web note: Avoid using raylib's internal WaitTime/nanosleep pacing. We do
     our own, and on web we keep it deterministic using emscripten_get_now(). */
#ifdef __EMSCRIPTEN__
  /* Run game logic at DOS-ish vblank cadence. We'll drop/skip presents to fit
     a 60Hz browser display later. */
  const double frame_dt = 1.0 / 70.0;
  double now = emscripten_get_now() / 1000.0;
#else
  const double frame_dt = 1.0 / 70.0;
  double now = GetTime();
#endif

  if (g_frame_next_s <= 0.0) {
    g_frame_next_s = now;
  }
  if (g_frame_next_s < now - frame_dt * 2) {
    /* Don't accumulate more than ~2 frames of debt.  Large gaps happen
       during level loading; without this cap the subsequent scroll/
       transition frames would burn through the debt instantly, making
       the animation invisible. */
    g_frame_next_s = now;
  }
  g_frame_next_s += frame_dt;

#ifdef __EMSCRIPTEN__
  {
    /* Browser-friendly wait:
       Yield once per frame (Asyncify) instead of looping and yielding many
       times. Too many short sleeps can cause severe stutter. */
    double remain_s = g_frame_next_s - now;
    if (remain_s > 0.0) {
      double remain_ms = remain_s * 1000.0;

      /* Pump once before yielding so key edges are responsive even during waits. */
      got_platform_pump();

      if (remain_ms > 2.0) {
        /* Sleep most of the remaining time, then finish with a short spin to
           reduce oversleep jitter. */
        int sleep_ms = (int)(remain_ms - 1.0);
        if (sleep_ms < 0) sleep_ms = 0;
        if (sleep_ms > 0) emscripten_sleep((unsigned)sleep_ms);

        /* Finish the last ~1ms with a short spin/pump. */
        while ((now = emscripten_get_now() / 1000.0) < g_frame_next_s) {
          got_platform_pump();
        }
      } else {
        /* If we're within ~2ms, just yield and keep going. */
        emscripten_sleep(0);
      }
    } else {
      /* Already late; at least yield once so the browser can paint. */
      emscripten_sleep(0);
    }
  }
#else
  while ((now = GetTime()) < g_frame_next_s) {
    got_platform_pump();
    if ((g_frame_next_s - now) > 0.002) {
      delay(1);
    }
  }
#endif
}

static void upload_composited_rgba(unsigned int pagebase) {
  int x, y;
  uint8_t* outp = g_frame_rgba;

  if (!g_split_mode) {
    /* Handle smooth hardware-style scrolling (story sequences).
       In Mode X, 80 bytes = 1 scanline (320 px / 4 planes).  The story
       scroll passes intermediate offsets (not page-aligned) to smoothly
       scroll between full-page surfaces. */
    int start_line = (int)pagebase / 80;
    for (y = 0; y < GOT_H; y++) {
      int src_line = start_line + y;
      int pg  = src_line / GOT_H;
      int row = src_line % GOT_H;
      Surf8 src;
      const uint8_t* inrow;
      if (pg < 0) pg = 0;
      if (pg > 2) pg = 2;
      src = surf_full_idx(pg);
      inrow = src.pix + row * src.stride;
      for (x = 0; x < GOT_W; x++) {
        uint8_t idx = inrow[x];
        *outp++ = g_pal_rgba[idx][0];
        *outp++ = g_pal_rgba[idx][1];
        *outp++ = g_pal_rgba[idx][2];
        *outp++ = 255;
      }
    }
  }
  else {
    /* Split mode: top 192 from the selected play page, bottom 48 from PAGES. */
    int base_idx = nearest_play_page_idx(pagebase);
    unsigned int bases[3] = { PAGE0, PAGE1, PAGE2 };
    int off = (int)pagebase - (int)bases[base_idx];
    int dx = 0, dy = 0;

    if (off == -1) dx = -4;
    else if (off == 1) dx = 4;
    else if (off == -80) dy = -1;
    else if (off == 80) dy = 1;

    {
      Surf8 top = surf_play_idx(base_idx);
      Surf8 st = surf_stat();
      for (y = 0; y < GOT_H; y++) {
        for (x = 0; x < GOT_W; x++) {
          uint8_t idx;
          if (y < GOT_PLAY_H) {
            idx = get_pixel(top, x + dx, y + dy);
          }
          else {
            idx = st.pix[(y - GOT_PLAY_H) * st.stride + x];
          }
          *outp++ = g_pal_rgba[idx][0];
          *outp++ = g_pal_rgba[idx][1];
          *outp++ = g_pal_rgba[idx][2];
          *outp++ = 255;
        }
      }
    }
  }

  UpdateTexture(g_frame_tex, g_frame_rgba);
}

static void present_page(unsigned int pagebase) {
  int dx = 0, dy = 0, s = 1;
  Rectangle src;
  Rectangle dst;

  if (!g_video_ready) got_platform_video_init();

  got_platform_pump();

  upload_composited_rgba(pagebase);

  BeginTextureMode(g_rt);
  ClearBackground(BLACK);
  DrawTexture(g_frame_tex, 0, 0, WHITE);
  EndTextureMode();

  BeginDrawing();
  ClearBackground(BLACK);

  compute_viewport(&dx, &dy, &s);
  src.x = 0.0f;
  src.y = 0.0f;
  src.width = (float)g_rt.texture.width;
  src.height = (float)-g_rt.texture.height; /* flip */

  dst.x = (float)dx;
  dst.y = (float)dy;
  dst.width = (float)(GOT_W * s);
  dst.height = (float)(GOT_H * s);

  {
    Vector2 origin;
    origin.x = 0.0f;
    origin.y = 0.0f;
    DrawTexturePro(g_rt.texture, src, dst, origin, 0.0f, WHITE);
  }
  EndDrawing();
}

/* --- GFX API expected by the original codebase --- */

void GOT_GFXCALL xsetmode(void) {
  got_platform_video_init();
  got_platform_set_split(0);
  clear_surf(surf_full_idx(0), 0);
  clear_surf(surf_full_idx(1), 0);
  clear_surf(surf_full_idx(2), 0);
  clear_surf(surf_play_idx(0), 0);
  clear_surf(surf_play_idx(1), 0);
  clear_surf(surf_play_idx(2), 0);
  clear_surf(surf_stat(), 0);
}

void GOT_GFXCALL xshowpage(unsigned page) {
  g_last_show_pagebase = page;
  maybe_palette_cycle_for_frame();
#ifdef __EMSCRIPTEN__
  /* Present at most 60Hz to match the browser's display pacing, but keep game
     logic running at 70Hz by still blocking like the original xshowpage. */
  {
    const double present_dt = 1.0 / 60.0;
    double now = emscripten_get_now() / 1000.0;
    if (g_present_next_s <= 0.0) g_present_next_s = now;
    if (g_present_next_s < now - present_dt * 4) {
      /* Clamp large gaps (tab switch, loading) to avoid long catch-up loops. */
      g_present_next_s = now;
    }
    if (now >= g_present_next_s) {
      g_present_next_s += present_dt;
      present_page(page);
    }
  }
#else
  present_page(page);
#endif
  got_platform_wait_for_frame();
}

void GOT_GFXCALL xfillrectangle(int StartX, int StartY, int EndX, int EndY,
                    unsigned int PageBase, int Color) {
  Surf8 s = resolve_surf(PageBase);
  int x, y;
  if (StartX < 0) StartX = 0;
  if (StartY < 0) StartY = 0;
  if (EndX > s.w) EndX = s.w;
  if (EndY > s.h) EndY = s.h;
  if (EndX <= StartX || EndY <= StartY) return;
  for (y = StartY; y < EndY; y++) {
    uint8_t* row = s.pix + y * s.stride;
    for (x = StartX; x < EndX; x++) {
      row[x] = (uint8_t)Color;
    }
  }
}

void GOT_GFXCALL xpset(int X, int Y, unsigned int PageBase, int Color) {
  Surf8 s = resolve_surf(PageBase);
  put_pixel(s, X, Y, (uint8_t)Color);
}

int GOT_GFXCALL xpoint(int X, int Y, unsigned int PageBase) {
  Surf8 s = resolve_surf(PageBase);
  return (int)get_pixel(s, X, Y);
}

void GOT_GFXCALL xget(int x1,int y1,int x2,int y2,unsigned int pagebase,
          char far *buff,int invis) {
  /* Store a 16-bit (DOS) header: widthBytes,height,invis + planar pixels.
     This is only needed by a handful of editor/tools; the game rarely calls it. */
  Surf8 s = resolve_surf(pagebase);
  /* DOS semantics (src/utility/g_asm.asm xget): end coords are inclusive. */
  int w = (x2 - x1) + 1;
  int h = (y2 - y1) + 1;
  int wbytes = (w + 3) / 4;
  uint8_t* outp = (uint8_t*)buff;
  int p, row, bx;
  if (w <= 0 || h <= 0) return;

  {
    uint16_t t;
    t = (uint16_t)wbytes; memcpy(outp + 0, &t, 2);
    t = (uint16_t)h;      memcpy(outp + 2, &t, 2);
    t = (uint16_t)invis;  memcpy(outp + 4, &t, 2);
  }
  outp += 6;

  for (p = 0; p < 4; p++) {
    for (row = 0; row < h; row++) {
      for (bx = 0; bx < wbytes; bx++) {
        int px = x1 + bx * 4 + p;
        int py = y1 + row;
        *outp++ = get_pixel(s, px, py);
      }
    }
  }
}

void GOT_GFXCALL xput(int x,int y,unsigned int pagebase,char *buff) {
  Surf8 dst = resolve_surf(pagebase);
  const uint8_t* b = (const uint8_t*)buff;
  uint16_t wbytes16, h16, invis16;
  int wbytes, h;
  const uint8_t* planes;

  memcpy(&wbytes16, b + 0, 2);
  memcpy(&h16, b + 2, 2);
  memcpy(&invis16, b + 4, 2);
  wbytes = (int)wbytes16;
  h = (int)h16;
  planes = b + 6;
  (void)invis16;
  /* DOS Mode X: offset = y*80 + x/4, truncating x to 4-pixel boundary. */
  x &= ~3;
  /* DOS semantics (src/utility/g_asm.asm xput_plane): treat 0 and 15 as transparent. */
  draw_planar_masked_to_surf(dst, x, y, planes, wbytes, h);
}

void xput2(int x,int y,unsigned int pagebase,char *buff) {
  xput(x, y, pagebase, buff);
}

void GOT_GFXCALL xfput(int x,int y,unsigned int pagebase,char far *buff) {
  Surf8 dst = resolve_surf(pagebase);
  const uint8_t* b = (const uint8_t*)buff;
  const uint8_t* planes = b + 6; /* fixed 16x16 tile format */
  /* DOS Mode X: offset = y*80 + x/4, truncating x to 4-pixel boundary. */
  x &= ~3;
  /* DOS semantics (src/utility/g_asm.asm xfput_plane): treat 0 and 15 as transparent. */
  draw_planar_masked_to_surf(dst, x, y, planes, 4, 16);
}

void GOT_GFXCALL xfarput(int x,int y,unsigned int pagebase,char far *buff) {
  Surf8 dst = resolve_surf(pagebase);
  const uint8_t* b = (const uint8_t*)buff;
  uint16_t wbytes16, h16;
  int wbytes, h;
  const uint8_t* planes;

  memcpy(&wbytes16, b + 0, 2);
  memcpy(&h16, b + 2, 2);
  wbytes = (int)wbytes16;
  h = (int)h16;
  /* xfarput buffer format matches the original assembly:
     widthBytes,height,invis (6 bytes header), followed by planar pixels. */
  planes = b + 6;
  /* DOS Mode X: offset = y*80 + x/4, truncating x to 4-pixel boundary. */
  x &= ~3;
  draw_planar_to_surf(dst, x, y, planes, wbytes, h, 0, 0);
}

void GOT_GFXCALL xtext(int x,int y,unsigned int pagebase,char far *buff,int color) {
  Surf8 dst = resolve_surf(pagebase);
  const uint8_t* b = (const uint8_t*)buff;
  /* 4 planes * (9 rows * 2 bytes) */
  int row, col;
  for (row = 0; row < 9; row++) {
    for (col = 0; col < 8; col++) {
      int plane = col & 3;
      int xbyte = col >> 2;
      uint8_t v = b[plane * 18 + row * 2 + xbyte];
      if (v) {
        put_pixel(dst, x + col, y + row, (uint8_t)color);
      }
    }
  }
}

void GOT_GFXCALL xtext1(int x,int y,unsigned int pagebase,char far *buff,int color) {
  /* Shadow version: draw 1px lower. */
  xtext(x, y + 1, pagebase, buff, color);
}

void GOT_GFXCALL xtextx(int x,int y,unsigned int pagebase,char far *buff,int color) {
  xtext(x, y, pagebase, buff, color);
}

void GOT_GFXCALL xcopyd2d(int SourceStartX, int SourceStartY,
     int SourceEndX, int SourceEndY, int DestStartX,
     int DestStartY, unsigned int SourcePageBase,
     unsigned int DestPageBase, int SourceBitmapWidth,
     int DestBitmapWidth) {
  (void)SourceBitmapWidth;
  (void)DestBitmapWidth;
  blit_rect(resolve_surf(SourcePageBase),
            SourceStartX, SourceStartY, SourceEndX, SourceEndY,
            resolve_surf(DestPageBase),
            DestStartX, DestStartY);
}

void GOT_GFXCALL xcopys2d(int SourceStartX, int SourceStartY,
     int SourceEndX, int SourceEndY, int DestStartX,
     int DestStartY, char* SourcePtr, unsigned int DestPageBase,
     int SourceBitmapWidth, int DestBitmapWidth) {
  /* The native build doesn't use the original Mode X download path; keep a
     simple chunky blit for any remaining call sites. */
  Surf8 dst = resolve_surf(DestPageBase);
  int w = SourceEndX - SourceStartX;
  int h = SourceEndY - SourceStartY;
  int y;
  (void)DestBitmapWidth;
  if (w <= 0 || h <= 0) return;
  for (y = 0; y < h; y++) {
    int sy = SourceStartY + y;
    int dy = DestStartY + y;
    if ((unsigned)dy >= (unsigned)dst.h) continue;
    if ((unsigned)sy >= 0x7fffffffU) continue;
    memcpy(dst.pix + dy * dst.stride + DestStartX,
           (uint8_t*)SourcePtr + sy * SourceBitmapWidth + SourceStartX,
           (size_t)w);
  }
}

void xddfast(int source_x,int source_y, int width, int height,
             int dest_x, int dest_y,
             unsigned int source_page,unsigned int dest_page) {
  xcopyd2d(source_x, source_y, source_x + width, source_y + height,
           dest_x, dest_y, source_page, dest_page, 320, 320);
}

int xsetpal(unsigned char color, unsigned char R,unsigned char G,unsigned char B) {
  g_pal6[color][0] = R;
  g_pal6[color][1] = G;
  g_pal6[color][2] = B;
  g_pal8[color][0] = (uint8_t)((int)R * 255 / 63);
  g_pal8[color][1] = (uint8_t)((int)G * 255 / 63);
  g_pal8[color][2] = (uint8_t)((int)B * 255 / 63);

  g_pal_rgba[color][0] = g_pal8[color][0];
  g_pal_rgba[color][1] = g_pal8[color][1];
  g_pal_rgba[color][2] = g_pal8[color][2];
  g_pal_rgba[color][3] = 255;
  return 0;
}

int xgetpal(char far * pal, int num_colrs, int start_index) {
  int i;
  uint8_t* p = (uint8_t*)pal;
  if (start_index < 0) start_index = 0;
  if (start_index > 255) start_index = 255;
  if (num_colrs < 0) num_colrs = 0;
  if (start_index + num_colrs > 256) num_colrs = 256 - start_index;
  for (i = 0; i < num_colrs; i++) {
    int idx = start_index + i;
    *p++ = g_pal6[idx][0];
    *p++ = g_pal6[idx][1];
    *p++ = g_pal6[idx][2];
  }
  return 0;
}

void GOT_GFXCALL pal_fade_in(char *buff) {
  int step, i;
  uint8_t* p = (uint8_t*)buff;
  for (step = 0; step <= 24; step++) {
    for (i = 0; i < 256; i++) {
      uint8_t r = (uint8_t)((p[i * 3 + 0] * step) / 24);
      uint8_t g = (uint8_t)((p[i * 3 + 1] * step) / 24);
      uint8_t b = (uint8_t)((p[i * 3 + 2] * step) / 24);
      xsetpal((unsigned char)i, r, g, b);
    }
    present_page(g_last_show_pagebase);
    delay(10);
  }
}

void GOT_GFXCALL pal_fade_out(char *buff) {
  int step, i;
  uint8_t saved[256][3];
  (void)buff;
  /* Snapshot the current palette before the loop — xsetpal modifies g_pal6,
     so reading g_pal6 live would cause exponential decay instead of linear. */
  memcpy(saved, g_pal6, sizeof(saved));
  for (step = 24; step >= 0; step--) {
    for (i = 0; i < 256; i++) {
      uint8_t r = (uint8_t)((saved[i][0] * step) / 24);
      uint8_t g = (uint8_t)((saved[i][1] * step) / 24);
      uint8_t b = (uint8_t)((saved[i][2] * step) / 24);
      xsetpal((unsigned char)i, r, g, b);
    }
    present_page(g_last_show_pagebase);
    delay(10);
  }
}

/* Actor rendering (replaces original Mode X assembly). */
void GOT_GFXCALL xerase_actors(ACTOR *act, unsigned int page) {
  int idx;
  int page_i = (page == PAGE0) ? 0 : 1;
  Surf8 bg = g_split_mode ? surf_play_idx(2) : surf_full_idx(2);
  Surf8 dst = resolve_surf(page);
  (void)act;

  for (idx = 0; idx < MAX_ACTORS; idx++) {
    ACTOR* a = &((ACTOR*)act)[idx];
    int x = a->last_x[page_i];
    int y = a->last_y[page_i];

    if (!a->used) {
      if (a->dead) {
        a->dead--;
      }
      else {
        continue;
      }
    }

    blit_rect(bg, x, y, x + 16, y + 16, dst, x, y);
  }
}

static void draw_actor_one(ACTOR* a, Surf8 dst, int page_i) {
  int dir = (int)a->dir;
  /* In the original DOS asm (src/utility/g_asm.asm xdisplay_actors), `next`
     indexes into `frame_sequence`, which yields the actual frame to draw. */
  int fr = (int)(unsigned char)a->frame_sequence[(unsigned char)a->next & 3u];
  MASK_IMAGE* mi;

  if (!a->used) return;
  /* Match DOS asm behavior (src/utility/g_asm.asm xdisplay_actors):
     if (show & 2) skip drawing this actor (blink/invulnerability). */
  if (a->show & 2) return;

  if (dir < 0) dir = 0;
  if (dir > 3) dir = 3;
  if (fr < 0) fr = 0;
  if (fr > 3) fr = 3;

  mi = &a->pic[dir][fr];
  if (!mi->alignments[0]) return;

  /* In the native build, our make_mask() implementation stores a pointer to
     planar pixels in mask_ptr (repurposed) and we ignore the original mask. */
  {
    const uint8_t* planes = (const uint8_t*)mi->alignments[0]->mask_ptr;
    draw_planar_masked_to_surf(dst, a->x, a->y, planes, 4, 16);
  }

  a->last_x[page_i] = a->x;
  a->last_y[page_i] = a->y;
}

void GOT_GFXCALL xdisplay_actors(ACTOR *act, unsigned int page) {
  int idx;
  int page_i = (page == PAGE0) ? 0 : 1;
  Surf8 dst = resolve_surf(page);
  ACTOR* base;

  /* The original asm expects &actor[MAX_ACTORS-1] and walks backward. */
  base = act - (MAX_ACTORS - 1);

  /* Draw all actors except actor[2], then actor[2] last (original detour). */
  for (idx = MAX_ACTORS - 1; idx >= 0; idx--) {
    if (idx == 2) continue;
    draw_actor_one(&base[idx], dst, page_i);
  }
  draw_actor_one(&base[2], dst, page_i);
}
