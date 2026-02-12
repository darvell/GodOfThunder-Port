/* game_define.h - Unified type definitions and constants for God of Thunder
 *
 * Replaces 1_define.h / 2_define.h / 3_define.h in the unified build.
 * All episode-specific constants are resolved at runtime via the episode table.
 */
#ifndef GOT_GAME_DEFINE_H
#define GOT_GAME_DEFINE_H

#ifdef __llvm__
#include <stdint.h>
#endif

#ifndef __WATCOMC__
#undef outportb
#undef inportb
#endif

/* ======================================================================== */
/* Type definitions (identical across all episodes)                          */
/* ======================================================================== */

typedef struct {
   int          image_width;
   unsigned int image_ptr;
   char far     *mask_ptr;
} ALIGNED_MASK_IMAGE;

typedef struct {
   ALIGNED_MASK_IMAGE *alignments[4];
} MASK_IMAGE;

#include "level.h"

typedef struct{                    /* size=256 */
       /* first part loaded from disk (size=40) */
       char move;
       char width;
       char height;
       char directions;
       char frames;
       char frame_speed;
       char frame_sequence[4];
       char speed;
       char size_x;
       char size_y;
       char strength;
       char health;
       char num_moves;
       char shot_type;
       char shot_pattern;
       char shots_allowed;
       char solid;
       char flying;
       char rating;
       char type;
       char name[9];
       char func_num;
       char func_pass;
       #ifdef __llvm__
       int16_t magic_hurts;
       #else
       int  magic_hurts;
       #endif
       char future1[4];

       /* the rest is dynamic (size=216) */
       MASK_IMAGE pic[4][4];
       char frame_count;
       char dir;
       char last_dir;
       int  x;
       int  y;
       int  center;
       int  last_x[2];
       int  last_y[2];
       char used;
       char next;
       char speed_count;
       char vunerable;
       char shot_cnt;
       char num_shots;
       char creator;
       char pause;
       char actor_num;
       char move_count;
       char dead;
       char toggle;
       char center_x;
       char center_y;
       char show;
       char temp1;
       char temp2;
       char counter;
       char move_counter;
       char edge_counter;
       char temp3;
       char temp4;
       char temp5;
       char hit_thor;
       int  rand;
       char init_dir;
       char pass_value;
       char shot_actor;
       char magic_hit;
       char temp6;
       int  i1,i2,i3,i4,i5,i6;
       char init_health;
       char talk_counter;
       char etype;
       char future2[25];
} ACTOR;

#include "actornfo.h"
#include "actordat.h"

typedef struct sup{
       unsigned int  f00 :1;
       unsigned int  f01 :1;
       unsigned int  f02 :1;
       unsigned int  f03 :1;
       unsigned int  f04 :1;
       unsigned int  f05 :1;
       unsigned int  f06 :1;
       unsigned int  f07 :1;

       unsigned int  f08 :1;
       unsigned int  f09 :1;
       unsigned int  f10 :1;
       unsigned int  f11 :1;
       unsigned int  f12 :1;
       unsigned int  f13 :1;
       unsigned int  f14 :1;
       unsigned int  f15 :1;

       unsigned int  f16 :1;
       unsigned int  f17 :1;
       unsigned int  f18 :1;
       unsigned int  f19 :1;
       unsigned int  f20 :1;
       unsigned int  f21 :1;
       unsigned int  f22 :1;
       unsigned int  f23 :1;

       unsigned int  f24 :1;
       unsigned int  f25 :1;
       unsigned int  f26 :1;
       unsigned int  f27 :1;
       unsigned int  f28 :1;
       unsigned int  f29 :1;
       unsigned int  f30 :1;
       unsigned int  f31 :1;

       unsigned int  f32 :1;
       unsigned int  f33 :1;
       unsigned int  f34 :1;
       unsigned int  f35 :1;
       unsigned int  f36 :1;
       unsigned int  f37 :1;
       unsigned int  f38 :1;
       unsigned int  f39 :1;

       unsigned int  f40 :1;
       unsigned int  f41 :1;
       unsigned int  f42 :1;
       unsigned int  f43 :1;
       unsigned int  f44 :1;
       unsigned int  f45 :1;
       unsigned int  f46 :1;
       unsigned int  f47 :1;

       unsigned int  f48 :1;
       unsigned int  f49 :1;
       unsigned int  f50 :1;
       unsigned int  f51 :1;
       unsigned int  f52 :1;
       unsigned int  f53 :1;
       unsigned int  f54 :1;
       unsigned int  f55 :1;

       unsigned int  f56 :1;
       unsigned int  f57 :1;
       unsigned int  f58 :1;
       unsigned int  f59 :1;
       unsigned int  f60 :1;
       unsigned int  f61 :1;
       unsigned int  f62 :1;
       unsigned int  f63 :1;

       char value[16];
       char junk;
       char game;
       char area;
       char pc_sound;
       char dig_sound;
       char music;
       char speed;
       char scroll_flag;
       char boss_dead[3];
       char skill;
       char game_over;
       char future[19];
} SETUP;

typedef struct {
       char width;
       char height;
} PIC_HEADER;

