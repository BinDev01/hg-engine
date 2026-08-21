# AGENTS.md

ROM-hack engine for Pokémon HeartGold (US). Builds a patched `test.nds` from a
vanilla `rom.nds`. No test suite, no lint, no typecheck — verification is "build
succeeds and the ROM runs."

## Hard requirements (the build enforces these, don't fight it)

- A **US** HeartGold ROM (gamecode `IPKE`) named `rom.nds` at the repo root.
  Other regions / renamed files fail at the Makefile gamecode check.
- Repo must be a git checkout (submodules: `git clone --recursive`). The
  Makefile errors out if not inside a git work tree (Docker `/hg-engine` is the
  only exception). OneDrive paths are rejected.
- `arm-none-eabi-*` toolchain on PATH (or `DEVKITARM` set). On MSYS2 the
  Makefile forces `/mingw64/bin/arm-none-eabi-`.

## Build

- Der Agent nimmt ausschließlich Code- und Spieldatenänderungen vor. Builds,
  Tool-Rebuilds, Clean-Schritte und ROM-Erzeugung führt der Benutzer manuell
  aus.

- `make` (or `make -j$(nproc)`). First run builds all tools from source
  (`tools/source/*` — armips, ndstool, nitrogfx, msgenc, o2narc, adpcm-xq,
  ENCODE_IMG, ntrWavTool) then the ROM. Slow the first time.
- A Python venv `.venv` is auto-created from `requirements.txt` (just
  `ndspy==4.1.0`). If `ensurepip` is missing the Makefile falls back to system
  python — don't assume the venv exists.
- Output is `test.nds` at repo root.
- **macOS arm64 (Apple Silicon):** `libpng`/arch mismatch breaks `nitrogfx` and
  `ENCODE_IMG`. Workaround from README: run Terminal under Rosetta, then
  `make tools/nitrogfx && make tools/ENCODE_IMG`, then switch back. Don't try
  to "fix" this by changing the toolchain.
- Clean targets:
  - `make clean` — build artifacts + `base/` + generated `rom_gen*.ld`
  - `make clean_code` — only `*.o`/`*.d`/linked output (use after editing C/ASM)
  - `make clean_tools` — wipes built tools **and** `.venv` (forces full rebuild)
- `make restore` swaps `rom.nds` from a `romClean.nds` backup;
  `make restore_build` = restore + build.

## Docker alternative

`docker build . -t hg-engine` then `./docker-makerom.cmd` (cross-platform,
works from Windows `cmd` too). Still need `make clean` / `make clean_code`
manually when things go stale.

## Where code lives (boundaries matter)

- `src/*.c` — C code compiled into the ARM9 extension (overlay 129). Linked via
  `src/linker.ld` against `rom.ld`. Single flat directory, all `*.c` are
  compiled (`wildcard`), so don't drop scratch files here.
- `asm/*.s` — hand assembly, same overlay. Also wildcard-compiled.
- `armips/` — assembled by `armips armips/global.s` (the entry point).
  - `armips/data/*.s` — **large generated-ish data tables** (mondata, evodata,
    levelupdata, encounters, ... — some files are hundreds of KB to ~1MB).
    This is where trainers, dex entries, mon stats, learnsets, encounters are
    edited. Editable game data lives here, not in C.
  - `armips/include/config.s`, `constants.s`, `macros.s`,
    `scriptmacros.s`, `flags.s`, `vars.s` — shared asm headers.
- `include/*.h` — C headers, paired with `src/`.
- `tools/` — built binaries; `tools/source/` — their sources (submodules +
  cloned-on-demand). `nitrogfx` is the one git submodule.
- `scripts/` — Python build helpers invoked by the Makefile (`make.py` is the
  main one; also `generate_ld.py`, `tm_learnset.py`, `tutor_learnset.py`,
  `reformat_sprite_data.py`, `validate_trainers_s.py`, ...). Not user-facing
  entry points.
- `data/` — asset/data sources (graphics, itemdata, codetables) pulled in via
  `*.mk` includes. `narcs.mk` and `overlays.mk` are large generated makefiles
  — treat as build plumbing, don't hand-edit casually.

## Build pipeline (order matters — defined in Makefile `all:`)

1. Build tools (`$(TOOLS)`).
2. Compile+link `src/*.c` + `asm/*.s` → `build/linked.o` → `build/output.bin`
   (overlay 129 payload) and overlay outputs.
3. `ndstool -x` extracts `rom.nds` into `base/`.
4. `narcpy` extracts NARC `a/0/2/8` → `build/a028/`.
5. `python scripts/make.py` — patches tables into `build/a028/`.
6. `make move_narc` — copies ~40 NARC categories (mon data, moves, trainers,
   encounters, graphics, SDAT, ...) into `base/` and runs a few armips data
   steps + `tm_learnset.py` / `tutor_learnset.py`.
7. `armips armips/global.s` — applies hooks/repoints into the ARM9.
8. Repackage NARC, then `ndstool -c test.nds`.

If a build fails mid-pipeline after editing data, `make clean_code` is usually
enough; full `make clean` also wipes the extracted `base/`.

## Configuration (keep these in sync)

Feature toggles live in **two paired files** that must stay consistent:
`include/config.h` (C) and `armips/include/config.s` (assembly). Most toggles
are documented in `CONFIG.md`. Common gotchas:

- `START_ADDRESS` must match between the two files (it's the offset in overlay
  129 for armips tables; default `0x0`, ~0x1000 bytes reserved).
- `ALLOW_SAVE_CHANGES` enables save-field expansion (needed for full dex) but
  **breaks PKHeX compatibility**. Comment out in both files to keep PKHeX.
- `FAIRY_TYPE_IMPLEMENTED`, `HIDDEN_ABILITIES`, `MEGA_EVOLUTIONS`,
  `PRIMAL_REVERSION`, `EXPERIENCE_FORMULA_GEN`, etc. — toggle in both files.

There is a **second, separate** offset for C code, not the same as
`START_ADDRESS`:
- `scripts/make.py` line ~13 `OFFSET_TO_START` — your free-space offset in
  overlay 129 for the C payload.
- `linker.ld` (repo root, the `rom.ld`-style file) and `src/linker.ld` — the
  numbers at the top must match `OFFSET_TO_START`. **These must not overlap
  with `START_ADDRESS`** (the armips tables region). CONFIG.md calls this out
  explicitly — easy to miss.

## Conventions

- C flags are fixed in the Makefile (`-mthumb -mcpu=arm7tdmi -march=armv4t
  -Os -fira-loop-pressure -fipa-pta ...`). Don't introduce toolchain options
  that aren't already there; this targets the NDS ARM7tdmi specifically.
- No tests, no linter, no formatter configured. "Did it build?" is the check.
- When editing game data (trainers, dex, mon stats, learnsets, encounters),
  edit the `.s`/`.txt` files under `armips/data/` — not C, not JSON.
- `tools/source/` is regenerated from upstream repos on demand; local edits
  there will be lost on `make clean_tools`. Only `tools/source/nitrogfx` is a
  real submodule.
