# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

hg-engine is a ROM-hack engine for Pokémon HeartGold (US). It patches a vanilla `rom.nds` into
`test.nds` by compiling new C/ASM into ARM9 extension overlays and hooking them into the original
game code. There is no test suite, no linter, no formatter — verification is "the build succeeds and
the ROM runs."

## Build policy

**Do not run builds.** Make code and game-data changes only; the user runs `make`, clean steps, and
ROM generation manually. (This is a standing instruction from `AGENTS.md`.)

For reference, the commands the user runs:

- `make -j$(nproc)` — full build → `test.nds`. First run compiles all tools from `tools/source/*`
  and is slow.
- `make clean_code` — only `*.o`/`*.d`/linked output. This is the right clean after editing C/ASM.
- `make clean` — build artifacts + extracted `base/` + generated `rom_gen*.ld`.
- `make clean_tools` — wipes built tools **and** `.venv`.
- `make restore` / `make restore_build` — swap `rom.nds` back from a `romClean.nds` backup.
- Docker: `docker build . -t hg-engine` then `./docker-makerom.cmd`.

Hard build requirements: a **US** HeartGold ROM (gamecode `IPKE`) named `rom.nds` at the repo root; a
git checkout with submodules (`git clone --recursive`); `arm-none-eabi-*` on PATH. macOS arm64 has a
known `libpng` arch mismatch that breaks `nitrogfx`/`ENCODE_IMG` — the README workaround is to build
those two targets under Rosetta. Don't "fix" it by changing the toolchain.

## Architecture: how new code reaches the ROM

The engine does **not** use the synthetic overlay system. New code lives in extension overlays that
are dynamically linked next to the vanilla overlays they extend:

| Source location | Overlay | Notes |
|---|---|---|
| `src/*.c` + `asm/*.s` | 129 (`OVERLAY_ARM9_EXTENSION`) | Always resident. Linked via `src/linker.ld`. |
| `src/battle/*.c` + `asm/battle/*.s` | 130 | Loaded with vanilla battle overlay 12. |
| `src/field/*.c` + `asm/field/*.s` | 131 | Loaded with field overlay 1, hall of fame, Pokéathlon, Pokéwalker. |
| `src/pokedex/*.c` | 132 | Loaded with Pokédex overlay 18. |
| `src/individual/<Name>.c` | one overlay each, 133+ | Overlay ID comes from the `/* Overlay ### */` comment on line 1 of `src/individual/linker/<Name>.ld`. |