typedef struct{
       char magic;
       char keys;
       int  jewels;
       char last_area;
       char last_screen;
       char last_icon;
       char last_dir;
       int  inventory;
       char item;
       char last_health;
       char last_magic;
       int  last_jewels;
       char last_keys;
       char last_item;
       int  last_inventory;
       char level;
       long score;
       long last_score;
       char object;
       char *object_name;
       char last_object;
       char *last_object_name;
       char armor;
       char future[65];
} THOR_INFO;

typedef struct{
  #ifdef __llvm__
   uint32_t offset;
   uint32_t length;
  #else
   long offset;
   long length;
  #endif
} HEADER;

/* ======================================================================== */
/* Fixed constants (identical across all episodes)                           */
/* ======================================================================== */

#define PAGES 0u
#define PAGE0 3840u
#define PAGE1 19280u
#define PAGE2 34720u
#define PAGE3 50160u

#define X_MAX  319
#define Y_MAX  191
#define MO_BUFF 56688u
#define MO_OFFSET 55968u
#define ENEMY_OFFSET 59664u
#define ENEMY_SHOT_OFFSET 64272u
#define MAX_ACTORS  35
#define MAX_ENEMIES 16
#define MAX_SHOTS   16
#define STAMINA 20

#define THOR 0
#define UP     72
#define DOWN   80
#define LEFT   75
#define RIGHT  77
#define HOME   71
#define PGUP   73
#define END    79
#define PGDN   81
#define ESC     1
#define SPACE  57
#define ENTER  28
#define ALT    56
#define CTRL   29
#define TAB    15
#define LSHIFT 42
#define _Z     44
#define _ONE   2
#define _TWO   3
#define _THREE 4
#define _FOUR  5
#define _S     31
#define _L     38
#define _K     37
#define _D     32
#define _B     48
#define _F1    59
#define AMI_LEN 1800
#define TMP_SIZE 5800

#define sc_Index 0x3C4
enum{
  sc_Reset,
  sc_Clock,
  sc_MapMask,
  sc_CharMap,
  sc_MemMode
};

#define crtc_Index 0x3D4

enum{
  crtc_H_Total,
  crtc_H_DispEnd,
  crtc_H_Blank,
  crtc_H_EndBlank,
  crtc_H_Retrace,
  crtc_H_EndRetrace,
  crtc_V_Total,
  crtc_OverFlow,
  crtc_RowScan,
  crtc_MaxScanLine,
  crtc_CursorStart,
  crtc_CursorEnd,
  crtc_StartHigh,
  crtc_StartLow,
  crtc_CursorHigh,
  crtc_CursorLow,
  crtc_V_Retrace,
  crtc_V_EndRetrace,
  crtc_V_DispEnd,
  crtc_Offset,
  crtc_Underline,
  crtc_V_Blank,
  crtc_V_EndBlank,
  crtc_Mode,
  crtc_LineCompare
};

#define gc_Index 0x3CE
enum{
  gc_SetReset,
  gc_EnableSetReset,
  gc_ColorCompare,
  gc_DataRotate,
  gc_ReadMap,
  gc_Mode,
  gc_Misc,
  gc_ColorDontCare,
  gc_BitMask
};

#define atr_Index 0x3c0
enum{
  atr_Mode = 16,
  atr_Overscan,
  atr_ColorPlaneEnable,
  atr_PelPan,
  atr_ColorSelect
};
#define	status_Reg1 0x3da

enum{
  OW,
  GULP,
  SWISH,
  YAH,
  ELECTRIC,
  THUNDER,
  DOOR,
  FALL,
  ANGEL,
  WOOP,
  DEAD,
  BRAAPP,
  WIND,
  PUNCH1,
  CLANG,
  EXPLODE,
  BOSS11,
  BOSS12,
  BOSS13,
};

/* Runtime area/game macros */
extern char area;
#define GAME1 (area==1)
#define GAME2 (area==2)
#define GAME3 (area==3)

extern volatile char key_flag[100];
#define BP    (key_flag[_B])

#define NUM_SOUNDS  19
#define NUM_OBJECTS 32

/* ======================================================================== */
/* Runtime magic constants - resolved via the episode table                  */
/* ======================================================================== */

#include "episode.h"

#define APPLE_MAGIC     (ep->magic_apple)
#define HOURGLASS_MAGIC (ep->magic_hourglass)
#define LIGHTNING_MAGIC (ep->magic_lightning)
#define BOOTS_MAGIC     (ep->magic_boots)
#define BOMB_MAGIC      (ep->magic_bomb)
#define WIND_MAGIC      (ep->magic_wind)
#define QUESTION_MAGIC  (ep->magic_question)
#define SHIELD_MAGIC    (ep->magic_shield)
#define THUNDER_MAGIC   (ep->magic_thunder)

/* Episode-specific constants resolved at runtime */
#define START_LEVEL     (ep->start_level)
#define DEMO_LEN        (ep->demo_len)

/* Boss level constants (used by boss_ep*.c) */
#define BOSS_LEVEL1  59
#define BOSS_LEVEL21 200
#define BOSS_LEVEL22 118
#define BOSS_LEVEL   (ep->boss_levels[0])

#endif /* GOT_GAME_DEFINE_H */
