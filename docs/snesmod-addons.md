# The `.snesmod` Package Architecture, Addons, and Configuration

The [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) framework includes an opt-in,
modular modding architecture based on **`.snesmod` packages** and **Trusted Plugins**.

This article explains how `.snesmod` works, what languages are used, how options and configuration
work, how other recompilation projects use it, and which existing Secret of Evermore patches and
hacks make ideal addons.

---

## 1. How `.snesmod` Works: Architecture and Security

### 1.1 The Package Archive
A `.snesmod` package is a standard **ZIP archive** with a root `manifest.toml`:

```text
my_addon.snesmod (ZIP archive)
├── manifest.toml              <- Package descriptor, features, options, targets
├── assets/                    <- Textures, tilemaps, palettes, audio (optional)
│   └── audio.pcm
└── patches/                   <- Declarative byte patches (optional)
    └── strings.toml
```

### 1.2 The Trust and Safety Model
To ensure cross-platform portability and security, `.snesmod` follows the architecture
pioneered by `psxrecomp` and `N64Recomp`:

1. **Archives Contain Data Only:** A `.snesmod` archive **never** contains DLLs, `.so` shared
   libraries, native object files, or executable bytecode.
2. **No Arbitrary Code Execution:** The loader verifies ZIP CRCs, caps extraction at 4,096 files
   and 256 MiB uncompressed, rejects path traversal (`../`), and stages extraction into a sandbox.
3. **Trusted Plugins for Native Logic:** Native gameplay modifications are written in **C or C++**
   and statically linked into the game executable. They register themselves under unique, stable IDs
   using `SNES_MOD_CONSTRUCTOR`.
4. **Declarative Activation:** The `.snesmod` manifest declares which features and trusted plugins
   it activates, which ROMs it targets, and what configuration options it exposes.

```text
 ┌──────────────────────────────────────┐       ┌──────────────────────────────────────┐
 │       .snesmod Archive (ZIP)         │       │        Desktop Executable (C/C++)    │
 │  • manifest.toml                     │       │  • Recompiled Evermore Logic         │
 │  • Options (bool, choice, integer)   │ ────► │  • Trusted Plugins (static C/C++)    │
 │  • Declarative patches & asset files │       │  • Game Runtime Layer (Game RTL)     │
 └──────────────────────────────────────┘       └──────────────────────────────────────┘
```

---

## 2. What Language is it Written in?

A mod is written across two layers depending on its complexity:

| Component | Language | Role |
|---|---|---|
| **Package Manifest** | **TOML** (`manifest.toml`) | Metadata, feature definitions, UI options, target ROM hashes, asset references. |
| **Declarative Patches** | **TOML** (`patches/*.toml`) | Hex byte replacements for ROM, RAM, or VRAM (e.g. text translations, palette tweaks). |
| **Trusted Plugins** | **C11 / C++17** | Custom game code, hooks into Evermore's Game RTL, entity modifications, UI widgets. |

### 2.1 Manifest Example (`manifest.toml`)
```toml
format_version = 1
id = "soe.twoplayer"
version = "1.0.0"
name = "Secret of Evermore 2-Player Co-op"
author = "Evermore Community"
description = "Enables full 2-Player co-op with Controller 2 controlling the companion."
license = "MIT"
save_compatibility = "shared"

[[target]]
game_id = "secretofevermore"
rom_sha256 = "c08650..."

[[feature]]
id = "coop"
name = "2-Player Co-op"
description = "Assigns Player 2 to control the dog or boy."
group = "Gameplay"
default_enabled = true

[[plugin]]
feature = "coop"
id = "soe.twoplayer.native"

[[option]]
feature = "coop"
id = "dog_ai"
label = "Companion Behavior on Idle"
type = "choice"
default = "vanilla_ai"

[[option.choice]]
value = "vanilla_ai"
label = "Vanilla Dog AI (sniffs, attacks)"

[[option.choice]]
value = "hold_position"
label = "Hold Position"
```

### 2.2 Native Trusted Plugin Example (C)
In the host project's C codebase:
```c
#include "mod_runtime.h"

static void enable_two_player(void) {
    // Hook Evermore input dispatcher in WRAM / Game RTL
    g_two_player_enabled = true;
}

static void reset_two_player(void) {
    g_two_player_enabled = false;
}

SNES_MOD_CONSTRUCTOR(register_two_player_plugin) {
    snes_mod_register_reset_callback(reset_two_player);
    snes_mod_register_activation_plugin("soe.twoplayer.native", enable_two_player);
}
```

---

## 3. Configuration & Options

