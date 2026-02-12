// Emscripten post-JS hook.
//
// Some JS glue (notably miniaudio as used by raylib) expects memory views to
// exist on `Module` (e.g. `Module.HEAPF32`). Recent Emscripten outputs expose
// them as closure-scoped globals (`HEAPF32`, etc.) but do not always mirror them
// back onto `Module`. Mirror them here and keep them in sync after memory
// growth (updateMemoryViews()).
//
// This file is appended to the generated `<target>.js` via `--post-js`, so it
// shares the same scope as `updateMemoryViews` and the `HEAP*` variables.

(function() {
  function mirrorHeapsToModule() {
    if (typeof Module === "undefined" || !Module) return;
    // Typed array views are updated when wasm memory grows; keep Module in sync.
    Module.HEAP8 = HEAP8;
    Module.HEAPU8 = HEAPU8;
    Module.HEAP16 = HEAP16;
    Module.HEAPU16 = HEAPU16;
    Module.HEAP32 = HEAP32;
    Module.HEAPU32 = HEAPU32;
    Module.HEAPF32 = HEAPF32;
    Module.HEAPF64 = HEAPF64;
    // These exist when BIGINT is enabled; harmless otherwise.
    if (typeof HEAP64 !== "undefined") Module.HEAP64 = HEAP64;
    if (typeof HEAPU64 !== "undefined") Module.HEAPU64 = HEAPU64;
  }

  if (typeof updateMemoryViews === "function") {
    var __got_updateMemoryViews = updateMemoryViews;
    updateMemoryViews = function() {
      __got_updateMemoryViews();
      mirrorHeapsToModule();
    };
  }

  // Mirror once for the initial memory view setup.
  mirrorHeapsToModule();
})();

