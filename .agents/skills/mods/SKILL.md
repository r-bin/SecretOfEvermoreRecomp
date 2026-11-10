---
name: mods
description: Use when making, changing or reviewing a mod: a .snesmod package with its trusted plugin, its MesenCE Lua twin, the parity check between them, packing, the docs/mods page.
applyTo: "**"
---

# Skill: mods and their MesenCE twins

A mod here is two things that must do the same: the recomp's package (manifest + trusted C plugin) and, where MesenCE can
express it, a Lua **twin** for MesenCE 2.2.1. `docs/mods/README.md` is the map (where each part lives, the parity
commands, the page template) and the table of every mod; read it and the mod's page first. Framework rules:
`snesrecomp/docs/MOD_PACKAGES.md` (manifest, plugin registration), `MOD_AUTHORING.md` (archives).

## Steps

1. **Understand the mod.** From its page, manifest and plugin, write down in a sentence each: what the player gets, which
   WRAM or picture it changes, the hook (instruction address or host callback), its gates, its environment switch.
   For a port of an existing ROM hack or practice feature, take the facts from our own sources (everscript, the
   everscript-vscode knowledge entries) and name them; the result writes what the hack writes, nothing more.
   Done when each of the five is named with an address or a file.
2. **Package.** `mods/preloaded/packages/<id>/<version>/manifest.toml`: features off by default, options for every
   choice the player would want, `presentation_only = true` only when no guest memory changes. Plugin
   `src/soe_<name>.c`, in `CMakeLists.txt`, `SNES_MOD_CONSTRUCTOR` registering a reset callback and the plugin ID
   (presentation plugins with `snes_mod_register_presentation_plugin`). With the feature off the plugin writes nothing.
   Give it a switch `SOE_<NAME>`: `0` off, `1` on with the options' defaults without the launcher; add `=0` to
   `tests/replay_recorded.sh` when the mod changes the game.
3. **Twin.** `mods/mesen/<id>.lua`: the options as locals at the top, all code in one `do ... end` (compare appends it
   to its own script), the same hook: `emu.addMemoryCallback(fn, emu.callbackType.exec, <addr>, <addr>,
   emu.cpuType.snes, emu.memType.snesMemory)` at the plugin's `hle` address, so both write before the same instruction.
   No twin when MesenCE cannot show or time it (a picture past 256 px, logic that costs no emulated time): the page
   says why.
4. **Check parity.** The three commands in `docs/mods/README.md`: an input file `tools/mods/<mod>-*.txt` that exercises
   every feature and switches toggles back off, the twin against the plugin, then the plugin off against the
   recording. Done when the first run is in sync to its end **and** the control parts where the input first acts;
   a control that never parts means the input missed the mod.
5. **Pack.** `node tools/mods/pack.mts <id>`, then `python3 -I -m zipfile -t out/mods/<id>-<version>.snesmod`.
6. **Page and table.** `docs/mods/<mod>.md` from the template, a dated row per check with its numbers; the README row's
   *Same?* says the parity result or why there is no twin. `ctest` green (8 tests), `docs/README.md` links the folder.

## Traps

- **Replays are vanilla.** A mod that changes the game is never matched against a replay; replays and their MesenCE runs
  compare WRAM and picture with no mod active unless the user asks. The parity check above is the one exception: the mod
  on both sides, input written for it. Every new mod that changes the game gets an `=0` in `tests/replay_recorded.sh`.
- `build/mods/preloaded/state.toml` is the player's selection, shared by every run of `build/`: test from a copy of the
  build with its own `state.toml`, or through the `SOE_<NAME>` switch; never edit the player's.
- The NMI counter (compare's key) runs behind the movie's frames: no NMI during loads. Input appended after replay 02's
  23,756 frames acts near key 23,000.
- MesenCE `--testrunner` Lua has no `io`; exec callbacks see one bank (`$8E:E45C`, not `$0E:E45C`).
- Lua `emu.write` and the plugin's C both cost no emulated cycles; a twin that waits for `endFrame` instead of the hook
  writes at another moment and parts.
- Hook inside the game's tick through an existing `hle` entry (`src/soe_bindings.c`); a new one is a
  `recomp/seed-exclusions.txt` line and goes through everscript-vscode's export (recomp skill).
- A save state holds guest memory, not plugin state: toggles kept in C are off after a load. Say so on the page.
