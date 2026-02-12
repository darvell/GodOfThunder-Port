#include "joy.h"

void read_joystick(joystick_input* joy) {
  if (!joy) return;
  joy->x = 0;
  joy->y = 0;
  joy->b1 = 0;
  joy->b2 = 0;
}

