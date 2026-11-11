/*
 * Debug overlay: the room's collision, triggers, map objects and characters drawn over the running field,
 * in the colours and shapes of everscript-vscode's map editor (maps/overlay-features.ts, overlay-shapes.ts,
 * collision-overlay.ts; rooms/webview/svg-spawns.js; sprites-motion.js for strikes). docs/mods/debug-overlay.md.
 *
 * Facts (everscript-vscode docs/map-format/room-reference.md, map_collision_mechanics.md,
 * cuttable_grass_mechanics.md, docs/entities/):
 *   room grid     $7F:0000, one word per 16x16 cell (2*(row*$08EE + col)): a metatile id, which is an offset
 *                 into the stamp dictionary, also in $7F; a stamp is BG1 word, BG2 word, collision word (+4)
 *                 (checked against the ROM decode of room $51: 2800 of 2800 cells)
 *   collision     bits 3..0 geometry (drift when bit 13), 5..4 plane, 6 see-through, 11..8 gate (bit 8
 *                 active: 3 blocks others, 5 the Dog, 7 Boy and Dog), 13 always walkable; passability
 *                 relative to a plane is $909DE8 (maps/collision.ts passability)
 *   grass         a cell is cuttable while its metatile id is a source of the room's swap table (blob
 *                 section 4: len:2, count:1, records [steps:1 source:2 ... 0000]); cutting stamps another
 *                 id into the grid ($90A6EF), so a cut cell drops out by itself
 *   triggers      step-on: length $1062, 24-bit table pointer $1064; B: length $1067, pointer $1069; records
 *                 [y1 x1 y2 x2 script:2] in cells plus the origin $0F86/$0F88, far edge exclusive
 *   objects       ROM blob $9F:FDE7 + 4*room, walked by its length fields (maps/blob-layout.ts): Section 3,
 *                 per object its states [hold x y target:2], target [w h] in the object area
 *   characters    lists $3DDF awake, $3DE1 asleep, linked through +$5E; Boy $4E89, Dog $4F37; +$10 flags
 *                 ($0002 friendly, $0004 party side, $0100 awake), +$16 animation mode ($C000 charging:
 *                 a blocked move deals contact damage, $8FB52C), +$18 plane, +$1A/+$1C position,
 *                 +$60 character record in bank $8E (the Boy's and Dog's: their stats blocks $0A26/$0A70, which
 *                 bank $8E below $2000 mirrors from WRAM), whose +$0D is the body radius: two bodies touch when
 *                 |dx| < r1+r2 and 2|dy| < r1+r2 ($8F:B4AB), so a body is 2r wide and r high
 *   strikes       animation command $47 [dx:s8 dy:s8 w h] ($9087BA): a w x h box centred at the attacker's
 *                 position + (dx, dy), live for the tick that runs it. A tick runs the commands from the
 *                 resume pointer (+$00, bank +$02) through the first with bit 7 set; a hold saves the
 *                 resume pointer after itself, the end of the frame (timer +$05 run out) after the last
 *                 command (docs/entities/animation_script.md "The machine"). Widths: maps/animation-opcodes.ts
 *   camera        BG2's drawn scroll (10-bit register, high bits from the shadows $0112/$0114); characters
 *                 one line lower (the camera is BG2's vertical scroll + 1); the picture shows the
 *                 characters of the previous frame, so they are drawn from the snapshot taken then
 *
 * Differences from the editor: the wall tint follows the plane the Boy stands on (the editor tints the
 * room's main plane); grass is drawn over the walls instead of cut out of them; a character is a box of its
 * body, not a spawn cell.
 *
 * Map layers lie under the sprites: a pixel whose colour is that of a sprite pixel covering it (OAM, VRAM,
 * CGRAM) is left alone. Bodies, strikes and labels lie on top.
 *
 * Presentation only: reads WRAM, the ROM and the PPU, writes no guest state. Field frames only.
 * Keys (while the mod is on): 1..8 toggle a layer, 0 the whole overlay. Pad: Start + Select the overlay,
 * Start + L presets. Test overrides: SOE_DEBUG_OVERLAY=0|1, SOE_DEBUG_LAYERS=<comma list of collision,
 * specials, triggers, objects, characters, labels, grass, strikes> (replaces the options).
 */
#include "soe_debug_overlay.h"
#include "soe_widescreen.h"
#include "snes_osd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "desktop/sdl_compat.h"
#include "mod_runtime.h"
#include "snes/ppu.h"

#define kModPackage "soe.debug.overlay"
#define kModFeature "overlay"

enum {
    kLines = 224,
    kBank7F = 0x10000,
    kRoomId = 0x0ADB, kCellsW = 0x08EE, kCellsH = 0x08F0,
    kOriginX = 0x0F86, kOriginY = 0x0F88,
    kStepLen = 0x1062, kStepPtr = 0x1064, kBLen = 0x1067, kBPtr = 0x1069,
    kScrollBg2H = 0x0112, kScrollBg2V = 0x0114,
    kAwakeHead = 0x3DDF, kAsleepHead = 0x3DE1, kSlotFirst = 0x3DE5, kSlotEnd = 0x4FE5,
    kBoy = 0x4E89, kDog = 0x4F37,
    kObjectState = 0x10CE,
    kHeld = 0x0EC6, kStart = 0x1000, kSelect = 0x2000, kL = 0x0020,
    kRoomTable = 0x9FFDE7,
    kMaxRects = 512, kMaxChars = 48, kMaxGrass = 128, kMaxLabels = 128, kMaxStrikes = 16,
    kStrikeLife = 12,
};

