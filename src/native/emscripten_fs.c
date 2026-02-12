#include "emscripten_fs.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <stdio.h>
#include <unistd.h>

static volatile int g_fs_ready = 0;
static volatile int g_fs_ok = 0;

/* Called from JS (Module._got_emscripten_fs_ready). */
EMSCRIPTEN_KEEPALIVE void got_emscripten_fs_ready(int ok) {
  g_fs_ok = ok ? 1 : 0;
  g_fs_ready = 1;
}

static void start_fs_init_async(int episode) {
  EM_ASM(
    {
      const episode = $0 | 0;
      const root = "/persist";
      const dir = root + "/got_ep" + episode;
      const saveName = "GOTSAVE" + episode + ".SAV";
      const savePath = dir + "/" + saveName;
      const lsKey = "got:ep" + episode + ":" + saveName;

      function safeMkdir(path) {
        try { FS.mkdir(path); } catch (e) {}
      }

      function exists(path) {
        try { FS.stat(path); return true; } catch (e) { return false; }
      }

      function copyIfMissing(dst, src) {
        if (exists(dst)) return;
        try {
          const data = FS.readFile(src, { encoding: "binary" });
          FS.writeFile(dst, data, { encoding: "binary" });
        } catch (e) {}
      }

      function b64ToBytes(b64) {
        const bin = atob(b64);
        const out = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i) & 0xFF;
        return out;
      }

      function bytesToB64(bytes) {
        // Chunk to avoid stack/argument limits.
        let s = "";
        const CHUNK = 0x8000;
        for (let i = 0; i < bytes.length; i += CHUNK) {
          const sub = bytes.subarray(i, i + CHUNK);
          s += String.fromCharCode.apply(null, sub);
        }
        return btoa(s);
      }

      function tryRestoreSave() {
        try {
          const b64 = localStorage.getItem(lsKey);
          if (!b64) return;
          FS.writeFile(savePath, b64ToBytes(b64), { encoding: "binary" });
        } catch (e) {
          // Storage can be unavailable (private mode, blocked). Continue without saves.
        }
      }

      function setupAutoSave() {
        if (Module.__gotAutoSaveInterval) return;
        Module.__gotAutoSaveInterval = setInterval(function() {
          try {
            if (!exists(savePath)) return;
            const bytes = FS.readFile(savePath, { encoding: "binary" });
            localStorage.setItem(lsKey, bytesToB64(bytes));
          } catch (e) {}
        }, 1000);
      }

      safeMkdir(root);
      safeMkdir(dir);

      /* Keep resources in the per-episode working directory so the original
         DOS code (which opens files in the CWD) continues to work. */
      copyIfMissing(dir + "/GOTRES.DAT", "/GOTRES.DAT");
      copyIfMissing(dir + "/VERSION.GOT", "/VERSION.GOT");

      tryRestoreSave();
      setupAutoSave();

      Module._got_emscripten_fs_ready(1);
    },
    episode);
}

void got_emscripten_persist_init(int episode) {
  char dir[64];

  g_fs_ready = 0;
  g_fs_ok = 0;

  if (episode < 1) episode = 1;
  if (episode > 3) episode = 3;

  start_fs_init_async(episode);

  /* localStorage-based setup is synchronous; this should flip immediately. */
  if (!g_fs_ready) {
    /* In case the runtime changes, avoid deadlocking. */
    fprintf(stderr, "Web persistence init did not signal readiness (episode %d)\n", episode);
    return;
  }

  if (!g_fs_ok) {
    fprintf(stderr, "Web persistence init failed (episode %d)\n", episode);
    return;
  }

  snprintf(dir, sizeof(dir), "/persist/got_ep%d", episode);
  (void)chdir(dir);
}

#else

void got_emscripten_persist_init(int episode) { (void)episode; }

#endif
