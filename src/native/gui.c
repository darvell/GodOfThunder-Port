#include "gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

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

/* Game globals (linked from episode code) */
extern char far *bg_pics;
extern char hampic[4][262];
extern volatile char key_flag[100];
extern unsigned int display_page;
extern volatile unsigned int timer_cnt, extra_cnt;
extern int restore_screen;
extern int key_fire, key_up, key_down, key_left, key_right, key_magic, key_select;
extern struct sup setup;
extern int music_flag, sound_flag, pcsound_flag;

/* Functions from game code */
void xfillrectangle(int, int, int, int, unsigned int, int);
void xprint(int, int, char *, unsigned int, int);
void xfput(int, int, unsigned int, char far *);
void xput(int, int, unsigned int, char *);
void xshowpage(unsigned int);
void play_sound(int, int);
void rotate_pal(void);
int  get_response(void);
void wait_not_response(void);
void d_restore(void);
void got_platform_pump(void);

got_config_t g_config;

/*=========================================================================*/
void got_config_set_defaults(void) {
    memset(&g_config, 0, sizeof(g_config));
    /* Keyboard scancodes */
    g_config.kb_fire   = ALT;    /* 56 */
    g_config.kb_magic  = CTRL;   /* 29 */
    g_config.kb_select = SPACE;  /* 57 */
    g_config.kb_up     = UP;     /* 72 */
    g_config.kb_down   = DOWN;   /* 80 */
    g_config.kb_left   = LEFT;   /* 75 */
    g_config.kb_right  = RIGHT;  /* 77 */
    /* Gamepad buttons (custom mapping; see gui_gamepad_button_name()) */
    g_config.gp_fire         = 1;   /* GAMEPAD_BUTTON_RIGHT_FACE_DOWN (A) */
    g_config.gp_magic        = 2;   /* GAMEPAD_BUTTON_RIGHT_FACE_RIGHT (B) */
    g_config.gp_select       = 3;   /* GAMEPAD_BUTTON_RIGHT_FACE_LEFT (X) */
    g_config.gp_confirm      = 4;   /* GAMEPAD_BUTTON_RIGHT_FACE_UP (Y) */
    g_config.gp_pause        = 7;   /* GAMEPAD_BUTTON_MIDDLE_RIGHT (Start) */
    g_config.gp_menu_confirm = 8;   /* Back */
    g_config.gp_item_prev    = 9;   /* GAMEPAD_BUTTON_LEFT_TRIGGER_1 (LB) */
    g_config.gp_item_next    = 10;  /* GAMEPAD_BUTTON_RIGHT_TRIGGER_1 (RB) */
    g_config.gp_deadzone     = 40;
    /* Display */
    g_config.fullscreen    = 0;
    g_config.screen_scroll = 1;
    /* Audio */
    g_config.sound_type = 2;  /* digi */
    g_config.music_on   = 1;
}

/*=========================================================================*/
int got_config_load(const char *path) {
    FILE *fp;
    char line[128];

    fp = fopen(path, "r");
    if (!fp) return 0;

    while (fgets(line, sizeof(line), fp)) {
        char *eq;
        char *key;
        int val;

        /* strip newline */
        { char *nl = strchr(line, '\n'); if (nl) *nl = 0; }
        { char *cr = strchr(line, '\r'); if (cr) *cr = 0; }

        /* skip comments and blanks */
        if (line[0] == '#' || line[0] == 0) continue;

        eq = strchr(line, '=');
        if (!eq) continue;

        *eq = 0;
        key = line;
        val = atoi(eq + 1);

        if      (!strcmp(key, "kb_fire"))         g_config.kb_fire = val;
        else if (!strcmp(key, "kb_magic"))        g_config.kb_magic = val;
        else if (!strcmp(key, "kb_select"))       g_config.kb_select = val;
        else if (!strcmp(key, "kb_up"))           g_config.kb_up = val;
        else if (!strcmp(key, "kb_down"))         g_config.kb_down = val;
        else if (!strcmp(key, "kb_left"))         g_config.kb_left = val;
        else if (!strcmp(key, "kb_right"))        g_config.kb_right = val;
        else if (!strcmp(key, "gp_fire"))         g_config.gp_fire = val;
        else if (!strcmp(key, "gp_magic"))        g_config.gp_magic = val;
        else if (!strcmp(key, "gp_select"))       g_config.gp_select = val;
        else if (!strcmp(key, "gp_confirm"))      g_config.gp_confirm = val;
        else if (!strcmp(key, "gp_pause"))        g_config.gp_pause = val;
        else if (!strcmp(key, "gp_menu_confirm")) g_config.gp_menu_confirm = val;
        else if (!strcmp(key, "gp_item_prev"))    g_config.gp_item_prev = val;
        else if (!strcmp(key, "gp_item_next"))    g_config.gp_item_next = val;
        else if (!strcmp(key, "gp_deadzone"))     g_config.gp_deadzone = val;
        else if (!strcmp(key, "fullscreen"))      g_config.fullscreen = val;
        else if (!strcmp(key, "screen_scroll"))   g_config.screen_scroll = val;
        else if (!strcmp(key, "sound_type"))      g_config.sound_type = val;
        else if (!strcmp(key, "music_on"))        g_config.music_on = val;
    }

    fclose(fp);
    return 1;
}

