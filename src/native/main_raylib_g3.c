#include <stdio.h>

#include "emscripten_fs.h"

/* Episode 3 entrypoint. In the native build, src/_g3/3_main.c renames its
   DOS `main()` to this symbol via a preprocessor define. */
void got_g3_game_main(int argc, char** argv);

int main(int argc, char** argv) {
  got_emscripten_persist_init(3);
  got_g3_game_main(argc, argv);
  return 0;
}
