# Experimental Graphics: Dynamic Lighting, HD Mode 7, and Modern Renderers

By decoupling game logic from 16-bit PPU constraints, a static recompilation can introduce
modern visual enhancements that were impossible on original SNES hardware.

This article explores experimental graphics techniques for Secret of Evermore:
**dynamic 2D lighting**, **HD Mode 7 flight rendering**, **high-framerate motion interpolation**,
and **color-science CRT simulation**.

---

## 1. Dynamic 2D Lighting Engine

### 1.1 How Vanilla Evermore Simulated Darkness
The SNES PPU possessed no lighting engine. Secret of Evermore simulated dark environments
(such as the Dark Forest, the Volcano tunnels, and the Gothica Sewers) using:
- **PPU Color Math:** Master brightness attenuation (`$2100` INIDISP) and subscreen subtraction (`$2131/$2132`).
- **HDMA Gradients:** Scanline-by-scanline palette fades simulating swamp mist or cave depth.
- **Window Masks:** Inverting window planes (`$2123..$212B`) to draw circular "spotlights" around the player.

These techniques were strictly 2D full-screen tints with no surface awareness or realistic light falloff.

### 1.2 Implementing True Dynamic Lighting in Recomp
Because the recompiled runner operates as a native 3D/shader application (using modern OpenGL,
Metal, or Vulkan renderers), we can inject a real-time lighting pass over the PPU's output.

```text
 ┌────────────────────────────────────────────────────────┐
 │ 1. Entity Extraction from WRAM                         │
 │  • Read entity table ($7E0E8E..$7E1E8E) for coordinates│
 │  • Identify player, companion, torches, and spells     │
 └───────────────────────────┬────────────────────────────┘
                             │
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │ 2. Shadow Caster Generation                            │
 │  • Extract solid wall geometry from collision words    │
 │  • Generate 2D shadow silhouette rays                  │
 └───────────────────────────┬────────────────────────────┘
                             │
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │ 3. Fragment Shader Compositing                         │
 │  • Compute radial light falloff & color attenuation    │
 │  • Composite dynamic lighting buffer over PPU BG layers│
 └────────────────────────────────────────────────────────┘
```

#### Step 1: Light Emitters from WRAM
By reading entity slots in WRAM every frame:
- **Player & Dog:** Emits a soft, warm local ambient radius.
- **Dungeon Torches & Braziers:** Fixed light sources with subtle high-frequency radius flicker.
- **Alchemy Spells:**
  - *Fireball / Flame Flare:* Fast-moving bright orange/red point light casting sharp dynamic shadows.
  - *Flash:* Screen-wide blinding white strobe.
  - *Corrosion / Acid Rain:* Sickly green/purple ambient illumination.
  - *Hard Ball:* Subtle metallic glint.

#### Step 2: 2D Shadow Casting from Collision Data
Evermore's collision map is loaded into WRAM as a grid of 16-bit collision words (`$7E2000+`).
- The lighting engine treats non-passable solid wall tiles as 2D shadow occluders.
- A fragment or compute shader casts smooth 2D shadows from torches around dungeon corners,
  creating deep atmospheric depth in interior dungeons.

---

## 2. HD Mode 7 (Windwalker Flight Sequences)

### 2.1 The Problem in Vanilla Mode 7
In Act 4, the player flies across the world map inside the Windwalker (Flying Machine).
On authentic SNES hardware, Mode 7 renders a 1024×1024 pixel background by applying affine matrix
transforms (`$211B..$2120`) to calculate one sample per 256×224 screen pixel.
- Near the horizon, severe pixel shimmering and severe stair-stepping pixelation occur.

### 2.2 Recomp High-Resolution Mode 7 (`HD_MODE7.md`)
snesrecomp includes native support for fractional Mode 7 supersampling (`PpuBindMode7HdSurface`):

```c
// Allocate high-definition Mode 7 framebuffer (e.g. 4x scale)
unsigned width = 256 + 2 * ppu->extraLeftRight;
unsigned height = 224, scale = 4;
size_t pitch = (size_t)width * scale * sizeof(uint32_t);
size_t capacity = pitch * height * scale;
uint32_t *hd_surface = malloc(capacity);

PpuBindMode7HdSurface(ppu, hd_surface, capacity, pitch, width, height, scale);
```

### 2.3 How It Looks
- Instead of sampling once per low-res screen pixel, the affine transform evaluates sub-pixel
  coordinates across a $4\times$ or $8\times$ resolution raster.
- The world map texture displays crisp, smooth terrain and coastline detail all the way to the horizon.
- **Authenticity Preserved:** The underlying 65816 game flight physics, camera coordinates, and
  landing triggers remain 100% byte-identical.

---

## 3. High-Framerate Motion Interpolation

On vanilla SNES hardware, Evermore runs at roughly 60 Hz NTSC, but complex scenes with numerous
enemies and particle effects frequently experience frame drops down to 30 Hz or lower.

### Sub-Frame Interpolation in Recomp
Because CPU emulation and PPU presentation are decoupled in the modern runner:
1. **Camera Smoothing:** The camera coordinates (`$7E00E2/$7E00E4`) can be interpolated between
   consecutive engine logic frames. When played on a 120Hz, 144Hz, or 240Hz monitor, scrolling
   becomes completely fluid and judder-free.
2. **Entity Position Smoothing:** Sprite positions recorded across the previous and current frame
   can be smoothly blended along their movement vectors during display scanout.

---

## 4. Accurate Color Science & CRT Simulation (`SHADOW_ENHANCEMENTS.md`)

Modern LCD and OLED monitors have vastly different gamma curves, color spaces, and subpixel layouts
compared to the 1990s cathode-ray tube (CRT) televisions for which Evermore's art was created.

### 4.1 Color Lookup Tables (`color_lut.c`)
snesrecomp provides a CIE color-science transformation pipeline supporting multiple display profiles:

| Profile | Target Calibration | Effect on Evermore Artwork |
|---|---|---|
| **`raw`** | sRGB Identity | Exact digital 15-bit SNES RGB values (bright, high contrast). |
| **`crt`** | SMPTE-C Phosphors | Warm, authentic color reproduction matching mid-90s consumer CRTs. |
| **`trinitron`** | Sony Trinitron Aperture Grille | Rich, deep blacks with vibrant phosphor saturation. |
| **`composite`** | NTSC Composite Chroma Bleed | Simulates analog color crosstalk and subcarrier blending, which artists originally leveraged to blend dithering patterns into smooth gradients. |

The color LUT operates entirely at presentation time. Frame hashes, memory verification, and
lockstep testing continue to run against the raw unshaded buffer, guaranteeing that visual
enhancements never compromise emulation accuracy.