enum OverlayLayer {
    kCollision = 1, kSpecials = 2, kTriggers = 4, kObjects = 8, kCharacters = 16, kLabels = 32, kGrass = 64,
    kStrikes = 128, kAllLayers = 255,
};
enum { kLayerCount = 8 };
static const char *const kLayerNames[kLayerCount] = {
    "collision", "specials", "triggers", "objects", "characters", "labels", "grass", "strikes",
};
static const char *const kLayerTitles[kLayerCount] = {
    "collision", "special cells", "triggers", "map objects", "characters", "labels", "cuttable grass", "strikes",
};

typedef struct { uint8_t r, g, b; } Rgb;
/* maps/overlay-features.ts PLANE_COLORS */
static const Rgb kPlane[4] = { { 0, 170, 255 }, { 235, 25, 25 }, { 0, 255, 170 }, { 190, 90, 255 } };

static struct { int on, layers; } s_mod;
static int s_hotkey_enabled = 1;
static int s_hotkey_layers = -1;
static uint16_t s_prev_chord = 0;
static unsigned s_prev_keys = 0;

/* ---- options ---------------------------------------------------------------------------------- */

static int OptionOn(const char *id, int dflt)
{
    char v[16];
    if (!snes_mod_runtime_feature_option_value_c(kModPackage, kModFeature, id, v, sizeof v)) return dflt;
    return !strcmp(v, "true") || !strcmp(v, "1");
}

static void ModReset(void) {
    memset(&s_mod, 0, sizeof s_mod);
    s_hotkey_enabled = 1;
    s_hotkey_layers = -1;
    s_prev_chord = 0;
    s_prev_keys = 0;
}

static void ModActivate(void)
{
    s_mod.on = 1;
    s_mod.layers = 0;
    for (int i = 0; i < kLayerCount; i++)
        if (OptionOn(kLayerNames[i], 1)) s_mod.layers |= 1 << i;
}

SNES_MOD_CONSTRUCTOR(soe_register_debug_overlay_plugin)
{
    (void)snes_mod_register_reset_callback(ModReset);
    (void)snes_mod_register_presentation_plugin("soe.debug.overlay", ModActivate);
}

/* The mod is on: by its switch, the launcher or the plugin's activation. The hotkeys aside. */
static int Enabled(void)
{
    const char *on = getenv("SOE_DEBUG_OVERLAY");
    if (on && *on && atoi(on) == 0) return 0;
    return (on && *on) || s_mod.on || snes_mod_runtime_feature_enabled_c(kModPackage, kModFeature);
}

/* The layers the switches and options ask for, before any hotkey. */
static int ConfiguredLayers(void)
{
    const char *list = getenv("SOE_DEBUG_LAYERS");
    if (list) {
        int layers = 0;
        for (int i = 0; i < kLayerCount; i++)
            if (strstr(list, kLayerNames[i])) layers |= 1 << i;
        return layers;
    }
    if (snes_mod_runtime_feature_enabled_c(kModPackage, kModFeature)) {
        int layers = 0;
        for (int i = 0; i < kLayerCount; i++)
            if (OptionOn(kLayerNames[i], 1)) layers |= 1 << i;
        return layers;
    }
    return s_mod.on ? s_mod.layers : kAllLayers;
}

static int Layers(void)
{
    if (!s_hotkey_enabled || !Enabled()) return 0;
    return s_hotkey_layers >= 0 ? s_hotkey_layers : ConfiguredLayers();
}

/* ---- memory ----------------------------------------------------------------------------------- */

static uint16_t W(uint32_t a) { return (uint16_t)(g_ram[a] | g_ram[a + 1] << 8); }
static uint8_t R8(uint32_t a) { return g_rom[(a & 0x3F0000) | (a & 0xFFFF)]; }
static uint16_t R16(uint32_t a) { return (uint16_t)(R8(a) | R8(a + 1) << 8); }
static uint32_t R24(uint32_t a) { return R8(a) | (uint32_t)R8(a + 1) << 8 | (uint32_t)R8(a + 2) << 16; }
/* A byte of bank $8E as the CPU sees it: below $2000 the WRAM mirror, from $8000 the ROM. */
static uint8_t Bank8E(uint16_t a) { return a < 0x2000 ? g_ram[a] : R8(0x8E0000u | a); }

/* The register is 10 bits; the shadow supplies the rest (soe_widescreen.c DrawnScroll). */
static int DrawnScroll(uint16_t reg, uint16_t shadow)
{
    return (int16_t)shadow + (int)((reg - shadow + 512) & 1023) - 512;
}

static int IsFieldFrame(void)
{
    return g_ppu && !(g_ppu->inidisp & 0x80) && g_ppu->bgmode == 0x39 && g_ppu->bgXsc[0] == 0x00 &&
           g_ppu->bgXsc[1] == 0x04 && g_ram[kCellsW] != 0 && W(kCellsH) != 0;
}

/* The metatile id of a cell (its stamp's offset), or -1 outside the room. */
static int CellStamp(int cx, int cy)
{
    const int w = g_ram[kCellsW], h = W(kCellsH);
    if (cx < 0 || cy < 0 || cx >= w || cy >= h) return -1;
    return W(kBank7F + 2u * (uint32_t)(cy * w + cx));
}

/* The collision word of a cell, or -1 outside the room. */
static int CellWord(int cx, int cy)
{
    const int stamp = CellStamp(cx, cy);
    return stamp < 0 ? -1 : W(kBank7F + (uint16_t)(stamp + 4));
}

