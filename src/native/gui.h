#ifndef GOT_GUI_H
#define GOT_GUI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Keyboard scancodes (DOS set 1) */
    int kb_fire, kb_magic, kb_select;
    int kb_up, kb_down, kb_left, kb_right;
    /* Gamepad buttons (raylib GamepadButton enum values) */
    int gp_fire, gp_magic, gp_select, gp_confirm;
    int gp_pause, gp_menu_confirm;
    int gp_item_prev, gp_item_next;
    int gp_deadzone;    /* 0-100, maps to 0.0-1.0 */
    /* Display */
    int fullscreen;
    int screen_scroll; /* scroll between screens */
    /* Audio */
    int sound_type;     /* 0=none, 1=pc, 2=digi */
    int music_on;
} got_config_t;

extern got_config_t g_config;

void got_config_set_defaults(void);
int  got_config_load(const char *path);
int  got_config_save(const char *path);
void got_config_apply(void);

/* Reusable drawing helpers */
void gui_draw_frame(int x1, int y1, int x2, int y2, unsigned int pg);
const char *gui_scancode_name(int scancode);
const char *gui_gamepad_button_name(int button);
int  gui_capture_key(unsigned int pg);
int  gui_capture_gamepad_button(unsigned int pg);

/* Main entry point — blocking modal */
void got_settings_menu(void);

#ifdef __cplusplus
}
#endif

#endif /* GOT_GUI_H */
