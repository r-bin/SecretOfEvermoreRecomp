# Mods

Every mod of this recomp, what it does, and whether MesenCE has a matching Lua script that does the same. One page per
mod (this folder); the skill `mods` (`.agents/skills/mods/SKILL.md`) is how to make or change one.

| Mod | Recomp package (`mods/preloaded/packages/`) | Changes the game | MesenCE counterpart (`mods/mesen/`) | Same? | Page |
|---|---|---|---|---|---|
| Practice hotkeys | `soe.practice.cheats` 1.0.0, plugin `soe.cheats` | yes | `soe.practice.cheats.lua` | **same** (2026-11-10, replay 02 + every hotkey) | [cheats.md](cheats.md) |
| Widescreen | `soe.enhancement.widescreen` 1.0.0, plugins `soe.widescreen`, `soe.widescreen.alive` | vanilla modes no, experimental yes | none: MesenCE cannot draw past 256 px | — | [widescreen.md](widescreen.md) |

**Same?** means checked, not believed: the MesenCE script and the recomp plugin, given the same input, leave WRAM, VRAM,
CGRAM and OAM identical at every NMI (`tools/compare --mesen-lua`, below). *Same by design* is not a result.

## Where a mod's parts live

| Part | Where |
|---|---|
| Manifest (features, options, plugin IDs) | `mods/preloaded/packages/<id>/<version>/manifest.toml`; staged beside the game by the build |
| Native code (trusted plugin) | `src/soe_<name>.c` + `.h`, in `CMakeLists.txt`, registered with `SNES_MOD_CONSTRUCTOR` |
| Hook into the game | an `hle` entry of `src/soe_bindings.c` (`recomp/seed-exclusions.txt`), or the host's frame callbacks (`src/main.c`) |
| MesenCE counterpart | `mods/mesen/<id>.lua`: options as locals at the top, everything in one `do ... end` block |
| `.snesmod` archive | `node tools/mods/pack.mts [id]` → `out/mods/<id>-<version>.snesmod` (not committed) |
| Parity input | `tools/mods/<mod>-*.txt`: input appended to a power-on replay by `tools/mods/replay.mts` |
| This page | `docs/mods/<mod>.md`, a row in the table above |

## The parity check

```sh
node tools/mods/replay.mts tools/compare/replays/02-new-game-to-flowers.mmo tools/mods/<input>.txt out/mods/<mod>.evsmv
<KNOB>=1 node tools/compare/compare.mts mesen recomp <rom> --replay out/mods/<mod>.evsmv \
    --regions wram,vram,cgram,oam --mesen-lua mods/mesen/<id>.lua --record out/mods/<mod>.crc     # same?
<KNOB>=0 node tools/compare/compare.mts recomp <rom> --replay out/mods/<mod>.evsmv \
    --regions wram,vram,cgram,oam --reference out/mods/<mod>.crc                                    # did it act?
```

The first run must report no difference to its end. The second, the plugin off against the recorded MesenCE run, must
part at the first frame the mod acts: otherwise the input never reached it and the first result proves nothing.
`<KNOB>` is the mod's environment switch (`SOE_CHEATS`): `1` turns it on with its options' defaults without the
launcher, `0` off; the run must not depend on the launcher's shared `state.toml`. Replay 02 takes about 7 minutes (MesenCE 1.5, the recomp the rest), the control one.
The NMI counter runs behind the movie's frames (no NMI during loads): input appended after replay 02's 23,756 frames acts
near key 23,000, inside the keys the stored 02 run also covers.

## Page template

```markdown
# <Mod name>
One paragraph: what the player gets. Package, feature(s), off by default, changes the game or not.
## Features and options        what each feature and option does (the manifest says it shorter)
## How it works                hook (address, why there), input, gates, what it writes, kill switch
## MesenCE counterpart         file, how to load it, what differs from the recomp plugin and why (or why there is none)
## Checks                      dated rows: what was run, on which emulator or build, the result
```
