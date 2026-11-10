# Everscript hacks as mods (evs → snesmod)

How a hack written in Everscript (the practice ROM, souls, …) becomes a `.snesmod` for this recomp, explained with the
practice ROM (everscript `in/practice/main.evs`) as the example. **Status: a plan.** Nothing below is built yet except
what the table at the end says exists. The general rule for every ROM hack is [rom-hacks.md](rom-hacks.md); this page
is the Everscript case, which is the easy one.

## The idea in four sentences

The recomp has the game's *code* compiled in, but it still reads the game's *data* from the ROM while it runs, and the
game's script interpreter is part of that code: it runs whatever script bytecode the ROM holds. An Everscript hack is
almost all script bytecode, plus text and pointer tables, so it is data. The compiler already knows what each byte it
writes is, so it can package those bytes as a mod instead of only as an IPS. The few parts that are 65816 code
(Asar patches) become small C plugins in this repository, and the compiler only names them.

```
in/practice/main.evs ──everscript──►  out/everscript.ips           (as today: the ROM hack)
                                      out/everscript.snesmod       (new: the same data, as a mod)
                                         ├─ manifest.toml
                                         ├─ everscript/patch.ips   data only: scripts, text, pointers
                                         ├─ everscript/slots.toml  entry points the C plugins start
                                         └─ everscript/names.toml  what each range is (from memory_map.txt)

the recomp, at launch:  stock ROM ─► plugin soe.everscript writes patch.ips into the in-memory cartridge
                                  ─► the game's own interpreter runs the new scripts
                                  ─► entry-point plugins (menu, hotkeys) start scripts from slots.toml
```

## Does the compiler have to know the recomp's code?

No. It writes data files whose format is fixed here; the recomp's C reads them. The compiler needs four facts, all
plain text:

| The compiler needs | Why | Where it gets it |
|---|---|---|
| The stock ROM's identity: `game_id = "secretofevermore-usa"`, its SHA-256 | the manifest's `[[target]]`: the launcher refuses the mod on any other ROM | it already opens the ROM with `--rom`; the hash is in `mods/preloaded/packages/*/manifest.toml` |
| The manifest format | `format_version`, `id`, `version`, `[[feature]]`, `[[plugin]]` | `snesrecomp/docs/MOD_PACKAGES.md`; the example below |
| The loader plugin's id and data format | `[[plugin]] id = "soe.everscript"`, reading `everscript/patch.ips` and `slots.toml` | this page, "The package" |
| The entry-point slots | which addresses a C plugin starts as scripts (debug ring buttons, hotkeys) | `in/core/…/01_rom.evs` (`DEBUG_MENU_BUTTON_n`, `HOTKEY_*`): the compiler already knows them for `@inject` |

It needs nothing about seeds, bindings, `hle` entries or how the C starts a script. If this repository changes its C,
the compiler is unaffected for as long as the four contracts stay the same.

## What a hack is made of, and where each part goes

Every byte the practice build writes, sorted (counted from its `out/everscript.ips` and `out/everscript.combined.ips`
of 2026-11-10):

| Part | Example from the practice ROM | Kind | In the mod |
|---|---|---|---|
| New scripts | the portals, Brian's room, the actions behind the debug ring buttons: about 12 KB at file `0x35xxxx` (bus `$F5`) | data, past the 3 MB stock image | `patch.ips` |
| Script and text pointer tables | `$D2:8294…` (`FunctionKey` entries of the `$92:8000` pointer table), `$D1:D546…` (`StringKey` entries) | data, in the stock image | `patch.ips` |
| A room's enter script | `$D2:8084` = `$92:801B + 5 × 0x15`: Brian's room (`MAP.BRIAN = 0x15`), `MapKey(928084)` in `memory_map.txt` | data | `patch.ips` |
| Script edits in place | `camera_hack.evs`: `4D` bytes over existing script code, e.g. `$D3:84DF` (`@install(0x9384df, False)`) | data | `patch.ips` |
| Entry-point slots | `$FD:8000`, `$FD:8040` … (`DEBUG_MENU_BUTTON_1..11`, `HOTKEY_*`): each holds `29 pp pp pp`, a call of the action script | data, written by `@inject` | `patch.ips` + `slots.toml` |
| Asar patches | `debug_menu`, `_hook_input`, `hotkeys`, `room_timer` (`#patch(...)`): hooks such as `$C0:84A3 JSL $FE5029`, ring item names at `$C4:65F5…` | **code** (and the data that only that code uses) | not in the mod: the manifest lists them as *needs a plugin*; each becomes C here |

