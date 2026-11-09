# Cutscenes: detection and fallback

Tag `widescreen.cutscene`. Marks: [README](README.md).

## Is there a cutscene flag?

None is known. Signals that exist:

| Signal | Address | Mark |
|---|---|---|
| Script slots: 20 × `$4F` bytes, `+$00` address (24 bit), `+$03` state (2 run, 4 wait, 0 free), `+$0B` parent, `+$0D` entity | `$7E:28FC`; run list `$7E:2F28` | L (layout from everscript-vscode `src/games/evermore/debugger/script-frames.ts`, verified headless there) |
| Camera follows a scripted point | `$242B` ≥ 0 | V (`$90:9EA1`) |
| Camera follows another actor | `$0F42` ≠ Boy/Dog | L |
| Dialogue lock | `$22EE` bit 0 | U |
| Boy halted | Boy `+$10 & $0020` (`$8F:CD7E`) | U |
| Cutscene freeze | `field_cutscene_freeze_init_and_entities_deactivate` `$8E:EE124`, via `$143C` | U |

Background scripts (step triggers, ambient loops) run all the time, so "a script is running" is not
enough.

## Proposal

1. Build a **cutscene script list** offline in everscript-vscode from the script tree
   (`soe://rom/assets/scripts/`, per room `soe://rom/assets/maps/<id>/scripts/`): every script that
   writes `$242B`/`$242D`, moves the camera (`teleport_screen`, opcode `0x96`; camera opcodes), halts
   or teleports the Boy, or runs dialogue with actors. Export start addresses as a small table.
2. The recomp scans the 20 slot states and addresses once per frame (cheap) and ORs with
   `$242B ≥ 0`. A slot whose call chain (`+$0B`) leads to a listed script counts.
3. While true: no ghosts, no experiments; fall back to the "Cutscenes" setting.

## Does the fallback make sense?

Yes. Scripts place actors off screen and walk them in; scripted cameras pan away from the Boy and
expose map parts the designers never showed; scripted spawns appear at chosen spots. A wide view
reveals staging (actors waiting in the margin), and ghosts would announce them. Options:

| Setting | During a cutscene |
|---|---|
| 4:3 (default) | pillarbox; the cleanest and closest to vanilla |
| vanilla enemies | stay wide, sprites clipped at 256, no ghosts; staging actors stay hidden, only backgrounds show |
| keep | no change (for testing) |

Switching aspect mid-scene is jarring; animate the pillarbox in over ~10 frames.
