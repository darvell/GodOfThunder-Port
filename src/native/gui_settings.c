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

/* Game globals */
extern char far *bg_pics;
extern char hampic[4][262];
extern volatile char key_flag[100];
extern unsigned int display_page;
extern volatile unsigned int timer_cnt, extra_cnt;
extern int restore_screen;
extern int key_fire, key_up, key_down, key_left, key_right, key_magic, key_select;
extern struct sup setup;
extern int music_flag, sound_flag, pcsound_flag, boss_active;
extern char level_type;
extern char last_setup[32];

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
void music_play(int, int);
void music_pause(void);

/* Palette colors */
enum {
    COL_BG      = 215,  /* dark blue background */
    COL_TEXT    = 14,   /* yellow */
    COL_HILIGHT = 54,   /* bright blue */
    COL_FOCUS   = 15,   /* white */
    COL_CAPTURE = 120,  /* red */
    COL_GREEN   = 32,   /* slider fill / "On" */
    COL_GREY    = 206,  /* "Off" / inactive */
    COL_BLACK   = 0
};

/* Tab definitions */
enum { TAB_AUDIO = 0, TAB_DISPLAY, TAB_KEYBOARD, TAB_GAMEPAD, NUM_TABS };

static const char *tab_names[NUM_TABS] = { "Audio", "Display", "Keys", "Gamepad" };

/* Widget types */
enum { W_TOGGLE, W_SELECT, W_SLIDER, W_KEYCAP, W_BTNCAP };

typedef struct {
    const char *label;
    int type;
    int *value;
    int min_val, max_val;
    const char **options;   /* for W_SELECT */
    int num_options;        /* for W_SELECT */
} widget_t;

/* Select option lists */
static const char *snd_opts[] = { "None", "PC Speaker", "Digitized" };
static const char *skill_opts[] = { "Easy", "Normal", "Hard" };

/* Per-tab widget arrays */
static widget_t audio_widgets[3];
static widget_t display_widgets[2];
static widget_t keyboard_widgets[7];
static widget_t gamepad_widgets[8];

static int tab_widget_count[NUM_TABS];
static widget_t *tab_widgets[NUM_TABS];

/* Skill level is not in g_config — it's in setup.skill. We mirror it. */
static int skill_mirror;

static void init_widgets(void) {
    skill_mirror = setup.skill;

    /* Audio tab */
    audio_widgets[0] = (widget_t){ "Sound",  W_SELECT, &g_config.sound_type, 0, 2, snd_opts, 3 };
    audio_widgets[1] = (widget_t){ "Music",  W_TOGGLE, &g_config.music_on,   0, 1, NULL, 0 };
    audio_widgets[2] = (widget_t){ "Skill",  W_SELECT, &skill_mirror,        0, 2, skill_opts, 3 };
    tab_widgets[TAB_AUDIO] = audio_widgets;
    tab_widget_count[TAB_AUDIO] = 3;

    /* Display tab */
    display_widgets[0] = (widget_t){ "Fullscreen",    W_TOGGLE, &g_config.fullscreen,    0, 1, NULL, 0 };
    display_widgets[1] = (widget_t){ "Screen Scroll", W_TOGGLE, &g_config.screen_scroll, 0, 1, NULL, 0 };
    tab_widgets[TAB_DISPLAY] = display_widgets;
    tab_widget_count[TAB_DISPLAY] = 2;

    /* Keyboard tab */
    keyboard_widgets[0] = (widget_t){ "Fire",   W_KEYCAP, &g_config.kb_fire,   0, 0, NULL, 0 };
    keyboard_widgets[1] = (widget_t){ "Magic",  W_KEYCAP, &g_config.kb_magic,  0, 0, NULL, 0 };
    keyboard_widgets[2] = (widget_t){ "Select", W_KEYCAP, &g_config.kb_select, 0, 0, NULL, 0 };
    keyboard_widgets[3] = (widget_t){ "Up",     W_KEYCAP, &g_config.kb_up,     0, 0, NULL, 0 };
    keyboard_widgets[4] = (widget_t){ "Down",   W_KEYCAP, &g_config.kb_down,   0, 0, NULL, 0 };
    keyboard_widgets[5] = (widget_t){ "Left",   W_KEYCAP, &g_config.kb_left,   0, 0, NULL, 0 };
    keyboard_widgets[6] = (widget_t){ "Right",  W_KEYCAP, &g_config.kb_right,  0, 0, NULL, 0 };
    tab_widgets[TAB_KEYBOARD] = keyboard_widgets;
    tab_widget_count[TAB_KEYBOARD] = 7;

    /* Gamepad tab */
    gamepad_widgets[0] = (widget_t){ "Fire",      W_BTNCAP, &g_config.gp_fire,      0, 0, NULL, 0 };
    gamepad_widgets[1] = (widget_t){ "Magic",     W_BTNCAP, &g_config.gp_magic,     0, 0, NULL, 0 };
    gamepad_widgets[2] = (widget_t){ "Select",    W_BTNCAP, &g_config.gp_select,    0, 0, NULL, 0 };
    gamepad_widgets[3] = (widget_t){ "Confirm",   W_BTNCAP, &g_config.gp_confirm,   0, 0, NULL, 0 };
    gamepad_widgets[4] = (widget_t){ "Pause",     W_BTNCAP, &g_config.gp_pause,     0, 0, NULL, 0 };
    gamepad_widgets[5] = (widget_t){ "Prev Item", W_BTNCAP, &g_config.gp_item_prev, 0, 0, NULL, 0 };
    gamepad_widgets[6] = (widget_t){ "Next Item", W_BTNCAP, &g_config.gp_item_next, 0, 0, NULL, 0 };
    gamepad_widgets[7] = (widget_t){ "Deadzone",  W_SLIDER, &g_config.gp_deadzone,  0, 100, NULL, 0 };
    tab_widgets[TAB_GAMEPAD] = gamepad_widgets;
    tab_widget_count[TAB_GAMEPAD] = 8;
}

