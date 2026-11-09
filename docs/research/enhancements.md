# Recomp Enhancements: Widescreen, Localization, Overlays, and Randomizers

Static recompilation transforms Secret of Evermore from a fixed 16-bit cartridge into a
modern, extensible desktop application.

This article details how to implement widescreen (16:9 / 21:9), runtime translations,
in-game map overlays, and native randomizer plugins.

---

## 1. Widescreen Support (16:9 and 21:9)

Moved and rewritten from the game's code: [../widescreen/README.md](../widescreen/README.md)
(2026-11-09). Two claims of the earlier text here were wrong: the camera is not `$7E00E2/E4` but
DP `$BF`/`$C1` (target) and `$59`/`$5B` (rendered), and rooms are not "up to 4096×4096": the header
allows 255 cells (4080 px), the largest vanilla room is 128 × 125 cells (2048 × 2000 px). The
"prefill from the room grid" and "clamp at world edges" ideas stand; the clamp must use the active
camera limits `$23E9..$23EF`, which scripts narrow (see `../widescreen/camera-and-limits.md`).

---

## 2. Runtime Translations & Multi-Language Support

Historically, playing Evermore in other languages required applying an IPS/BPS translation patch
directly to a ROM file. With recompilation, translations operate as **declarative runtime packs**.

### 2.1 How it Works (`RUNTIME_LOCALIZATION.md`)
The runner provides a declarative text translation system driven by simple TOML tables:

```c
snes_text_xlate_init_c("translations/evermore_de.toml", "de");
snes_mod_register_frame_callback(snes_text_xlate_on_frame_c);
```

### 2.2 Patch Vocabulary in TOML
```toml
schema = 1
default_lang = "en"
fallback_de = "en"

# Patch dialog strings on the fly in memory
[[rom_patch]]
address = 0x1D8000
source_hex = "..."       # Verified clean US bytes
de_hex = "..."           # German translated bytes

# Inject updated font glyphs into VRAM
[[vram_patch]]
address = 0x6000
source_hex = "..."
de_hex = "..."
```

### 2.3 Benefits
- **Zero ROM Alteration:** The clean US ROM (`79e7738630...`) remains untouched.
- **On-the-Fly Switching:** Players can change languages in the options menu without restarting.
- **Community Localization:** Translators distribute lightweight `.snesmod` archives containing
  only text tables, not copyrighted ROM assets.

---

## 3. Porting Extension Overlays: Live Map, Hitboxes, and Pause Bar

In `everscript-vscode`, we developed rich visual inspection tools: the Room Map viewer, collision
bitfield visualizer, and live entity inspector. We can port these directly into the recompiled
engine as **Host Overlays**.

### 3.1 Live Map & Hitbox Overlay
Because the recompiled executable uses SDL and OpenGL/Metal/Vulkan backends, we can render an
ImGui or hardware overlay directly onto the presentation surface:

```text
 ┌────────────────────────────────────────────────────────┐
 │ [F3] Evermore Inspector Overlay                        │
 │  ☑ Collision Grid (Passable / Blocked / Water / Ledges)│
 │  ☑ Entity Hitboxes (Green = Hurt, Red = Strike)       │
 │  ☑ Trigger Boundaries (Doorways, Script Step-ons)      │
 │  Camera: (1240, 0892) | Room: $2B (Salabona)           │
 └────────────────────────────────────────────────────────┘
```

- **Collision Grid:** Reads the active collision word array (`$7E2000+`) and draws semi-transparent
  walkable/solid overlays over the gameplay tiles.
- **Hitboxes & Triggers:** Draws live colored rectangles around active enemies, the dog, projectile
  boxes, and doorway step-on sensors.

### 3.2 In-Game Pause Menu Bar
Instead of relying on hotkeys for emulator functions:
- A host overlay menu bar appears at the top of the screen on pressing Escape or Gamepad Back:
  - **Game:** Soft Reset, Load/Save State (1–10), Quick Save.
  - **Display:** Aspect Ratio (4:3, 16:9, 21:9), CRT Shader presets, Integer Scaling.
  - **Audio:** Master Volume, MSU-1 Orchestral toggle, Voice volume.
  - **Mods:** On-the-fly addon toggles (2-Player co-op, instant text).

---

## 4. The Evermore Randomizer as a Native Plugin

Randomizers for SNES games are typically external Python or web tools that generate a new ROM file.
In the recompilation architecture, the Randomizer can be implemented natively as a **Trusted Plugin**
or a **Content Variant**.

### 4.1 Native Randomizer Plugin
Implemented in C/C++ within the Game RTL:
1. **Seed Input UI:** Players enter a seed number and toggle flags (e.g. *Open Sand Crab Island*,
   *Shuffle Boss Drops*, *Fast Alchemy*) in the launcher or an in-game prompt.
2. **Deterministic Memory Shuffling:** On session boot (`GameSessionReset`):
   - Shuffles the gourd/chest loot tables (`tables.js`).
   - Shuffles alchemy ingredient costs and NPC formula gifts.
   - Adjusts doorway destination tables (`map_transitions.ts`).
3. **No ROM Patches:** Every seed generates instantaneously in memory.

### 4.2 Content Variant Model (`CONTENT_VARIANTS.md`)
If a community randomizer significantly rewrites engine assembly routines (e.g. adding custom shop
logic or new weapon types):
- The modified game is compiled as an independent program module: `src/gen-rando/`.
- Packaged as `evermore-rando.snesmod`.
- Player save files (`saves/rando_save.srm`) remain completely isolated from vanilla playthroughs.
