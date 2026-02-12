#ifndef GOT_PLATFORM_H
#define GOT_PLATFORM_H

#include <stdint.h>

/* Video */
void got_platform_video_init(void);
void got_platform_video_shutdown(void);
void got_platform_set_split(int on);

/* Called by the renderer to keep timers/input/audio moving. */
void got_platform_pump(void);

/* Audio */
int got_platform_audio_init(void);
void got_platform_audio_shutdown(void);

/* Key translation: platform backend updates the game's key_flag array. */
int got_platform_map_key_to_dos_scancode(int raylib_key);

/* Mouse helpers (for tools/editor code). Coords are in 320x240 game pixels. */
void got_platform_mouse_get_position(int* out_x, int* out_y);
int got_platform_mouse_button_down(int button); /* 0=left, 1=right, 2=middle */

/* Gamepad helpers (optional; input is also mapped into key_flag[]).
   Buttons use the same 0..17 codes as gui_gamepad_button_name():
     1=A,2=B,3=X,4=Y,7=Start,8=Back,9=LB,10=RB,11..14=D-pad,15=LT,16=RT,17=Guide.
   Axes use raylib's GamepadAxis enum values (0..5). */
int got_platform_gamepad_is_available(int gamepad);
int got_platform_gamepad_button_down(int gamepad, int button);
float got_platform_gamepad_axis_movement(int gamepad, int axis);

#endif