/* maps/collision.ts geometryMask: is pixel (px, py) of a cell with this geometry solid? */
static int GeometrySolid(int code, int px, int py)
{
    switch (code) {
    case 0x0f: return 1;
    case 0x02: case 0x06: return py >= px;
    case 0x01: case 0x05: return px + py >= 15;
    case 0x0a: case 0x0e: return px + py <= 15;
    case 0x09: case 0x0d: return py <= px;
    case 0x03: case 0x04: return py >= 8;
    case 0x0c: case 0x0b: return py < 8;
    case 0x08: return px >= 8;
    case 0x07: return px < 8;
    default: return 0;
    }
}

/* maps/collision.ts passability for the Boy: the geometry a cell has seen from `plane`. */
static int Passability(int cw, int plane)
{
    const int bits = (plane & 3) << 4;
    if ((cw & 0x0100) && ((cw >> 8) & 0xf) == 7) return 0x0f;
    if (cw & 0x0040) return ((cw ^ bits) & 0x30) ? 0 : cw & 0x0f;
    const int diff = (cw - bits) & 0xffff;
    if (!(diff & 0x30)) return (cw & 0x2000) ? 0 : cw & 0x0f;
    if (!(diff & 0x20)) return (cw & 0x2000) ? 0 : 0x0f;
    return 0x0f;
}

/* ---- drawing ---------------------------------------------------------------------------------- */

/* The field: frame_w x 224 pixels 0xAARRGGBB; screen x 0 is column `extra`. This frame's layers; `under`
 * keeps the drawing off the sprite pixels in s_sprite. */
static struct { uint32_t *px; int w, extra, ws_left, ws_right, layers, under; } s_out;
static uint8_t s_sprite[kPpuBufWidth * kLines];

static void Blend(int sx, int sy, Rgb c, int alpha256)
{
    const int x = sx + s_out.extra;
    if (x < 0 || x >= s_out.w || sy < 0 || sy >= kLines) return;
    if (x < s_out.extra - s_out.ws_left || x >= s_out.extra + 256 + s_out.ws_right) return;
    if (s_out.under && s_sprite[sy * s_out.w + x]) return;
    uint32_t *p = &s_out.px[(size_t)sy * s_out.w + x];
    const uint32_t d = *p;
    const int r = (int)(d >> 16 & 255), g = (int)(d >> 8 & 255), b = (int)(d & 255);
    *p = 0xFF000000u | (uint32_t)(r + ((c.r - r) * alpha256 >> 8)) << 16 |
         (uint32_t)(g + ((c.g - g) * alpha256 >> 8)) << 8 | (uint32_t)(b + ((c.b - b) * alpha256 >> 8));
}

/* A box with a 1-pixel edge, [x1, x2) x [y1, y2) on screen. */
static void Box(int x1, int y1, int x2, int y2, Rgb c, int edge, int fill, int dashed)
{
    for (int y = y1; y < y2; y++)
        for (int x = x1; x < x2; x++) {
            const int border = y == y1 || y == y2 - 1 || x == x1 || x == x2 - 1;
            if (border && dashed && ((x + y) / 2) % 2) continue;
            Blend(x, y, c, border ? edge : fill);
        }
}

/* ---- sprites ---------------------------------------------------------------------------------- */

/* OAM as the PPU had it: now, and at the end of the previous frame (the picture may show either). */
typedef struct { uint16_t oam[0x100]; uint8_t high[0x20]; uint8_t obsel; } OamCopy;
static OamCopy s_oam_prev;

/* Marks each pixel where the frame shows the colour of a sprite pixel that covers it. */
static void MarkSprites(const OamCopy *o)
{
    static const uint8_t kSizes[8][2] = { { 8, 16 }, { 8, 32 }, { 8, 64 }, { 16, 32 },
                                          { 16, 64 }, { 32, 64 }, { 16, 32 }, { 16, 32 } };
    const uint8_t *bright = g_ppu->brightnessMult;
    const uint32_t adr1 = (uint32_t)(o->obsel & 7) << 13, adr2 = adr1 + ((uint32_t)((o->obsel & 0x18) + 8) << 9);
    for (int i = 0; i < 128; i++) {
        const uint16_t w0 = o->oam[2 * i], w1 = o->oam[2 * i + 1];
        const uint8_t hi = (uint8_t)(o->high[i >> 2] >> ((i & 3) * 2));
        const int size = kSizes[o->obsel >> 5][(hi >> 1) & 1];
        int x = (w0 & 0xff) | (hi & 1) << 8;
        if (x >= 256) x -= 512;
        const uint32_t base = (w1 & 0x100) ? adr2 : adr1;
        const int palette = 0x80 + 16 * ((w1 >> 9) & 7);
        for (int row = 0; row < size; row++) {
            const int sy = ((w0 >> 8) + row) & 0xff;
            if (sy >= kLines) continue;
            const int r = (w1 & 0x8000) ? size - 1 - row : row;
            for (int col = 0; col < size; col++) {
                const int fx = x + col + s_out.extra;
                if (fx < 0 || fx >= s_out.w) continue;
                const int c = (w1 & 0x4000) ? size - 1 - col : col;
                const int tile = ((((w1 & 0xff) >> 4) + (r >> 3)) << 4) | (((w1 & 0xf) + (c >> 3)) & 0xf);
                const uint32_t a = base + (uint32_t)tile * 16 + (uint32_t)(r & 7);
                const uint16_t lo = g_ppu->vram[a & 0x7fff], hi = g_ppu->vram[(a + 8) & 0x7fff];
                const int bit = 7 - (c & 7);
                const int index = (lo >> bit & 1) | (lo >> (bit + 8) & 1) << 1 | (hi >> bit & 1) << 2 |
                                  (hi >> (bit + 8) & 1) << 3;
                if (!index) continue;
                const uint16_t colour = g_ppu->cgram[palette + index];
                const uint32_t rgb = (uint32_t)bright[colour & 31] << 16 | (uint32_t)bright[(colour >> 5) & 31] << 8 |
                                     bright[(colour >> 10) & 31];
                if ((s_out.px[(size_t)sy * s_out.w + fx] & 0xFFFFFF) == rgb) s_sprite[sy * s_out.w + fx] = 1;
            }
        }
    }
}

