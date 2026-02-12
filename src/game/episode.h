/* episode.h - Runtime episode parameterization for God of Thunder
 *
 * Instead of compiling 3 separate binaries with different #defines,
 * the unified build selects episode data at runtime via this table.
 */
#ifndef GOT_EPISODE_H
#define GOT_EPISODE_H

/* Forward declarations for boss function pointer types */
struct actor_struct; /* ACTOR - defined in game_define.h */

typedef struct {
    int number;             /* 1, 2, or 3 */
    const char *title;      /* e.g. "Part I: Serpent Surprise" */
    const char *story_res;  /* resource name: "STORY1", "STORY2", "STORY3" */
    const char *save_file;  /* "GOTSAVE1.SAV" etc. */
    int start_level;        /* 23, 51, 33 */
    int demo_len;           /* 3600 or 4800 */
    int music_track;        /* 7 or 8 */
    long song_buf_size;     /* 20000 or 26000 */

    /* Player start state */
    int start_health;       /* 150 */
    int start_magic;        /* 0 or 150 */
    int start_armor;        /* 0, 1, or 10 */
    int start_inventory;    /* episode-specific bitfield */
    int start_x, start_y;  /* player start position */

    /* Magic bit layout for this episode (0 = item not in this episode) */
    int magic_apple;
    int magic_hourglass;    /* ep1 only */
    int magic_lightning;
    int magic_boots;
    int magic_bomb;         /* ep1 only */
    int magic_wind;
    int magic_question;     /* ep1 only */
    int magic_shield;
    int magic_thunder;

    /* Boss levels (-1 = unused) */
    int boss_levels[3];
    int num_boss_levels;

    /* Boss function pointers - set per episode in episode.c */
    void (*boss_level_fn[3])(void);
    int  (*boss_dead_fn[3])(void);
    void (*closing_fn[3])(void);

    /* Ep2/3 extras (NULL for ep1) */
    void (*ending_screen_fn)(void);
    int  (*endgame_movement_fn)(void);

    /* Death tombstone object index */
    int death_obj_index;    /* 10 for ep1, 18 for ep2, 21 for ep3 */

    /* Object names table pointer (set at init) */
    char **object_names;
    int num_objects;
} episode_t;

extern const episode_t *ep;   /* current episode pointer */
extern int g_episode;          /* 1, 2, or 3 */

/* Select an episode at runtime. Must be called before game_main(). */
void got_episode_select(int episode_num);

/* Access the episode table directly by index (1-based). */
const episode_t *got_episode_get(int episode_num);

#endif /* GOT_EPISODE_H */
