# Audio with more entities

Tag `widescreen.audio`. Marks: [README](README.md).

## Facts

* Animation scripts play SFX: `$90:8921` (direct) and `$90:88BC` (panned). For an entity slot the
  direct one is skipped when `+$14 & $20` is set (V); what sets that bit (an off-screen flag?) was
  not found.
* Commands go to the SPC through a synchronous handshake (`$8C:81FD`, polls `$2142` up to `$2800`
  times, U), so every SFX costs CPU time.
* SFX play through `$8C:82DC` without loading a package; script SFX ids are translated by
  `$8C:8362` (V, from the SPC dumping work).
* The screen-relative attenuation/pan block `$8C:8590..85EC` is never executed in the CDL library
  (exported as unreached): probably unused, so the game does not quieten far enemies (U).
* Channel arbitration for SFX: `docs/disassembly/audio/sfx-arbitration.md` in everscript-vscode (U);
  far sounds can cut near ones.
* Samples come per room/music (tables `$81:99D8`, `$81:9A1E`); more enemies of the same room need no
  new samples. Music cues by enemy presence: none known (U).

## Per mode ([modes](modes.md))

* Vanilla, hybrid, E1, E3, E4: the same entities run as in vanilla → no extra sound. Ghosts make
  none (no guest code runs).
* E2, E5: more awake entities → more animation, attack and death SFX; the handshake time can trip
  the lag rule and lower `$1076` ([enemies](enemies.md)).

## Mitigation for E2/E5

An APU-write filter (`snes_mod_register_apu_write_callback`) cannot tell which entity asked. Better:
in bound C of the SFX opcode handlers (`$90:8921`, `$90:88BC`), skip — or send at a lower volume —
SFX whose entity lies outside the 4:3 window. That changes the APU stream, so it is part of the
experiment, not of the vanilla mode.