static void SpriteMask(void)
{
    OamCopy now;
    memcpy(now.oam, g_ppu->oam, sizeof now.oam);
    memcpy(now.high, g_ppu->highOam, sizeof now.high);
    now.obsel = g_ppu->obsel;
    memset(s_sprite, 0, (size_t)s_out.w * kLines);
    MarkSprites(&now);
    MarkSprites(&s_oam_prev);
    s_oam_prev = now;
}

/* ---- labels ----------------------------------------------------------------------------------- */

/* Our own 5x7 hex digits, one byte per row, bit 4 the leftmost pixel. */
static const uint8_t kFont5x7[16][7] = {
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
};

/* Labels are queued and drawn last, over everything, so no layer covers another's text. */
static struct { int count; struct { int16_t x, y; char text[8]; Rgb ink; } l[kMaxLabels]; } s_labels;

/* 2 px in from the top-left (or 9 up from the bottom) of a box; nothing for a box too small to point at. */
static void Label(int x1, int y1, int x2, int y2, const char *text, Rgb c, int bottom)
{
    if (!(s_out.layers & kLabels) || x2 - x1 < 6 || y2 - y1 < 6 || s_labels.count >= kMaxLabels) return;
    s_labels.l[s_labels.count].x = (int16_t)(x1 + 2);
    s_labels.l[s_labels.count].y = (int16_t)(bottom ? y2 - 9 : y1 + 2);
    snprintf(s_labels.l[s_labels.count].text, sizeof s_labels.l[0].text, "%s", text);
    s_labels.l[s_labels.count].ink = c;
    s_labels.count++;
}

static int GlyphInk(const char *text, int px, int py)
{
    if (py < 0 || py >= 7 || px < 0) return 0;
    const int i = px / 6, col = px % 6;
    if (i >= (int)strlen(text) || col >= 5) return 0;
    const char ch = text[i];
    const int g = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
    return g >= 0 && (kFont5x7[g][py] >> (4 - col) & 1);
}

/* Each glyph pixel in its ink, ringed by a 1-pixel black outline. */
static void DrawLabels(void)
{
    const Rgb black = { 0, 0, 0 };
    for (int k = 0; k < s_labels.count; k++) {
        const int w = 6 * (int)strlen(s_labels.l[k].text);
        for (int py = -1; py <= 7; py++)
            for (int px = -1; px <= w; px++) {
                int ink = GlyphInk(s_labels.l[k].text, px, py), edge = 0;
                for (int dy = -1; dy <= 1 && !ink && !edge; dy++)
                    for (int dx = -1; dx <= 1; dx++) edge |= GlyphInk(s_labels.l[k].text, px + dx, py + dy);
                if (ink || edge) Blend(s_labels.l[k].x + px, s_labels.l[k].y + py, ink ? s_labels.l[k].ink : black, ink ? 256 : 220);
            }
    }
    s_labels.count = 0;
}

static void Number(char *out, unsigned v, unsigned base)
{
    char tmp[8];
    int n = 0;
    do { tmp[n++] = "0123456789ABCDEF"[v % base]; v /= base; } while (v && n < 7);
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

/* ---- collision -------------------------------------------------------------------------------- */

/* Per screen pixel: 0 open, 1 solid, 2 outside the room (never an edge's other side). */
static uint8_t s_mask[kPpuBufWidth * kLines], s_edge[kPpuBufWidth * kLines], s_line[kPpuBufWidth * kLines];

/* s_edge: the pixels of s_mask == 1 that touch a 0 (8 neighbours), grown by `grow` pixels on every side. */
static void Contour(int grow)
{
    const int w = s_out.w;
    memset(s_line, 0, (size_t)w * kLines);
    for (int y = 0; y < kLines; y++)
        for (int x = 0; x < w; x++) {
            if (s_mask[y * w + x] != 1) continue;
            for (int dy = -1; dy <= 1 && !s_line[y * w + x]; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < w && ny < kLines && s_mask[ny * w + nx] == 0) {
                        s_line[y * w + x] = 1;
                        break;
                    }
                }
        }
    memset(s_edge, 0, (size_t)w * kLines);
    for (int y = 0; y < kLines; y++)
        for (int x = 0; x < w; x++) {
            if (!s_line[y * w + x]) continue;
            for (int dy = -grow; dy <= grow; dy++)
                for (int dx = -grow; dx <= grow; dx++) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < w && ny < kLines) s_edge[ny * w + nx] = 1;
                }
        }
}

