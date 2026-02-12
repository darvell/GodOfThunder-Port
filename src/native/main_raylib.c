#include <stdio.h>

#include "emscripten_fs.h"

/* Episode 1 entrypoint. In the native build, src/_g1/1_main.c renames its
   DOS `main()` to this symbol via a preprocessor define. */
void got_g1_game_main(int argc, char** argv);

int main(int argc, char** argv) {
  got_emscripten_persist_init(1);
  got_g1_game_main(argc, argv);
  return 0;
}