/*=========================================================================*/
int got_config_save(const char *path) {
    FILE *fp;

    fp = fopen(path, "w");
    if (!fp) return 0;

    fprintf(fp, "# God of Thunder settings\n");
    fprintf(fp, "kb_fire=%d\n",         g_config.kb_fire);
    fprintf(fp, "kb_magic=%d\n",        g_config.kb_magic);
    fprintf(fp, "kb_select=%d\n",       g_config.kb_select);
    fprintf(fp, "kb_up=%d\n",           g_config.kb_up);
    fprintf(fp, "kb_down=%d\n",         g_config.kb_down);
    fprintf(fp, "kb_left=%d\n",         g_config.kb_left);
    fprintf(fp, "kb_right=%d\n",        g_config.kb_right);
    fprintf(fp, "gp_fire=%d\n",         g_config.gp_fire);
    fprintf(fp, "gp_magic=%d\n",        g_config.gp_magic);
    fprintf(fp, "gp_select=%d\n",       g_config.gp_select);
    fprintf(fp, "gp_confirm=%d\n",      g_config.gp_confirm);
    fprintf(fp, "gp_pause=%d\n",        g_config.gp_pause);
    fprintf(fp, "gp_menu_confirm=%d\n", g_config.gp_menu_confirm);
    fprintf(fp, "gp_item_prev=%d\n",    g_config.gp_item_prev);
    fprintf(fp, "gp_item_next=%d\n",    g_config.gp_item_next);
    fprintf(fp, "gp_deadzone=%d\n",     g_config.gp_deadzone);
    fprintf(fp, "fullscreen=%d\n",      g_config.fullscreen);
    fprintf(fp, "screen_scroll=%d\n",  g_config.screen_scroll);
    fprintf(fp, "sound_type=%d\n",      g_config.sound_type);
    fprintf(fp, "music_on=%d\n",        g_config.music_on);

    fclose(fp);
    return 1;
}

/*=========================================================================*/
void got_config_apply(void) {
    /* Keyboard bindings */
    key_fire   = g_config.kb_fire;
    key_magic  = g_config.kb_magic;
    key_select = g_config.kb_select;
    key_up     = g_config.kb_up;
    key_down   = g_config.kb_down;
    key_left   = g_config.kb_left;
    key_right  = g_config.kb_right;

    /* Audio */
    switch (g_config.sound_type) {
        case 0:
            setup.dig_sound = 0;
            setup.pc_sound  = 0;
            break;
        case 1:
            setup.dig_sound = 0;
            setup.pc_sound  = 1;
            break;
        case 2:
        default:
            setup.dig_sound = 1;
            setup.pc_sound  = 0;
            break;
    }
    setup.music = g_config.music_on ? 1 : 0;

    /* Display */
    setup.scroll_flag = g_config.screen_scroll ? 1 : 0;
}

/*=========================================================================*/
void gui_draw_frame(int x1, int y1, int x2, int y2, unsigned int pg) {
    int s, i, num_rows;

    xfillrectangle(x1, y1, x2, y2, pg, 215);

    /* Corner sprites (bg_pics indices 192-195) */
    xfput(x1 - 16, y1 - 16, pg, (char far *)(bg_pics + (192 * 262)));
    xfput(x2,      y1 - 16, pg, (char far *)(bg_pics + (193 * 262)));
    xfput(x1 - 16, y2,      pg, (char far *)(bg_pics + (194 * 262)));
    xfput(x2,      y2,      pg, (char far *)(bg_pics + (195 * 262)));

    /* Top/bottom borders (indices 196-197) */
    s = (x2 - x1) / 16;
    for (i = 0; i < s; i++) {
        xfput(x1 + (i * 16), y1 - 16, pg, (char far *)(bg_pics + (196 * 262)));
        xfput(x1 + (i * 16), y2,      pg, (char far *)(bg_pics + (197 * 262)));
    }

    /* Left/right borders (indices 198-199) */
    num_rows = (y2 - y1) / 16;
    for (i = 0; i < num_rows; i++) {
        xfput(x1 - 16, y1 + (i * 16), pg, (char far *)(bg_pics + (198 * 262)));
        xfput(x2,      y1 + (i * 16), pg, (char far *)(bg_pics + (199 * 262)));
    }
}