static void Collision(int cam_x, int cam_y, int boy_plane)
{
    const int w = s_out.w, x_min = -s_out.extra;
    static uint8_t plane_line[kPpuBufWidth * kLines];
    int present = 0;
    for (int sy = 0; sy < kLines; sy += 8)
        for (int sx = x_min; sx < x_min + w; sx += 8) {
            const int cw = CellWord((sx + cam_x) >> 4, (sy + cam_y) >> 4);
            if (cw >= 0) present |= 1 << ((cw >> 4) & 3);
        }
    memset(plane_line, 0, (size_t)w * kLines);
    for (int p = 0; p < 4; p++) {
        if (!(present & (1 << p)) && p != boy_plane) continue;
        for (int sy = 0; sy < kLines; sy++)
            for (int sx = 0; sx < w; sx++) {
                const int wx = sx + x_min + cam_x, wy = sy + cam_y;
                const int cw = CellWord(wx >> 4, wy >> 4);
                s_mask[sy * w + sx] = cw < 0 ? 2 : GeometrySolid(Passability(cw, p), wx & 15, wy & 15);
            }
        Contour(1);
        /* The Boy's plane: walls tinted where no line of it lies (overlay-shapes.ts drawContours). */
        const Rgb wall = { 220, 20, 20 };
        for (int y = 0; y < kLines; y++)
            for (int x = 0; x < w; x++) {
                if (s_edge[y * w + x] && (present & (1 << p))) plane_line[y * w + x] = (uint8_t)(p + 1);
                else if (!s_edge[y * w + x] && p == boy_plane && s_mask[y * w + x] == 1) Blend(x + x_min, y, wall, 51);
            }
    }
    for (int y = 0; y < kLines; y++)
        for (int x = 0; x < w; x++)
            if (plane_line[y * w + x]) Blend(x + x_min, y, kPlane[plane_line[y * w + x] - 1], 256);
}

/* Drift, see-through and gates, cell by cell (collision-overlay.ts passes 3..6). */
static void Specials(int cam_x, int cam_y)
{
    const Rgb drift = { 0, 188, 212 }, through = { 156, 39, 176 }, gate = { 235, 235, 235 };
    const int x_min = -s_out.extra, x_max = x_min + s_out.w;
    for (int cy = (cam_y >> 4) - 1; cy <= (cam_y + kLines) >> 4; cy++)
        for (int cx = ((x_min + cam_x) >> 4) - 1; cx <= (x_max + cam_x) >> 4; cx++) {
            const int cw = CellWord(cx, cy);
            if (cw < 0) continue;
            const int x = cx * 16 - cam_x, y = cy * 16 - cam_y;
            if (cw & 0x2000) Box(x, y, x + 16, y + 16, drift, 87, 87, 0);
            if (cw & 0x0040) Box(x, y, x + 16, y + 16, through, 77, 77, 0);
            const int g = (cw >> 8) & 0xf;
            if ((cw & 0x0100) && (g == 3 || g == 5 || g == 7)) Box(x, y, x + 16, y + 16, gate, 243, 0, 0);
        }
}

/* ---- triggers, objects, grass ----------------------------------------------------------------- */

static void Triggers(int cam_x, int cam_y)
{
    static const struct { uint16_t len, ptr; Rgb stroke, ink; } kKinds[2] = {
        { kBLen, kBPtr, { 255, 255, 0 }, { 255, 255, 140 } },        /* B-trigger */
        { kStepLen, kStepPtr, { 255, 0, 255 }, { 255, 160, 255 } },   /* step-on */
    };
    const int ox = W(kOriginX), oy = W(kOriginY);
    for (int k = 0; k < 2; k++) {
        const unsigned len = W(kKinds[k].len);
        const uint32_t table = W(kKinds[k].ptr) | (uint32_t)g_ram[kKinds[k].ptr + 2] << 16;
        if (!table || len > 6 * 255) continue;
        for (unsigned i = 0; i + 6 <= len; i += 6) {
            const uint32_t r = table + i;
            const int x1 = (R8(r + 1) - ox) * 16 - cam_x, y1 = (R8(r) - oy) * 16 - cam_y;
            const int x2 = (R8(r + 3) - ox) * 16 - cam_x, y2 = (R8(r + 2) - oy) * 16 - cam_y;
            Box(x1, y1, x2, y2, kKinds[k].stroke, 256, 85, 0);
            char text[8];
            Number(text, R16(r + 4), 16);
            Label(x1, y1, x2, y2, text, kKinds[k].ink, 0);
        }
    }
}

/* Every state of every Section 3 object of the room, from the ROM (maps/room.ts readObjects), and the
 * sources of its grass swap table (maps/cuttable-grass.ts parseGrassSwapSection). */
static struct {
    int room;
    uint32_t blob;
    int count, grass_count;
    struct { uint8_t x, y, w, h, index, state; } r[kMaxRects];
    uint16_t grass[kMaxGrass];
} s_objects = { -1 };

