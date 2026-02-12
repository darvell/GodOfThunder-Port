#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emscripten_fs.h"
#include "episode.h"
#include "launcher.h"

/* Unified entrypoint. In the native build, src/game/main.c renames its
   DOS `main()` to this symbol via a preprocessor define. */
void got_game_main(int argc, char** argv);

/* Set by the game when the user chooses "Quit to DOS" from the ESC menu.
   When set, we exit the process instead of returning to the launcher. */
extern int got_wants_quit;

/* Parse --episode N / -e N / bare "1"/"2"/"3" from argv.
 * Returns 0 if no episode specified (show launcher). */
static int parse_episode(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) {
    if ((strcmp(argv[i], "--episode") == 0 || strcmp(argv[i], "-e") == 0)
        && i + 1 < argc) {
      int ep = atoi(argv[i + 1]);
      if (ep >= 1 && ep <= 3) return ep;
    }
    /* Bare "1", "2", or "3" as first non-flag arg */
    if (argv[i][0] >= '1' && argv[i][0] <= '3' && argv[i][1] == '\0')
      return argv[i][0] - '0';
  }
  return 0; /* no episode specified → show launcher */
}

int main(int argc, char** argv) {
  int episode = parse_episode(argc, argv);
  int from_cli = (episode != 0);

  /* Main loop: launcher → game → launcher → ...
   * Game completion (beating the episode) returns to the launcher for
   * episode selection.  Explicit "Quit to DOS" exits the process.
   *
   * The game's exit_code(0) fully cleans up (window, audio, memory).
   * The launcher re-creates everything from scratch each cycle. */
  for (;;) {
    if (episode == 0) {
      launcher_init();
      episode = launcher_run();
      launcher_shutdown();
      if (episode == 0) return 0;  /* user quit from launcher */
    }

    got_episode_select(episode);
    got_emscripten_persist_init(episode);
    got_game_main(argc, argv);

    /* "Quit to DOS" or CLI mode → exit the process. */
    if (from_cli || got_wants_quit) return 0;

    /* Game ended naturally (completed, died, etc.) → return to launcher. */
    episode = 0;
  }
}