/*=========================================================================*/
static void draw_tabs(int tab, unsigned int pg) {
    int i, x;
    x = 24;
    for (i = 0; i < NUM_TABS; i++) {
        int col = (i == tab) ? COL_FOCUS : COL_GREY;
        xprint(x, 20, (char *)tab_names[i], pg, col);
        x += (int)(strlen(tab_names[i]) * 8) + 16;
    }
    /* Separator line */
    xfillrectangle(16, 32, 304, 33, pg, COL_HILIGHT);
}

/*=========================================================================*/
static void draw_widget(widget_t *w, int y, int focused, unsigned int pg) {
    int label_col = focused ? COL_FOCUS : COL_TEXT;
    char buf[32];

    /* Label on left */
    xprint(32, y, (char *)w->label, pg, label_col);

    /* Value on right */
    switch (w->type) {
        case W_TOGGLE: {
            int on = *(w->value);
            if (on) {
                xprint(200, y, "On", pg, COL_GREEN);
            } else {
                xprint(200, y, "Off", pg, COL_GREY);
            }
            break;
        }
        case W_SELECT: {
            int v = *(w->value);
            if (v < 0) v = 0;
            if (v >= w->num_options) v = w->num_options - 1;
            snprintf(buf, sizeof(buf), "< %s >", w->options[v]);
            xprint(176, y, buf, pg, focused ? COL_FOCUS : COL_TEXT);
            break;
        }
        case W_SLIDER: {
            int v = *(w->value);
            int fill_w;
            /* Draw bar background */
            xfillrectangle(176, y, 256, y + 8, pg, COL_BLACK);
            /* Fill proportional to value */
            fill_w = (v * 80) / 100;
            if (fill_w > 0) {
                xfillrectangle(176, y, 176 + fill_w, y + 8, pg, COL_GREEN);
            }
            /* Numeric value */
            snprintf(buf, sizeof(buf), "%d", v);
            xprint(264, y, buf, pg, COL_TEXT);
            break;
        }
        case W_KEYCAP: {
            const char *name = gui_scancode_name(*(w->value));
            xprint(200, y, (char *)name, pg, focused ? COL_HILIGHT : COL_TEXT);
            break;
        }
        case W_BTNCAP: {
            const char *name = gui_gamepad_button_name(*(w->value));
            xprint(200, y, (char *)name, pg, focused ? COL_HILIGHT : COL_TEXT);
            break;
        }
    }
}