static void LoadRoom(int room)
{
    const uint32_t blob = R24(kRoomTable + 4u * (uint32_t)room);
    if (room == s_objects.room && blob == s_objects.blob) return;
    s_objects.room = room;
    s_objects.blob = blob;
    s_objects.count = s_objects.grass_count = 0;
    if (!blob) return;
    const uint32_t b_len = blob + 0x0f + R16(blob + 0x0d);
    const uint32_t payload = b_len + 2 + R16(b_len);
    const uint32_t extras = payload + 1 + R8(payload) * 2u;
    const uint32_t block1 = extras + 1 + R8(extras) * 3u;
    const uint32_t section2 = block1 + 2 + R16(block1);
    const uint32_t section3 = section2 + 3 + R16(section2 + 1);
    const int objects = R8(section3);
    const uint32_t block2 = section3 + 1 + objects * 2u;
    const uint32_t section4 = block2 + 2 + R16(block2);
    const uint32_t block3 = section4 + 2 + R16(section4);
    const uint32_t area = block3 + 2 + R16(block3);
    for (int i = 0; i < objects; i++) {
        const uint32_t rec = area + R16(section3 + 1 + 2u * (uint32_t)i);
        for (int s = 0; s < R8(rec) && s_objects.count < kMaxRects; s++) {
            const uint32_t st = rec + 1 + 5u * (uint32_t)s, target = area + R16(st + 3);
            s_objects.r[s_objects.count].x = R8(st + 1);
            s_objects.r[s_objects.count].y = R8(st + 2);
            s_objects.r[s_objects.count].w = R8(target) ? R8(target) : 1;
            s_objects.r[s_objects.count].h = R8(target + 1) ? R8(target + 1) : 1;
            s_objects.r[s_objects.count].index = (uint8_t)i;
            s_objects.r[s_objects.count].state = (uint8_t)s;
            s_objects.count++;
        }
    }
    /* Records [steps:1][source:2][step:2 ...][0000:2] until the section's length is used up. */
    const uint32_t end = section4 + 2 + R16(section4);
    for (uint32_t p = section4 + 3; p + 3 <= end && s_objects.grass_count < kMaxGrass;) {
        const uint16_t source = R16(p + 1);
        if (!source) break;
        s_objects.grass[s_objects.grass_count++] = source;
        p += 3;
        while (p + 2 <= end && R16(p)) p += 2;
        p += 2;
    }
}

static void Objects(int cam_x, int cam_y)
{
    const Rgb blue = { 33, 150, 243 }, ink = { 150, 210, 255 };
    for (int i = 0; i < s_objects.count; i++) {
        const int obj_idx = s_objects.r[i].index;
        const int is_active = s_objects.r[i].state == g_ram[kObjectState + obj_idx];
        const int x1 = s_objects.r[i].x * 16 - cam_x, y1 = s_objects.r[i].y * 16 - cam_y;
        const int x2 = x1 + s_objects.r[i].w * 16, y2 = y1 + s_objects.r[i].h * 16;
        Box(x1, y1, x2, y2, blue, is_active ? 256 : 140, is_active ? 82 : 25, !is_active);
        char text[8];
        Number(text, obj_idx, 10);
        Label(x1, y1, x2, y2, text, ink, 1);
    }
}

static int IsGrass(int stamp)
{
    for (int i = 0; i < s_objects.grass_count; i++)
        if (s_objects.grass[i] == stamp) return 1;
    return 0;
}

/* Cuttable cells: a soft fill and a 5-pixel contour around each patch, over the walls
 * (overlay-shapes.ts drawGrass, with a fatter line). */
static void Grass(int cam_x, int cam_y)
{
    if (!s_objects.grass_count) return;
    const int w = s_out.w, x_min = -s_out.extra;
    int any = 0;
    for (int sy = 0; sy < kLines; sy++)
        for (int sx = 0; sx < w; sx++) {
            const int wx = sx + x_min + cam_x, wy = sy + cam_y;
            const int stamp = CellStamp(wx >> 4, wy >> 4);
            const int grass = stamp >= 0 && IsGrass(stamp);
            s_mask[sy * w + sx] = (uint8_t)grass;
            any |= grass;
        }
    if (!any) return;
    Contour(2);
    const Rgb green = { 76, 175, 80 };
    for (int y = 0; y < kLines; y++)
        for (int x = 0; x < w; x++) {
            if (s_edge[y * w + x]) Blend(x + x_min, y, green, 256);
            else if (s_mask[y * w + x]) Blend(x + x_min, y, green, 72);
        }
}

/* ---- characters and strikes ------------------------------------------------------------------- */

typedef struct {
    int16_t x, y;
    uint8_t radius, friendly, awake, charging, timer;
    uint16_t slot, restart;
    uint32_t anim;
} Character;
static struct { int count; Character c[kMaxChars]; } s_now, s_drawn;
/* Strike boxes in room pixels, fading out over kStrikeLife frames. */
static struct { int count; struct { int16_t x, y; uint8_t w, h, life; } s[kMaxStrikes]; } s_strikes;

/* Bytes of the animation command at p, or 0 when its width is unknown (maps/animation-opcodes.ts). */
static int CommandLength(uint32_t p)
{
    static const uint8_t kLen[0x65] = {
        [0x00] = 1, [0x1f] = 3, [0x20] = 2, [0x21] = 1, [0x2c] = 4, [0x2d] = 1, [0x2e] = 2, [0x2f] = 2,
        [0x30] = 1, [0x31] = 3, [0x32] = 4, [0x33] = 5, [0x34] = 6, [0x35] = 3, [0x36] = 4, [0x37] = 2,
        [0x38] = 2, [0x39] = 4, [0x3a] = 4, [0x3b] = 3, [0x3c] = 9, [0x3d] = 5, [0x3e] = 9, [0x3f] = 3,
        [0x40] = 3, [0x41] = 1, [0x42] = 2, [0x43] = 1, [0x44] = 1, [0x45] = 3, [0x46] = 3, [0x47] = 5,
        [0x48] = 1, [0x49] = 3, [0x4a] = 3, [0x4b] = 3, [0x4c] = 6, [0x4d] = 3, [0x4e] = 1, [0x4f] = 1,
        [0x50] = 5, [0x51] = 1, [0x52] = 1, [0x53] = 1, [0x54] = 3, [0x55] = 1, [0x56] = 3, [0x58] = 1,
        [0x59] = 7, [0x5a] = 2, [0x5b] = 1, [0x5c] = 1, [0x5d] = 1, [0x5e] = 25, [0x5f] = 4, [0x60] = 49,
        [0x61] = 13, [0x62] = 28, [0x63] = 13, [0x64] = 52,
    };
    const int c = R8(p) & 0x7f;
    if (c >= 0x01 && c <= 0x1e) return 1;     /* hold */
    if (c >= 0x22 && c <= 0x2b) return 3;     /* sprite */
    if (c == 0x57) {                          /* segments: a count, then [count sprite:3] groups */
        const int total = R8(p + 1);
        int assigned = 0, groups = 0;
        uint32_t q = p + 2;
        while (assigned < total && groups++ < 64) {
            const int n = R8(q);
            assigned += n ? n : total;
            q += 4;
        }
        return (int)(q - p);
    }
    return c < 0x65 ? kLen[c] : 0;
}

