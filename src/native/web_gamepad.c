#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

/* Browser Gamepad bridge.
   The JS shell polls navigator.getGamepads() and pushes the state into these
   arrays via exported C functions (EMSCRIPTEN_KEEPALIVE). The rest of the
   codebase reads the state via got_web_gamepad_*() helpers.

   Button indices are "custom codes" shared with the in-game settings UI:
     1=A,2=B,3=X,4=Y,7=Start,8=Back,9=LB,10=RB,11..14=D-pad,15=LT,16=RT,17=Guide.
*/

enum { GOT_WEB_MAX_PADS = 4, GOT_WEB_MAX_BTNS = 32, GOT_WEB_MAX_AXES = 8 };

static uint8_t g_web_connected[GOT_WEB_MAX_PADS];
static uint8_t g_web_btn_down[GOT_WEB_MAX_PADS][GOT_WEB_MAX_BTNS];
static float g_web_btn_val[GOT_WEB_MAX_PADS][GOT_WEB_MAX_BTNS];
static float g_web_axis[GOT_WEB_MAX_PADS][GOT_WEB_MAX_AXES];

int got_web_gamepad_is_connected(int pad) {
  if (pad < 0 || pad >= GOT_WEB_MAX_PADS) return 0;
  return g_web_connected[pad] ? 1 : 0;
}

int got_web_gamepad_button_down(int pad, int btn) {
  if (pad < 0 || pad >= GOT_WEB_MAX_PADS) return 0;
  if (btn < 0 || btn >= GOT_WEB_MAX_BTNS) return 0;
  return g_web_btn_down[pad][btn] ? 1 : 0;
}

float got_web_gamepad_button_value(int pad, int btn) {
  if (pad < 0 || pad >= GOT_WEB_MAX_PADS) return 0.0f;
  if (btn < 0 || btn >= GOT_WEB_MAX_BTNS) return 0.0f;
  return g_web_btn_val[pad][btn];
}

float got_web_gamepad_axis(int pad, int axis) {
  if (pad < 0 || pad >= GOT_WEB_MAX_PADS) return 0.0f;
  if (axis < 0 || axis >= GOT_WEB_MAX_AXES) return 0.0f;
  return g_web_axis[pad][axis];
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE void got_web_gamepad_set_connected(int pad, int connected) {
  if (pad < 0 || pad >= GOT_WEB_MAX_PADS) return;
  g_web_connected[pad] = (uint8_t)(connected ? 1 : 0);
}

EMSCRIPTEN_KEEPALIVE void got_web_gamepad_set_button(int pad, int btn, int pressed, float value) {
  if (pad < 0 || pad >= GOT_WEB_MAX_PADS) return;
  if (btn < 0 || btn >= GOT_WEB_MAX_BTNS) return;
  g_web_btn_down[pad][btn] = (uint8_t)(pressed ? 1 : 0);
  g_web_btn_val[pad][btn] = value;
}

EMSCRIPTEN_KEEPALIVE void got_web_gamepad_set_axis(int pad, int axis, float value) {
  if (pad < 0 || pad >= GOT_WEB_MAX_PADS) return;
  if (axis < 0 || axis >= GOT_WEB_MAX_AXES) return;
  g_web_axis[pad][axis] = value;
}
#endif