/*=========================================================================*/
static void draw_full_page(int tab, int focus, int cursor_frame, unsigned int pg) {
    widget_t *wlist;
    int wcount, i, y;

    /* Clear content area */
    xfillrectangle(0, 0, 320, 192, pg, COL_BG);

    /* Draw ornate frame */
    gui_draw_frame(16, 8, 304, 184, pg);

    /* Title */
    xprint(112, 10, "Settings", pg, COL_HILIGHT);

    /* Tabs */
    draw_tabs(tab, pg);

    /* Widgets */
    wlist = tab_widgets[tab];
    wcount = tab_widget_count[tab];
    for (i = 0; i < wcount; i++) {
        y = 40 + (i * 16);
        draw_widget(&wlist[i], y, i == focus, pg);
    }

    /* Hammer cursor next to focused widget */
    {
        int cy = 40 + (focus * 16);
        xput(16, cy, pg, hampic[cursor_frame & 3]);
    }

    /* Footer hint */
    xprint(32, 172, "Tab:switch  Esc:cancel", pg, COL_GREY);
}

/*=========================================================================*/
static void apply_audio_immediate(void) {
    got_config_apply();
    memcpy(last_setup, &setup, 32);
}

/*=========================================================================*/
static void handle_fullscreen_toggle(void) {
    ToggleBorderlessWindowed();
}