`overlays.mk` derives the overlay list from directory names under `src/` (anything that isn't a file
and isn't `individual`), so adding a new subdirectory + `linker.ld` creates a new overlay. All `*.c`
in these directories are wildcard-compiled — don't leave scratch files there.

`src/individual/` is for single large functions that would blow the size budget of a shared overlay
(e.g. `BattleController_BeforeMove.c` at 200 KB). `src/overlay.c` holds the link/cleanup/priority
tables (`gLinkedOverlayList`, `gCleanupOverlayList`, `gOverlayPriorityList`) that decide which
extension loads with which vanilla overlay and what gets evicted. Overlay IDs are named in
`include/constants/file.h`.

**Any call that crosses an overlay boundary — including every call into vanilla ROM code — must be
declared `LONG_CALL`** (`__attribute__((long_call))`, `include/types.h`). Omitting it produces a
short branch that silently goes out of range at runtime. Look at how existing prototypes in
`include/*.h` are declared. A missing `LONG_CALL` is a classic crash source here (see git history).

### Injection tables (repo root)

`scripts/make.py` patches the ROM using five plain-text tables at the repo root. They support
`#include "..."` of C headers plus `#ifdef`/`#ifndef`/`#else` on the defines pulled in that way.

- `hooks` — `<overlay|arm9> <C symbol> <address> [register]`. Writes a thumb trampoline at `address`
  jumping to the symbol. `overlay` is the numeric vanilla overlay (`0012` = battle) or `arm9`.
  Omitting the register means full-function replacement (needs 0x1C of space; see
  `documentation/testing_hook.s`).
- `armhooks` — same, but for ARM-mode code.
- `repoints` — `<overlay|arm9> <symbol> <address>`: overwrite a pointer word.
- `routinepointers` — like repoints, for function-pointer table entries.
- `bytereplacement` — raw byte patches at absolute offsets.

`armips/global.s` is the other injection path: it's the armips entry point that `.include`s all of
`armips/asm/*.s` (table repoints, fairy type, level-up format change, Pokédex expansion, ...) and
applies them to the ARM9.

### Build pipeline order (Makefile `all:`)

1. Build tools → 2. compile/link `src` + `asm` into per-overlay `build/output_*.bin` → 3. `ndstool -x`
extracts `rom.nds` into `base/` → 4. extract NARC `a/0/2/8` → `build/a028/` → 5. `scripts/make.py`
applies the injection tables → 6. `make move_narc` copies ~40 NARC categories into `base/` and runs
`tm_learnset.py`/`tutor_learnset.py` → 7. `armips armips/global.s` → 8. repack NARC, `ndstool -c
test.nds`.

## Where to edit what

- **Game data** (trainers, mon stats, learnsets, evolutions, encounters, dex entries) → `.s`/`.txt`
  files under `armips/data/`. Not C, not JSON. These files are large (`mondata.s` ~940 KB,
  `levelupdata.s` ~825 KB, `tmlearnset.txt` ~878 KB) — always grep to the relevant entry rather than
  reading whole files.
- **Battle logic** → `src/battle/` (shared) or `src/individual/` (the big per-function overlays).
  `src/battle/battle_script_commands.c` and `other_battle_calculators.c` are the main hubs.
- **Constants and symbol names** → `asm/include/*.inc` (species, moves, items, abilities,
  battle commands, hold item effects) for assembly; `include/constants/` for C.
- **Build plumbing** → `narcs.mk`, `overlays.mk`, `data/*.mk`. Generated-ish; don't hand-edit casually.
- `tools/source/` is re-cloned on demand and wiped by `make clean_tools`; only
  `tools/source/nitrogfx` is a real submodule. Local edits there are lost.

## Configuration — three offsets and two paired files

Feature toggles live in **two files that must stay in sync**: `include/config.h` (C) and
`armips/include/config.s` (assembly). Some toggles exist in only one of the two — `CONFIG.md`
documents which. Notable ones:

- `START_ADDRESS` — offset in overlay 129 for the armips-inserted tables. Must match between both
  config files (default `0x0`, ~0x1000 bytes reserved).
- `OFFSET_TO_START` in `scripts/make.py` (~line 13) — a **separate** offset, for the C payload.
  The numbers at the top of `linker.ld` (repo root) must match it, and it **must not overlap
  `START_ADDRESS`**. This is easy to miss and produces confusing corruption.
- `ALLOW_SAVE_CHANGES` — enables save expansion (new-mon dex registration, bigger pockets, Kyurem
  forme change) but **breaks PKHeX compatibility**. Comment out in both files to keep PKHeX.
- `FAIRY_TYPE_IMPLEMENTED`, `HIDDEN_ABILITIES`, `MEGA_EVOLUTIONS`, `PRIMAL_REVERSION`,
  `EXPERIENCE_FORMULA_GEN`, `LEARNSET_TOTAL_MOVES`, `IMPLEMENT_LEVEL_CAP`, `EXPAND_PC_BOXES`, ...

`include/debug.h` holds cheat/trace toggles (`GUARANTEE_CAPTURES`, `DEBUG_ENABLE_ALL_GIMMICKS`,
`DEBUG_BATTLE_SCRIPT_COMMANDS`, `DEBUG_PRINT_OVERLAY_LOADS`, ...) that print to the DeSmuME console.
Treat enabled entries there as the user's deliberate local testing state — don't flip them back
as cleanup.

## Conventions

- C flags are fixed in the Makefile (`-mthumb -mcpu=arm7tdmi -march=armv4t -Os -fira-loop-pressure
  -fipa-pta ...`) and target the NDS ARM7TDMI specifically. Don't introduce toolchain options that
  aren't already present.
- Overlay space is finite and per-overlay. Adding a large function to `src/battle/` can silently
  overflow overlay 130's `LENGTH` in its `linker.ld`; that's what `src/individual/` exists for.
- Licensing: this is a community project that must stay free — no paywalls, donations included, and
  `CREDITS.md` must be reproduced downstream. Keep that in mind for anything license-adjacent.