The snesrecomp launcher's **Mods** page reads `manifest.toml` and dynamically renders an
interactive UI. Player selections are saved to `mods/state.toml`.

### Supported Option Types:
1. **`boolean` (Checkboxes):**
   ```toml
   [[option]]
   feature = "gameplay"
   id = "fast_text"
   label = "Instant Dialogue Text"
   type = "boolean"
   default = true
   ```
2. **`choice` (Dropdowns / Radio buttons):**
   ```toml
   [[option]]
   feature = "display"
   id = "aspect_ratio"
   label = "Target Aspect Ratio"
   type = "choice"
   default = "16:9"

   [[option.choice]]
   value = "16:9"
   label = "16:9 Widescreen"

   [[option.choice]]
   value = "21:9"
   label = "21:9 Ultrawide"
   ```
3. **`integer` (Bounded Numeric Sliders):**
   ```toml
   [[option]]
   feature = "audio"
   id = "orchestral_volume"
   label = "MSU-1 Music Volume (%)"
   type = "integer"
   default = 100
   min = 0
   max = 150
   ```

---

## 4. Examples from Other Recomp Games

Other titles in the `snesrecomp` / `N64Recomp` family demonstrate what `.snesmod` achieves:

| Game Recomp | Mod / Addon | How it Works |
|---|---|---|
| **Mega Man X Recomp** | **Widescreen 16:9** | Statically linked trusted plugin `mmx.widescreen` with margin prefilling hooks. |
| **Gundam Wing: Endless Duel** | **Runtime English Translation** | Declarative TOML translation file (`endless_duel.toml`) patching ROM/VRAM strings on the fly without touching the Japanese ROM. |
| **Zelda 64 Recomp** | **RTX Dynamic Lighting / Gyro Aim** | Trusted native plugins hooking game actor coordinates and camera matrices. |
| **Super Mario World Recomp** | **Content Variants (ROM Hacks)** | Separate compiled modules bundled into `.snesmod` packages, selectable in-game with Left/Right. |

---

## 5. Ideal Secret of Evermore Addons

Existing community patches and reverse-engineering research in this repository map cleanly
to `.snesmod` packages:

### 5.1 The 2-Player Co-op Mod (`soe-2player.snesmod`)
- **Background:** Documented in [`docs/soe-2player-patch-analysis.md`](https://github.com/r-bin/everscript-vscode/blob/develop/docs/soe-2player-patch-analysis.md).
- **Functionality:** Replaces the companion AI loop so that Joypad 2 controls the companion
  (Dog, or Boy in Dog-first chapters).
- **Configuration Options:**
  - Assign Joypad 2 to Dog / Boy.
  - Toggle whether the companion reverts to vanilla AI when Controller 2 is idle.
  - Dedicated Ring Menu button for Player 2.

### 5.2 Essential Bugfix & Balance Patches
- **5 Status Effects Fix:**
  - Documented in [`docs/5-status-effects-bug.md`](https://github.com/r-bin/everscript-vscode/blob/develop/docs/5-status-effects-bug.md).
  - Vanilla SoE corrupts RAM when more than 4 status effects tick simultaneously.
  - An addon can fix the status timer loop natively in C or via a guarded RAM patch.
- **Naris Super Heal & Prophet Bronze Armor Fixes:**
  - Documented in [`docs/Naris Super Heal Logic.md`](https://github.com/r-bin/everscript-vscode/blob/develop/docs/Naris%20Super%20Heal%20Logic.md) and
    [`docs/Prophet Bronze Armor Logic.md`](https://github.com/r-bin/everscript-vscode/blob/develop/docs/Prophet%20Bronze%20Armor%20Logic.md).
  - Corrects vanilla arithmetic logic bugs in boss spell scaling and shop armor values.

### 5.3 High-Fidelity Audio / MSU-1 Orchestral Soundtrack
- **Mechanism:** snesrecomp provides a trusted PCM overlay mixer (`snes_mod_audio_register_pcm_s16`)
  and full MSU-1 audio playback.
- **Addon:** Bundles FLAC/WAV or signed-16 PCM recordings of Jeremy Soule's atmospheric soundtrack
  and ambient environmental tracks. When enabled, the game fades the 8-channel BRR audio and plays
  lossless stereo music synced to the current room.

### 5.4 Quality-of-Life (QoL) Suite
- **Instant Dialogue:** Skips typewriter delays in message boxes.
- **Sprint by Default:** Enables running without draining stamina or holding B.
- **Direct Alchemy Shortcuts:** Binds favorite formulas directly to shoulder triggers (L/R)
  without opening the Ring Menu.
- **Ingredient Counter in Status HUD:** Shows held alchemy ingredient counts on screen.