/* Runs one tick's commands from `p` the way the animation machine reads them, both ways at a branch. True
 * for a path that ends where `now` says the tick ended: its resume pointer after the last command or after a
 * hold, or unchanged with the frame timer counted down; *strike is the path's strike command, or 0. */
static int WalkTick(uint32_t p, const Character *was, const Character *now, uint32_t hold, int steps,
                    uint32_t *strike)
{
    const uint32_t bank = p & 0xFF0000;
    for (; steps < 64; steps++) {
        const uint8_t raw = R8(p);
        const int c = raw & 0x7f, len = CommandLength(p);
        if (!len) return 0;
        uint32_t next = p + (uint32_t)len;
        if (c == 0x47) *strike = p;
        if ((c >= 0x01 && c <= 0x1e) || c == 0x1f || c == 0x20) hold = next;
        if (c == 0x3b) next = bank | R16(p + 1);
        if (c == 0x2d) next = bank | now->restart;
        if ((c == 0x39 || c == 0x3a || c == 0x54) && !(raw & 0x80)) {
            uint32_t taken = *strike;
            if (WalkTick(bank | R16(p + (c == 0x54 ? 1 : 2)), was, now, hold, steps + 1, &taken)) {
                *strike = taken;
                return 1;
            }
        }
        if (raw & 0x80) {
            const uint16_t resume = (uint16_t)now->anim;
            return resume == (uint16_t)next || (hold && resume == (uint16_t)hold) ||
                   (now->anim == was->anim && now->timer + 1 == was->timer);
        }
        p = next;
    }
    return 0;
}

/* The strike a character's last tick ran, if any: its command's address, else 0. The tick starts at the
 * resume pointer of the frame before, or at the start of an animation begun this frame. */
static uint32_t TickStrike(const Character *was, const Character *now)
{
    if (was->anim == now->anim && was->timer == now->timer) return 0;   /* no tick this frame */
    const uint32_t starts[2] = { was->anim, (now->anim & 0xFF0000) | now->restart };
    for (int i = 0; i < 2; i++) {
        uint32_t strike = 0;
        if (WalkTick(starts[i], was, now, 0, 0, &strike)) return strike;
    }
    return 0;
}

static void AddStrike(const Character *c, uint32_t at)
{
    if (s_strikes.count >= kMaxStrikes) memmove(&s_strikes.s[0], &s_strikes.s[1], sizeof s_strikes.s[0] * --s_strikes.count);
    const int w = R8(at + 3), h = R8(at + 4);
    s_strikes.s[s_strikes.count].x = (int16_t)(c->x + (int8_t)R8(at + 1) - w / 2);
    s_strikes.s[s_strikes.count].y = (int16_t)(c->y + (int8_t)R8(at + 2) - h / 2);
    s_strikes.s[s_strikes.count].w = (uint8_t)w;
    s_strikes.s[s_strikes.count].h = (uint8_t)h;
    s_strikes.s[s_strikes.count].life = kStrikeLife;
    s_strikes.count++;
}

static void Snapshot(void)
{
    s_now.count = 0;
    for (int list = 0; list < 3; list++) {
        uint16_t e = list < 2 ? W(list ? kAsleepHead : kAwakeHead) : 0;
        for (int guard = 0; guard < kMaxChars; guard++) {
            if (list == 2) e = guard == 0 ? kBoy : guard == 1 ? kDog : 0;
            if (e < kSlotFirst || e >= kSlotEnd || s_now.count >= kMaxChars) break;
            int dup = 0;
            for (int i = 0; i < s_now.count; i++) dup |= s_now.c[i].slot == e;
            if (!dup) {
                const uint16_t flags = W(e + 0x10), record = W(e + 0x60);
                Character *c = &s_now.c[s_now.count++];
                c->x = (int16_t)W(e + 0x1A);
                c->y = (int16_t)W(e + 0x1C);
                c->radius = record ? Bank8E((uint16_t)(record + 0x0D)) : 0;
                c->friendly = e == kBoy || e == kDog || (flags & 0x0006);
                c->awake = (flags & 0x0100) != 0;
                c->charging = (W(e + 0x16) & 0xC000) != 0;
                c->anim = W(e) | (uint32_t)g_ram[e + 2] << 16;
                c->restart = W(e + 3);
                c->timer = g_ram[e + 5];
                c->slot = e;
                for (int i = 0; i < s_drawn.count; i++)
                    if (s_drawn.c[i].slot == e) {
                        const uint32_t at = TickStrike(&s_drawn.c[i], c);
                        if (at) AddStrike(c, at);
                        break;
                    }
            }
            if (list < 2) e = W(e + 0x5E);
        }
    }
}

