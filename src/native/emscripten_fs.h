#ifndef GOT_EMSCRIPTEN_FS_H
#define GOT_EMSCRIPTEN_FS_H

/* Web build support (Emscripten): persistent filesystem for saves.
   On non-web builds these are no-ops. */

#ifdef __cplusplus
extern "C" {
#endif

/* Prepare per-episode persistent working directory and background sync.
   Intended to be called at process startup, before game code runs. */
void got_emscripten_persist_init(int episode);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GOT_EMSCRIPTEN_FS_H */