A packed script pointer (`29 cc 8e 11` = call `$118ECC`) is the offset from `$92:8000` in 32 KB windows
(everscript-vscode `docs/scripts/vm.md`): bank `$92 + ($118ECC >> 15) = $B5`, address `$8000 | $0ECC`, which is
`$B5:8ECC` = file `0x358ECC`, the first byte of that action's bytecode in `patch.ips`. The asm debug menu starts button 1
by putting the packed pointer `$158000` into DP `$26`–`$28` (`$BD:8000` = `$FD:8000`, the slot), then running the
same steps as opcode `0x29`: allocate a thread (`$8C:CF18`), append it to the run list `$7E:2F28`.

## Why the 4 MB extension works without a new framework feature

The practice ROM grows the cartridge from 3 MB to 4 MB and puts its scripts in the 4th MB. The recomp loads the 3 MiB
stock ROM into a 4 MiB buffer (`snesrecomp/runner/src/snes/snes_other.c`: the image is padded to a power of two by
mirroring, so `0x300000–0x3FFFFF` holds a copy of `0x200000–0x2FFFFF`). The ROM identity hash covers only the first
3 MiB (`romImageSize`, `common_cpu_infra.c`). A plugin writing into `cart->rom` up to `cart->romSize` reaches the 4th MB,
and bus reads of `$F0`–`$FF` see what it wrote. **To check before relying on it:** that vanilla code never reads that
mirror (the CDL library has the answer).

## Why `patch.ips` and not a `[[rom_patch]]` table

snesrecomp's runtime table (`[[rom_patch]]`, rom-hacks.md step 4) wants the stock bytes as `source_hex` for each entry.
For the 4th MB that is 12 KB of vanilla bytes (the mirror), shipped inside every package. That is ROM content in a file
people pass around: against this repository's first rule. The IPS needs no stock bytes, and the check `source_hex`
gives is already done: the launcher verifies the whole ROM's SHA-256 before the mod runs. So the loader applies IPS
records, refusing a record past `cart->romSize`. `[[rom_patch]]` stays the right tool for hacks that change a few
bytes and want each one verified.

## The package

An example of what `everscript --snesmod` would write for the practice ROM (names are proposals):

```toml
# manifest.toml
format_version = 1
id = "soe.everscript.practice"
version = "1.3.0"                 # the hack's own version
name = "Practice ROM"
author = "r-bin"
description = "Brian's room as a hub with portals to the speedrun spots, and the practice scripts. Changes the game."
license = "MIT"
resolver = "declarative"
save_compatibility = "separate"   # metadata only (any string); saves made with the hub are not vanilla saves

[[target]]
game_id = "secretofevermore-usa"
rom_sha256 = "17c864a76d498feb6479eee8e7d6807b951c66225033228622bb66754baab1db"

[[feature]]
id = "scripts"
name = "Practice scripts"
description = "The hub, the portals and the intro skip (everscript in/practice)."
group = "Practice"
default_enabled = false

[[plugin]]
feature = "scripts"
id = "soe.everscript"             # the one loader for every Everscript hack
```

```toml
# everscript/slots.toml: what the entry-point plugins may start
[[slot]]
name = "DEBUG_MENU_BUTTON_1"      # the compiler's name
address = 0xFD8000                # bus address of the slot (holds 29 pp pp pp)
script = "menu_presets"           # the @inject function, for messages and the docs

[[slot]]
name = "HOTKEY_START"
address = 0xFD82C0
script = "hotkey_start"
```

```toml
# everscript/names.toml: one line per range of patch.ips (from out/memory_map.txt), for errors and the mod page
[[range]]
address = 0x128084
size = 3
name = "MapKey(928084): enter script of room 0x15 (brians_room)"
```

And what it would say about the parts it leaves out (printed by the compiler, and written into the manifest's
description so the player knows):

```
everscript: --snesmod: 4 patches are 65816 code and are not in the mod:
  debug_menu   (debug ring menu)      -> needs a plugin in SecretOfEvermoreRecomp
  _hook_input  (input hook)           -> covered by the entry-point plugin
  hotkeys      (Start/L/R/B chords)   -> covered by the entry-point plugin
  room_timer   (room timer)           -> needs a plugin
```

## Three worked examples

**1. The camera hack: data only, works as soon as the loader exists.**
`camera_hack.evs` writes `4D` (a no-op) over the camera-limit instructions inside vanilla scripts, at their own
addresses:

```
@install(0x9384df, False)
fun camera_southern_jungle_1() { eval("4D 4D 4D 4D 4D 4D 4D // ..."); ... }
```

In the IPS that is a record at file `0x1384DF`, 14 bytes of `4D`. The loader writes them into the cartridge at launch;
when the interpreter reaches that script it reads `4D` and skips the camera limit. No C changes.

**2. A portal: new data in the 4th MB, still no C.**
`portal_act_2` is new bytecode at `$F5:xxxx`; the NPC that starts it is spawned by Brian's room's enter script, whose
pointer the compiler wrote into the room table (`$D2:8084`). The intro skip (`@install(ADDRESS.INTRO_FIRST_CODE_EXECUTED)`)
sends a new game to that room. All of it is pointers and bytecode: the loader writes them, the game runs them.

**3. A debug ring button: data plus an entry point.**
`menu_presets` is bytecode (data), and `@inject(ADDRESS.DEBUG_MENU_BUTTON_1)` writes a call of it into the slot
`$FD:8000` (data). What starts the slot is XaserLE's ring-menu asm (code): in the recomp, an entry-point plugin has to
do it instead, by starting the script at the slot address the way opcode `0x29` does. Until that plugin exists, the
action is in the cartridge but nothing reaches it. A cheaper entry point, if the ring is not needed: a chord that
starts one script with a `question()` menu of the same actions. That is all bytecode, but it looks different.

## Steps

1. **Compiler (everscript):** `--snesmod` writes the archive above from what it already has: the data-only IPS
   (`everscript.ips`, without the `#patch` asm), `memory_map.txt`, the `@inject` slots, the ROM's hash. It warns for
   each asm patch.
2. **Loader (here):** plugin `soe.everscript`, `src/soe_everscript.c`. At reset it finds its package
   (`snes_mod_runtime_package_root_c`), applies `patch.ips` to `cart->rom` (refusing records past `cart->romSize`), and
   writes nothing when the feature is off. One plugin for every Everscript hack.
3. **Entry points (here):** a plugin that starts a slot's script on a chord, hooked where `soe.cheats` already hooks
   (`HleFieldTickFreeBegin`, `$8E:E45C`); later the ring menu.
4. **Check:** the parity check of [README.md](README.md) with the practice ROM itself in MesenCE on one side and the
   recomp with the mod on the other, the same replay and input, WRAM/VRAM/CGRAM/OAM identical at every NMI. Then the
   mod off, which must part at the first frame the hack acts.
5. **Page:** `docs/mods/<mod>.md` and its row in README.md.

## Traps

- **When to write.** The scripts must be in the cartridge before the game first reads them (the intro skip runs on a
  new game): write at reset, not in a frame callback.
- **`everscript.ips`, not `everscript.combined.ips`.** The combined one holds the asm patches too: their bytes in compiled
  routines do nothing, and their bytes in the extension are 65816 code no one runs.
- **Asm patches can carry data.** The ring item names (`$C4:65F5…`) come from `debug_menu`, not from the scripts; they
  belong to that patch's C port.
- **The hack frees vanilla text.** `#memory(string_key(0x0546)..)` hands vanilla string keys and function keys to the
  compiler, so the mod changes vanilla dialogue (the practice README says so). Those bytes are inside the stock image:
  fine for the loader, but the mod page must say it.
- **Header.** The combined IPS rewrites the checksum at `$C0:FFDC`. The mod never does: the stock ROM stays stock.
- **Third-party asm.** `debug_menu` (XaserLE) and `room_timer` (Skarsnik, `sneshacking`, no licence file) are
  reimplemented from what they do, never from their source.

## What exists today

| Piece | State |
|---|---|
| Data-only IPS, memory map, source map from the compiler | exists: `out/everscript.ips`, `out/memory_map.txt`, `out/source_map.json` |
| A hook in the field tick for entry points | exists: `HleFieldTickFreeBegin`, used by `soe.cheats` ([cheats.md](cheats.md)) |
| The practice ROM's cheats (no clip, Atlas, items, levels) | exists as `soe.practice.cheats`, rewritten in C, not via the slots |
| Package files from a plugin | exists in the framework: `snes_mod_runtime_package_root_c` |
| `--snesmod` in the compiler | not yet |
| `soe.everscript` loader | not yet |
| Entry-point plugin for slots, ring menu, room timer | not yet |
