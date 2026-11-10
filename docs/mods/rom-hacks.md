# ROM hacks as mods

How a Secret of Evermore ROM hack (an Everscript build, an Asar patch, an IPS/BPS file) reaches the recomp: **not by
recompiling the patched ROM**, but as a `.snesmod` against the stock US ROM. The player keeps the clean ROM; the mod
changes the game in memory at launch. snesrecomp has deprecated the other path (recompiled ROM hacks, `[[patch]]` and
content variants: `snesrecomp/docs/MOD_AUTHORING.md` "Reimplement an existing ROM hack", `MOD_PACKAGES.md` "Deprecated
patched-ROM recompilation path"). This page is that rule applied to Evermore; [README.md](README.md) lists the mods.

## Why a patched ROM does not just work

The executable is generated from one stock image (its SHA-256 is checked at launch). What the generated code does with a
changed byte depends on what the byte is:

- **Data is read at run time.** Compiled routines still read tables, pointers, graphics and tilemaps from the ROM image
  over the bus, and the game's own script interpreter (`$8C:D0A6`) runs whatever Everscript bytecode the image holds.
  Changed data takes effect.
- **Code is compiled in.** A routine that exists as C runs as C: changed 65816 bytes at its address are never executed.
  The interpreter fallback (`snesrecomp/docs/LLE_FIRST_ANALYSIS.md`) covers addresses the seeds missed; it is a safety
  net, not a way to ship mods.
- **Layout is fixed.** A bigger ROM (ExHiROM, a 4th or 5th MB), another mapping, or moved banks no longer matches the
  image the code was generated from.

## The three kinds of change

| A hack changes | In the recomp | Ship it as |
|---|---|---|
| **Data**: script bytecode, script pointers, text, tilemaps, graphics, tables, bytes in free space | works | verified byte patches (below) |
| **65816 code**: Asar hooks (`JSL` to new code), rewritten routines, changed opcodes | ignored | a trusted C plugin in this repo |
| **ROM size or layout**: ExHiROM, the table hack's relocated tables, a remapped header | unsupported | rework as data a plugin loads, or leave out |

Most Everscript hacks are the first kind: the compiler writes bytecode and repoints scripts, into existing free space
when it fits. A hack that also ships `patches/*.asm` (everscript's Asar patches) has code in it. For those hacks the
compiler, which knows what every byte it wrote is, should write the mod itself: [evs-to-snesmod.md](evs-to-snesmod.md)
(the practice ROM as the worked example).

**A 4 MB hack is not the third kind here.** A 3 MiB ROM is loaded into a 4 MiB buffer (`snes_other.c` pads to a power of
two by mirroring: `0x300000–0x3FFFFF` = a copy of `0x200000–0x2FFFFF`), the identity hash covers only the first 3 MiB
(`romImageSize`), and `[[rom_patch]]` checks bounds against the 4 MiB `cart->romSize`. A plugin can therefore write a
hack's 4th MB into the cartridge, and bus reads of `$F0`–`$FF` see it. Not yet checked: that vanilla never reads the
mirror. Banks past `$FF` or a remapped header (ExHiROM) stay unsupported.

## The process

1. **Build the hack and diff it.** Compile it with everscript into a copy of the stock ROM, then list every changed
   range against the vanilla ROM (offset, length, old bytes, new bytes). Neither ROM nor diff is committed.
2. **Name each range.** Use the knowledge in everscript-vscode (`soe://rom/...`, the ROM tab, the compiler's own output
   map) to say what each range is: a script, a pointer table entry, a tilemap, a routine. A range nobody can name is
   not shipped.
3. **Sort the ranges** into the three kinds above. A range the size grew past the stock image is the third kind.
4. **Data → byte patches.** snesrecomp's runtime table (`snesrecomp/docs/RUNTIME_LOCALIZATION.md`,
   `runner/src/snes_text_xlate.cpp`) has `[[rom_patch]]` (the in-memory cartridge after boot), `[[ram_patch]]` and
   `[[vram_patch]]`. Each entry carries `source_hex`, the stock bytes, and is skipped if they do not match, so a wrong
   ROM is never half-patched. Payload width equals source width: new content goes into existing free space, whose
   filler bytes are the `source_hex`. **`address` is a file offset into the cartridge image, not a bus address**:
   `$C0:0000` and up maps to `bus & $3FFFFF`; a `$80`–`$BF` bus address needs its HiROM mirror worked out first. Generate
   the table from the step-1 diff, never write it by hand.
5. **Code → a trusted plugin.** Rewrite what the patch does in C (`src/soe_<name>.c`, `SNES_MOD_CONSTRUCTOR`), hooked
   through an `hle` entry of `src/soe_bindings.c` or a frame callback registered at activation. The practice hotkeys
   ([cheats.md](cheats.md)) are the worked example: an everscript hack, reimplemented at `HleFieldTickFreeBegin`. Then
   rebuild the executable; the archive cannot carry code.
6. **Package it.** A manifest with one `[[feature]]` per thing the player can switch, its `[[plugin]]` and `[[option]]`s;
   the byte-patch table as package data; `node tools/mods/pack.mts <id>`. The `mods` skill has the layout.
7. **Prove it.** The same check as every mod (README.md, "The parity check"): the hack's ROM in MesenCE against the recomp
   with the mod on, from the same replay, WRAM/VRAM/CGRAM/OAM identical at every NMI; then the mod off, which must part
   at the first frame the hack acts. Write the mod's page here and its row in README.md.

## Open gaps

- **No byte patches from a package yet.** This repo does not call `snes_text_xlate_init_c` or apply `[[rom_patch]]`
  anywhere; snesrecomp loads that table from a path a game plugin gives it, not from the `.snesmod` by itself. A
  data-only hack needs a plugin that loads its package's table first (one shared plugin for every data hack).
- **No diff-to-table tool.** Steps 1–4 are by hand today; the tool belongs in `tools/mods/`. For Everscript hacks the
  compiler is the better source than a diff: it names every range ([evs-to-snesmod.md](evs-to-snesmod.md)).
- **`source_hex` ships stock bytes.** Every `[[rom_patch]]` carries the vanilla bytes it replaces; for a hack's 4th MB
  that is the mirror, kilobytes of ROM content in the package. Data that big goes through a plugin that applies its
  own payload after the launcher's whole-ROM SHA-256 check (evs-to-snesmod.md, "Why patch.ips").
- **Same-width only.** A hack that relocates or grows data past free space has no path except a plugin that serves the
  data itself (the 4th MB above, or a plugin that writes the payload).