/*=========================================================================*/
void got_settings_menu(void) {
    unsigned int pg;
    int tab, focus, cursor_frame, kf, key;
    int prev_fullscreen;
    got_config_t saved;
    int saved_skill;
    /* Tab switching edge detection */
    int tab_prev, lb_prev, rb_prev;

    pg = display_page;
    tab = 0;
    focus = 0;
    cursor_frame = 0;
    kf = 0;

    /* Snapshot for cancel/revert */
    saved = g_config;
    saved_skill = setup.skill;

    init_widgets();

    wait_not_response();
    extra_cnt = 0;

    tab_prev = 0;
    lb_prev = 0;
    rb_prev = 0;

    while (1) {
        int wcount = tab_widget_count[tab];
        widget_t *wlist = tab_widgets[tab];
        widget_t *cur_w;

        if (focus >= wcount) focus = wcount - 1;
        if (focus < 0) focus = 0;
        cur_w = &wlist[focus];

        /* Draw frame */
        draw_full_page(tab, focus, cursor_frame, pg);
        cursor_frame = (cursor_frame + 1) & 3;

        xshowpage(pg);

        timer_cnt = 0;
        while (timer_cnt < 8) rotate_pal();

        if (extra_cnt > 15) {
            kf = 0;
            extra_cnt = 0;
        }

        /* Tab switching: keyboard (Tab/Shift+Tab) */
        {
            int tab_now = key_flag[15]; /* TAB scancode */
            if (tab_now && !tab_prev) {
                if (key_flag[42]) { /* LSHIFT */
                    tab--;
                    if (tab < 0) tab = NUM_TABS - 1;
                } else {
                    tab++;
                    if (tab >= NUM_TABS) tab = 0;
                }
                focus = 0;
                play_sound(WOOP, 1);
            }
            tab_prev = tab_now;
        }

        /* Tab switching: gamepad LB/RB (use platform bridge; works on web) */
        if (got_platform_gamepad_is_available(0)) {
            int lb = got_platform_gamepad_button_down(0, 9);   /* LB */
            int rb = got_platform_gamepad_button_down(0, 10);  /* RB */
            if (lb && !lb_prev) {
                tab--;
                if (tab < 0) tab = NUM_TABS - 1;
                focus = 0;
                play_sound(WOOP, 1);
            }
            if (rb && !rb_prev) {
                tab++;
                if (tab >= NUM_TABS) tab = 0;
                focus = 0;
                play_sound(WOOP, 1);
            }
            lb_prev = lb;
            rb_prev = rb;
        }

        /* Input */
        key = get_response();

        if (key == ESC) {
            /* Cancel: revert all changes */
            g_config = saved;
            setup.skill = saved_skill;
            got_config_apply();
            break;
        }

        /* Navigation */
        if (key_flag[UP] || key_flag[key_up]) {
            if (!kf) {
                focus--;
                if (focus < 0) focus = wcount - 1;
                play_sound(WOOP, 1);
                kf = 1;
                extra_cnt = 0;
            }
        }
        else if (key_flag[DOWN] || key_flag[key_down]) {
            if (!kf) {
                focus++;
                if (focus >= wcount) focus = 0;
                play_sound(WOOP, 1);
                kf = 1;
                extra_cnt = 0;
            }
        }
        else if (key_flag[LEFT] || key_flag[key_left]) {
            if (!kf) {
                /* Adjust widget left */
                switch (cur_w->type) {
                    case W_TOGGLE:
                        *(cur_w->value) = !(*(cur_w->value));
                        play_sound(WOOP, 1);
                        if (cur_w->value == &g_config.music_on || cur_w->value == &g_config.sound_type) {
                            apply_audio_immediate();
                        }
                        if (cur_w->value == &g_config.fullscreen) {
                            handle_fullscreen_toggle();
                        }
                        break;
                    case W_SELECT:
                        (*(cur_w->value))--;
                        if (*(cur_w->value) < cur_w->min_val) *(cur_w->value) = cur_w->max_val;
                        play_sound(WOOP, 1);
                        if (cur_w->value == &g_config.sound_type || cur_w->value == &g_config.music_on) {
                            apply_audio_immediate();
                        }
                        if (cur_w->value == &skill_mirror) {
                            setup.skill = skill_mirror;
                        }
                        break;
                    case W_SLIDER:
                        (*(cur_w->value)) -= 5;
                        if (*(cur_w->value) < cur_w->min_val) *(cur_w->value) = cur_w->min_val;
                        play_sound(WOOP, 1);
                        break;
                    default:
                        break;
                }
                kf = 1;
                extra_cnt = 0;
            }
        }
        else if (key_flag[RIGHT] || key_flag[key_right]) {
            if (!kf) {
                /* Adjust widget right */
                switch (cur_w->type) {
                    case W_TOGGLE:
                        *(cur_w->value) = !(*(cur_w->value));
                        play_sound(WOOP, 1);
                        if (cur_w->value == &g_config.music_on || cur_w->value == &g_config.sound_type) {
                            apply_audio_immediate();
                        }
                        if (cur_w->value == &g_config.fullscreen) {
                            handle_fullscreen_toggle();
                        }
                        break;
                    case W_SELECT:
                        (*(cur_w->value))++;
                        if (*(cur_w->value) > cur_w->max_val) *(cur_w->value) = cur_w->min_val;
                        play_sound(WOOP, 1);
                        if (cur_w->value == &g_config.sound_type || cur_w->value == &g_config.music_on) {
                            apply_audio_immediate();
                        }
                        if (cur_w->value == &skill_mirror) {
                            setup.skill = skill_mirror;
                        }
                        break;
                    case W_SLIDER:
                        (*(cur_w->value)) += 5;
                        if (*(cur_w->value) > cur_w->max_val) *(cur_w->value) = cur_w->max_val;
                        play_sound(WOOP, 1);
                        break;
                    default:
                        break;
                }
                kf = 1;
                extra_cnt = 0;
            }
        }
        else if (key == ENTER || key == SPACE || key == key_fire || key == key_magic) {
            /* Activate: capture widgets */
            switch (cur_w->type) {
                case W_KEYCAP: {
                    int sc = gui_capture_key(pg);
                    if (sc > 0) {
                        *(cur_w->value) = sc;
                        play_sound(WOOP, 1);
                    }
                    break;
                }
                case W_BTNCAP: {
                    int btn = gui_capture_gamepad_button(pg);
                    if (btn >= 0) {
                        *(cur_w->value) = btn;
                        play_sound(WOOP, 1);
                    }
                    break;
                }
                case W_TOGGLE:
                    *(cur_w->value) = !(*(cur_w->value));
                    play_sound(WOOP, 1);
                    if (cur_w->value == &g_config.music_on || cur_w->value == &g_config.sound_type) {
                        apply_audio_immediate();
                    }
                    if (cur_w->value == &g_config.fullscreen) {
                        handle_fullscreen_toggle();
                    }
                    break;
                case W_SELECT:
                    (*(cur_w->value))++;
                    if (*(cur_w->value) > cur_w->max_val) *(cur_w->value) = cur_w->min_val;
                    play_sound(WOOP, 1);
                    if (cur_w->value == &g_config.sound_type || cur_w->value == &g_config.music_on) {
                        apply_audio_immediate();
                    }
                    if (cur_w->value == &skill_mirror) {
                        setup.skill = skill_mirror;
                    }
                    break;
                default:
                    break;
            }
        }
        else {
            kf = 0;
        }
    }

    /* Apply final state and save */
    setup.skill = skill_mirror;
    got_config_apply();
    got_config_save("GOT.CFG");
    memcpy(last_setup, &setup, 32);
    restore_screen = 1;
}