/*=========================================================================*/
const char *gui_scancode_name(int scancode) {
    static const struct { int code; const char *name; } tbl[] = {
        {  1, "ESC"     }, {  2, "1"       }, {  3, "2"       }, {  4, "3"       },
        {  5, "4"       }, {  6, "5"       }, {  7, "6"       }, {  8, "7"       },
        {  9, "8"       }, { 10, "9"       }, { 11, "0"       }, { 12, "-"       },
        { 13, "="       }, { 14, "BKSP"    }, { 15, "TAB"     }, { 16, "Q"       },
        { 17, "W"       }, { 18, "E"       }, { 19, "R"       }, { 20, "T"       },
        { 21, "Y"       }, { 22, "U"       }, { 23, "I"       }, { 24, "O"       },
        { 25, "P"       }, { 26, "["       }, { 27, "]"       }, { 28, "ENTER"   },
        { 29, "CTRL"    }, { 30, "A"       }, { 31, "S"       }, { 32, "D"       },
        { 33, "F"       }, { 34, "G"       }, { 35, "H"       }, { 36, "J"       },
        { 37, "K"       }, { 38, "L"       }, { 39, ";"       }, { 40, "'"       },
        { 41, "`"       }, { 42, "LSHIFT"  }, { 43, "\\"      }, { 44, "Z"       },
        { 45, "X"       }, { 46, "C"       }, { 47, "V"       }, { 48, "B"       },
        { 49, "N"       }, { 50, "M"       }, { 51, ","       }, { 52, "."       },
        { 53, "/"       }, { 54, "RSHIFT"  }, { 56, "ALT"     }, { 57, "SPACE"   },
        { 59, "F1"      }, { 60, "F2"      }, { 61, "F3"      }, { 62, "F4"      },
        { 63, "F5"      }, { 64, "F6"      }, { 65, "F7"      }, { 66, "F8"      },
        { 67, "F9"      }, { 68, "F10"     }, { 71, "HOME"    }, { 72, "UP"      },
        { 73, "PGUP"    }, { 75, "LEFT"    }, { 77, "RIGHT"   }, { 79, "END"     },
        { 80, "DOWN"    }, { 81, "PGDN"    },
        {  0, NULL      }
    };
    int i;
    for (i = 0; tbl[i].name; i++) {
        if (tbl[i].code == scancode) return tbl[i].name;
    }
    return "???";
}

/*=========================================================================*/
const char *gui_gamepad_button_name(int button) {
    static const struct { int code; const char *name; } tbl[] = {
        {  0, "Unknown"  },
        {  1, "A"        }, {  2, "B"        }, {  3, "X"        }, {  4, "Y"        },
        {  5, "L Stick"  }, {  6, "R Stick"  },
        {  7, "Start"    }, {  8, "Back"     },
        {  9, "LB"       }, { 10, "RB"       },
        { 11, "D-Up"     }, { 12, "D-Down"   }, { 13, "D-Left"  }, { 14, "D-Right" },
        { 15, "LT"       }, { 16, "RT"       },
        { 17, "Guide"    },
        { -1, NULL       }
    };
    int i;
    for (i = 0; tbl[i].name; i++) {
        if (tbl[i].code == button) return tbl[i].name;
    }
    return "???";
}

/*=========================================================================*/
int gui_capture_key(unsigned int pg) {
    int i;

    /* Show prompt */
    xfillrectangle(80, 88, 240, 104, pg, 215);
    xprint(88, 92, "Press key...", pg, 120);
    timer_cnt = 0;
    while (timer_cnt < 10) rotate_pal();

    /* Wait for all keys released */
    while (1) {
        int any = 0;
        got_platform_pump();
        for (i = 1; i < 100; i++) {
            if (key_flag[i]) { any = 1; break; }
        }
        if (!any) break;
        rotate_pal();
    }

    /* Wait for a key press */
    while (1) {
        got_platform_pump();
        if (key_flag[ESC]) return 0;  /* cancelled */
        for (i = 1; i < 100; i++) {
            if (i == ESC) continue;
            if (key_flag[i]) return i;
        }
        rotate_pal();
    }
}

/*=========================================================================*/
int gui_capture_gamepad_button(unsigned int pg) {
    int btn;

    /* Show prompt */
    xfillrectangle(80, 88, 240, 104, pg, 215);
    xprint(88, 92, "Press btn...", pg, 120);
    timer_cnt = 0;
    while (timer_cnt < 10) rotate_pal();

    /* Wait for all buttons released */
    while (1) {
        int any = 0;
        got_platform_pump();
        if (!got_platform_gamepad_is_available(0)) return -1;
        for (btn = 0; btn <= 17; btn++) {
            if (got_platform_gamepad_button_down(0, btn)) { any = 1; break; }
        }
        if (key_flag[ESC]) return -1;
        if (!any) break;
        rotate_pal();
    }

    /* Wait for a button press */
    while (1) {
        got_platform_pump();
        if (key_flag[ESC]) return -1;
        if (!got_platform_gamepad_is_available(0)) return -1;
        for (btn = 0; btn <= 17; btn++) {
            if (got_platform_gamepad_button_down(0, btn)) return btn;
        }
        rotate_pal();
    }
}
