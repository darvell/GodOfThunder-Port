/* episode.c - Episode data tables for God of Thunder unified build */

#include <stdlib.h>
#include <string.h>
#include "episode.h"

/* Boss function prototypes - defined in boss_ep*.c */

/* Episode 1 bosses */
extern void boss_level1(void);
extern int  boss_dead1(void);
extern void closing_sequence1(void);
extern void boss_level21(void);
extern int  boss_dead21(void);
extern void closing_sequence21(void);
extern void boss_level22(void);
extern int  boss_dead22(void);
extern void closing_sequence22(void);

/* Episode 2 boss */
extern void boss_level_ep2(void);
extern int  boss_die_ep2(void);
extern void closing_sequence_ep2(void);

/* Episode 3 boss */
extern void boss_level_ep3(void);
extern int  boss_die_ep3(void);
extern void closing_sequence_ep3(void);
extern void ending_screen_ep3(void);
extern int  endgame_movement_ep3(void);

/* Object name tables per episode */
static char *ep1_object_names[] = {
    "Shrub", "Child's Doll", "UNUSED", "FUTURE",
    "FUTURE", "FUTURE", "FUTURE", "FUTURE", "FUTURE",
    "FUTURE", "FUTURE", "FUTURE", "FUTURE", "FUTURE",
    "FUTURE"
};

static char *ep2_object_names[] = {
    "Shrubbery", "Child's Doll", "UNUSED",
    "Skeleton Key", "Electric Saw", "Woman's Bracelet",
    "Mystic Mushroom", "FUTURE", "Hypno-Stone", "Shovel",
    "FUTURE", "FUTURE", "FUTURE", "FUTURE"
};

static char *ep3_object_names[] = {
    "Shrubbery", "Child's Doll", "UNUSED",
    "Skeleton Key", "Electric Saw", "Woman's Bracelet",
    "Mystic Mushroom", "FUTURE", "FUTURE", "FUTURE",
    "FUTURE", "FUTURE", "FUTURE", "FUTURE"
};

/* ---- Episode 1: Serpent Surprise ---- */
static const episode_t episode1 = {
    .number         = 1,
    .title          = "Part I: Serpent Surprise",
    .story_res      = "STORY1",
    .save_file      = "GOTSAVE1.SAV",
    .start_level    = 23,
    .demo_len       = 3600,
    .music_track    = 7,
    .song_buf_size  = 20000L,

    .start_health   = 150,
    .start_magic    = 0,
    .start_armor    = 0,
    .start_inventory = 0,   /* area-based: adds apple+lightning if area>1 etc. */
    .start_x        = 152,
    .start_y        = 96,

    /* Episode 1 magic layout */
    .magic_apple     = 1,
    .magic_hourglass = 2,
    .magic_lightning = 4,
    .magic_boots     = 8,
    .magic_bomb      = 16,
    .magic_wind      = 32,
    .magic_question  = 64,
    .magic_shield    = 128,
    .magic_thunder   = 256,

    /* 3 boss levels */
    .boss_levels     = { 59, 200, 118 },
    .num_boss_levels = 3,
    .boss_level_fn   = { boss_level1, boss_level21, boss_level22 },
    .boss_dead_fn    = { boss_dead1, boss_dead21, boss_dead22 },
    .closing_fn      = { closing_sequence1, closing_sequence21, closing_sequence22 },

    .ending_screen_fn    = NULL,
    .endgame_movement_fn = NULL,

    .death_obj_index = 10,
    .object_names    = ep1_object_names,
    .num_objects     = 15,
};

/* ---- Episode 2: Non-stick Nognir ---- */
static const episode_t episode2 = {
    .number         = 2,
    .title          = "Part II: Non-Stick Nognir",
    .story_res      = "STORY2",
    .save_file      = "GOTSAVE2.SAV",
    .start_level    = 51,
    .demo_len       = 4800,
    .music_track    = 8,
    .song_buf_size  = 26000L,

    .start_health   = 150,
    .start_magic    = 150,
    .start_armor    = 1,
    .start_inventory = 3,   /* APPLE(1) + LIGHTNING(2) */
    .start_x        = 32,
    .start_y        = 32,

    /* Episode 2 magic layout (no hourglass, bomb, question) */
    .magic_apple     = 1,
    .magic_hourglass = 0,
    .magic_lightning = 2,
    .magic_boots     = 4,
    .magic_bomb      = 0,
    .magic_wind      = 8,
    .magic_question  = 0,
    .magic_shield    = 16,
    .magic_thunder   = 32,

    /* 1 boss level */
    .boss_levels     = { 60, -1, -1 },
    .num_boss_levels = 1,
    .boss_level_fn   = { boss_level_ep2, NULL, NULL },
    .boss_dead_fn    = { boss_die_ep2, NULL, NULL },
    .closing_fn      = { closing_sequence_ep2, NULL, NULL },

    .ending_screen_fn    = NULL,
    .endgame_movement_fn = NULL,

    .death_obj_index = 18,
    .object_names    = ep2_object_names,
    .num_objects     = 14,
};

/* ---- Episode 3: Lookin' for Loki ---- */
static const episode_t episode3 = {
    .number         = 3,
    .title          = "Part III: Lookin' for Loki",
    .story_res      = "STORY3",
    .save_file      = "GOTSAVE3.SAV",
    .start_level    = 33,
    .demo_len       = 4800,
    .music_track    = 8,
    .song_buf_size  = 20000L,

    .start_health   = 150,
    .start_magic    = 150,
    .start_armor    = 1,
    .start_inventory = 15,  /* APPLE(1) + LIGHTNING(2) + BOOTS(4) + WIND(8) */
    .start_x        = 272,
    .start_y        = 80,

    /* Episode 3 magic layout (same as ep2) */
    .magic_apple     = 1,
    .magic_hourglass = 0,
    .magic_lightning = 2,
    .magic_boots     = 4,
    .magic_bomb      = 0,
    .magic_wind      = 8,
    .magic_question  = 0,
    .magic_shield    = 16,
    .magic_thunder   = 32,

    /* 1 boss level */
    .boss_levels     = { 95, -1, -1 },
    .num_boss_levels = 1,
    .boss_level_fn   = { boss_level_ep3, NULL, NULL },
    .boss_dead_fn    = { boss_die_ep3, NULL, NULL },
    .closing_fn      = { closing_sequence_ep3, NULL, NULL },

    .ending_screen_fn    = ending_screen_ep3,
    .endgame_movement_fn = endgame_movement_ep3,

    .death_obj_index = 21,
    .object_names    = ep3_object_names,
    .num_objects     = 14,
};

static const episode_t *episodes[3] = { &episode1, &episode2, &episode3 };

const episode_t *ep = NULL;
int g_episode = 0;

void got_episode_select(int episode_num) {
    if (episode_num < 1 || episode_num > 3) return;
    g_episode = episode_num;
    ep = episodes[episode_num - 1];
}

const episode_t *got_episode_get(int episode_num) {
    if (episode_num < 1 || episode_num > 3) return NULL;
    return episodes[episode_num - 1];
}