static void Characters(int cam_x, int cam_y)
{
    const Rgb hostile = { 0xff, 0x55, 0x55 }, friendly = { 0x4f, 0xc3, 0xf7 }, contact = { 0xff, 0x00, 0x00 };
    for (int i = 0; i < s_drawn.count; i++) {
        const Character *c = &s_drawn.c[i];
        const int r = c->radius ? c->radius : 4, x = c->x - cam_x, y = c->y - cam_y;
        const Rgb ink = c->charging ? contact : c->friendly ? friendly : hostile;
        Box(x - r, y - r / 2, x + r, y + r / 2 + 1, ink, c->charging ? 256 : 230, c->awake ? 51 : 26, !c->awake);
        if (c->charging) Box(x - r - 1, y - r / 2 - 1, x + r + 1, y + r / 2 + 2, contact, 256, 0, 0);
    }
}

/* sprites-motion.js drawTrail: the strike box, fading out. */
static void Strikes(int cam_x, int cam_y)
{
    const Rgb red = { 255, 51, 85 };
    int kept = 0;
    for (int i = 0; i < s_strikes.count; i++) {
        const int a = 256 * s_strikes.s[i].life / kStrikeLife;
        const int x = s_strikes.s[i].x - cam_x, y = s_strikes.s[i].y - cam_y;
        Box(x, y, x + s_strikes.s[i].w, y + s_strikes.s[i].h, red, a, a / 3, 0);
        if (--s_strikes.s[i].life) s_strikes.s[kept++] = s_strikes.s[i];
    }
    s_strikes.count = kept;
}

/* ---- hotkeys ---------------------------------------------------------------------------------- */

static void ToggleLayer(int i)
{
    const int base = s_hotkey_layers >= 0 ? s_hotkey_layers : ConfiguredLayers();
    s_hotkey_layers = base ^ (1 << i);
    char text[64];
    snprintf(text, sizeof text, "Debug overlay: %s %s", kLayerTitles[i], (s_hotkey_layers >> i) & 1 ? "on" : "off");
    snes_osd_push(text, 0);
}

static void ToggleOverlay(void)
{
    s_hotkey_enabled = !s_hotkey_enabled;
    snes_osd_push(s_hotkey_enabled ? "Debug overlay: on" : "Debug overlay: off", 0);
}

/* Keys 1..8 and 0, on the press; pad chords Start + Select and Start + L. */
static void Hotkeys(void)
{
    static const int kKeys[9] = { SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
                                  SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_0 };
    const uint8_t *keyboard = snesrecomp_sdl_get_keyboard_state();
    unsigned keys = 0;
    for (int i = 0; keyboard && i < 9; i++)
        if (keyboard[kKeys[i]]) keys |= 1u << i;
    const unsigned pressed = keys & ~s_prev_keys;
    s_prev_keys = keys;
    for (int i = 0; i < kLayerCount; i++)
        if (pressed & (1u << i)) ToggleLayer(i);
    if (pressed & (1u << 8)) ToggleOverlay();

    const uint16_t held = W(kHeld);
    const uint16_t chord = (held & kStart) ? (held & (kSelect | kL)) : 0;
    if (chord && chord != s_prev_chord) {
        if (chord & kSelect) {
            ToggleOverlay();
        } else if (chord & kL) {
            static const int kPresets[6] = { kAllLayers, kCollision | kGrass | kLabels, kSpecials,
                                             kTriggers | kLabels, kObjects | kLabels, kCharacters | kStrikes };
            static const char *const kPresetNames[6] = {
                "Debug layers: all", "Debug layers: collision", "Debug layers: specials",
                "Debug layers: triggers", "Debug layers: objects", "Debug layers: characters"
            };
            static int s_idx = 0;
            s_idx = (s_idx + 1) % 6;
            s_hotkey_layers = kPresets[s_idx];
            snes_osd_push(kPresetNames[s_idx], 0);
        }
    }
    s_prev_chord = chord;
}

/* ---- frame ------------------------------------------------------------------------------------ */

static int s_frame_w = 256;

void SoeDebugPrepareFrame(int frame_w)
{
    s_frame_w = frame_w;
}

void SoeDebugEndFrame(const uint8_t *field, unsigned number)
{
    (void)number;
    if (IsFieldFrame() && Enabled()) Hotkeys();
    const int layers = Layers();
    if (!field || !layers || !IsFieldFrame()) { s_drawn.count = s_now.count = s_strikes.count = 0; return; }
    s_out.px = (uint32_t *)(uintptr_t)field;
    s_out.w = s_frame_w;
    s_out.extra = (s_frame_w - 256) / 2;
    int ws_left = 0, ws_right = 0;
    SoeWsMargins(&ws_left, &ws_right);
    s_out.ws_left = ws_left;
    s_out.ws_right = ws_right;
    s_out.layers = layers;
    const int bg2_x = DrawnScroll(g_ppu->hScroll[1], W(kScrollBg2H));
    const int bg2_y = DrawnScroll(g_ppu->vScroll[1], W(kScrollBg2V));
    LoadRoom(g_ram[kRoomId]);
    SpriteMask();
    s_out.under = 1;
    if (layers & kCollision) Collision(bg2_x, bg2_y, (g_ram[kBoy + 0x18] >> 4) & 3);
    if (layers & kGrass) Grass(bg2_x, bg2_y);
    if (layers & kSpecials) Specials(bg2_x, bg2_y);
    if (layers & kObjects) Objects(bg2_x, bg2_y);
    if (layers & kTriggers) Triggers(bg2_x, bg2_y);
    s_out.under = 0;
    if (layers & kCharacters) Characters(bg2_x, bg2_y + 1);
    if (layers & kStrikes) Strikes(bg2_x, bg2_y + 1);
    else s_strikes.count = 0;
    DrawLabels();
    s_drawn = s_now;
    Snapshot();
}
