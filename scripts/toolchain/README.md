# DOS Toolchain Notes

This repo’s original DOS build expects Borland tooling inside DOSBox:

- `C:\TC` (Turbo C / Turbo C++)
- `C:\TASM` (Turbo Assembler)

Turbo Assembler is not freely redistributable. For the assembler step, this
repo is now configured to use **JWasm** instead, which is MASM-compatible and
can assemble the shipped `.asm` files into OMF `.obj` that still link with
`tlink.exe`.

## JWasm (Assembler Replacement)

Run:

```sh
scripts/toolchain/fetch_jwasm.sh
```

This downloads JWasm into `./tasm/` so that DOSBox sees it as `C:\TASM\...`.

The DOS Makefiles are configured to use `jwasm.exe` as the assembler.

## OpenWatcom (Recommended)

This repo can be built using OpenWatcom (free/open-source) inside DOSBox.

Fetch and unpack an OpenWatcom DOS toolchain into `./ow/`:

```sh
uv run python scripts/toolchain/fetch_openwatcom.py --dest ow
```

The build uses:

- `wcc.exe` for C compilation (16-bit)
- `wasm.exe` for assembling the MASM-style `.asm` files
- `wlink.exe` for linking DOS executables

The build also uses a small set of Borland-compat headers in `src/owcompat/`
to satisfy includes like `<alloc.h>`.

## OpenWatcom v2 (Native Host Build, Fast Iteration)

If you are on macOS/Linux and want much faster compile iterations, you can
build a native OpenWatcom v2 toolchain (host tools run on your OS, but still
target 16-bit DOS).

One-time toolchain build:

```sh
scripts/toolchain/build_openwatcom_v2.sh
```

Faster alternative (bootstrap only, sufficient for building this repo):

```sh
scripts/toolchain/build_openwatcom_v2_bootstrap.sh
```

Then build the game (no DOSBox involved):

```sh
./build.sh host
```

Clean host build artifacts:

```sh
./build.sh host clean
```

Notes:

- The OpenWatcom v2 build is slow the first time; subsequent game builds are fast.
- If you already have a `rel/` tree elsewhere, you can point `OW2_REL` at it:
  `OW2_REL=/path/to/rel ./build.sh host`

## Turbo C (Legacy)

You still need a Turbo C toolchain (or a compatible drop-in providing
`tcc.exe`, `tlink.exe`, headers, and libraries) in `./tc/` so DOSBox sees it
as `C:\TC\...`.

This project does not download or bundle Turbo C automatically because
redistribution and download sources vary. Once you have it, arrange the
directory like:

- `tc/bin/tcc.exe`
- `tc/bin/tlink.exe`
- `tc/include/...`
- `tc/lib/...`

Then `build.sh` (DOS build via DOSBox-X) will be able to find it.

Note: `build.sh` does not rely on `make.exe` anymore; it drives the toolchain
from a generated DOS batch file.
