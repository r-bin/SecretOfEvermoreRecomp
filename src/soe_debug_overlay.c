/*
 * Debug overlay: the room's collision, triggers, map objects and characters drawn over the running field,
 * in the colours and shapes of everscript-vscode's map editor (maps/overlay-features.ts, overlay-shapes.ts,
 * collision-overlay.ts, font.ts; rooms/webview/svg-spawns.js). docs/mods/debug-overlay.md.
 *
 * Facts (everscript-vscode docs/map-format/room-reference.md, map_collision_mechanics.md, docs/entities/):
 *   room grid     $7F:0000, one word per 16x16 cell (2*(row*$08EE + col)): an offset into the stamp
 *                 dictionary, also in $7F; a stamp is BG1 word, BG2 word, collision word (+4)
 *                 (checked against the ROM decode of room $51: 2800 of 2800 cells)
 *   collision     bits 3..0 geometry (drift when bit 13), 5..4 plane, 6 see-through, 11..8 gate (bit 8
 *                 active: 3 blocks others, 5 the Dog, 7 Boy and Dog), 13 always walkable; passability
 *                 relative to a plane is $909DE8 (maps/collision.ts passability)
 *   triggers      step-on: length $1062, 24-bit table pointer $1064; B: length $1067, pointer $1069; records
 *                 [y1 x1 y2 x2 script:2] in cells plus the origin $0F86/$0F88, far edge exclusive
 *   objects       ROM blob $9F:FDE7 + 4*room, walked by its length fields (maps/blob-layout.ts): Section 3,
 *                 per object its states [hold x y target:2], target [w h] in the object area
 *   characters    lists $3DDF awake, $3DE1 asleep, linked through +$5E; Boy $4E89, Dog $4F37; +$10 flags
 *                 ($0002 friendly, $0004 party side, $0100 awake), +$18 plane, +$1A/+$1C position,
 *                 +$60 character record in bank $8E (the Boy's and Dog's: their stats blocks $0A26/$0A70, which
 *                 bank $8E below $2000 mirrors from WRAM), whose +$0D is the body radius: two bodies touch when
 *                 |dx| < r1+r2 and 2|dy| < r1+r2 ($8F:B4AB), so a body is 2r wide and r high
 *   camera        BG2's drawn scroll (10-bit register, high bits from the shadows $0112/$0114); characters
 *                 one line lower (the camera is BG2's vertical scroll + 1); the picture shows the
 *                 characters of the previous frame, so they are drawn from the snapshot taken then
 *
 * Differences from the editor: the wall tint follows the plane the Boy stands on (the editor tints the
 * room's main plane); cuttable grass is not drawn; a character is a box of its body, not a spawn cell.
 *
 * Presentation only: reads WRAM, the ROM and the PPU's scroll, writes no guest state. Field frames only.
 * Test overrides: SOE_DEBUG_OVERLAY=0|1, SOE_DEBUG_LAYERS=<comma list of collision, specials, triggers,
 * objects, characters, labels> (replaces the options).
 */
#include "soe_debug_overlay.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
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
    kRoomTable = 0x9FFDE7,
    kMaxRects = 512, kMaxChars = 48,
};

enum OverlayLayer { kCollision = 1, kSpecials = 2, kTriggers = 4, kObjects = 8, kCharacters = 16, kLabels = 32 };
static const char *const kLayerNames[] = { "collision", "specials", "triggers", "objects", "characters", "labels" };

typedef struct { uint8_t r, g, b; } Rgb;
/* maps/overlay-features.ts PLANE_COLORS */
static const Rgb kPlane[4] = { { 0, 170, 255 }, { 235, 25, 25 }, { 0, 255, 170 }, { 190, 90, 255 } };

static struct { int on, layers; } s_mod;

/* ---- options ---------------------------------------------------------------------------------- */

static int OptionOn(const char *id, int dflt)
{
    char v[16];
    if (!snes_mod_runtime_feature_option_value_c(kModPackage, kModFeature, id, v, sizeof v)) return dflt;
    return !strcmp(v, "true") || !strcmp(v, "1");
}

static void ModReset(void) { memset(&s_mod, 0, sizeof s_mod); }

static void ModActivate(void)
{
    s_mod.on = 1;
    s_mod.layers = 0;
    for (int i = 0; i < 6; i++)
        if (OptionOn(kLayerNames[i], 1)) s_mod.layers |= 1 << i;
}

SNES_MOD_CONSTRUCTOR(soe_register_debug_overlay_plugin)
{
    (void)snes_mod_register_reset_callback(ModReset);
    (void)snes_mod_register_presentation_plugin("soe.debug.overlay", ModActivate);
}

static int Layers(void)
{
    const char *on = getenv("SOE_DEBUG_OVERLAY");
    if (on && *on && atoi(on) == 0) return 0;
    const int enabled = (on && *on) || s_mod.on || snes_mod_runtime_feature_enabled_c(kModPackage, kModFeature);
    if (!enabled) return 0;
    const char *list = getenv("SOE_DEBUG_LAYERS");
    if (!list) return s_mod.on ? s_mod.layers : 63;
    int layers = 0;
    for (int i = 0; i < 6; i++)
        if (strstr(list, kLayerNames[i])) layers |= 1 << i;
    return layers;
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

/* The collision word of a cell, or -1 outside the room. */
static int CellWord(int cx, int cy)
{
    const int w = g_ram[kCellsW], h = W(kCellsH);
    if (cx < 0 || cy < 0 || cx >= w || cy >= h) return -1;
    const uint16_t stamp = W(kBank7F + 2u * (uint32_t)(cy * w + cx));
    return W(kBank7F + (uint16_t)(stamp + 4));
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

/* The field: frame_w x 224 pixels 0xAARRGGBB; screen x 0 is column `extra`. This frame's layers. */
static struct { uint32_t *px; int w, extra, layers; } s_out;

static void Blend(int sx, int sy, Rgb c, int alpha256)
{
    const int x = sx + s_out.extra;
    if (x < 0 || x >= s_out.w || sy < 0 || sy >= kLines) return;
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

/* maps/font.ts FONT_3X5, the characters the labels use; ink '1', row-major. */
static const char *Glyph(char ch)
{
    static const char *const digits[16] = {
        "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
        "111100111001111", "111100111101111", "111001010010010", "111101111101111", "111101111001111",
        "111101111101101", "110101110101110", "111100100100111", "110101101101110", "111100110100111",
        "111100110100100",
    };
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch >= 'A' && ch <= 'F') return digits[ch - 'A' + 10];
    return "000000000000000";
}

/* font.ts drawLabelInRect: 3 px in from the top (or 9 up from the bottom), a 1-pixel shadow. */
static void Label(int x1, int y1, int x2, int y2, const char *text, Rgb c, int bottom)
{
    if (!(s_out.layers & kLabels) || x2 - x1 < 6 || y2 - y1 < 6) return;
    const int x0 = x1 + 3, y0 = bottom ? y2 - 9 : y1 + 3;
    const Rgb black = { 0, 0, 0 };
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; text[i]; i++) {
            const char *g = Glyph(text[i]);
            for (int k = 0; k < 15; k++)
                if (g[k] == '1') Blend(x0 + i * 4 + k % 3 + !pass, y0 + k / 3 + !pass, pass ? c : black, 256);
        }
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

static void Collision(int cam_x, int cam_y, int boy_plane)
{
    const int w = s_out.w, x_min = -s_out.extra;
    int present = 0;
    for (int sy = 0; sy < kLines; sy += 8)
        for (int sx = x_min; sx < x_min + w; sx += 8) {
            const int cw = CellWord((sx + cam_x) >> 4, (sy + cam_y) >> 4);
            if (cw >= 0) present |= 1 << ((cw >> 4) & 3);
        }
    memset(s_line, 0, (size_t)w * kLines);
    for (int p = 0; p < 4; p++) {
        if (!(present & (1 << p)) && p != boy_plane) continue;
        for (int sy = 0; sy < kLines; sy++)
            for (int sx = 0; sx < w; sx++) {
                const int wx = sx + x_min + cam_x, wy = sy + cam_y;
                const int cw = CellWord(wx >> 4, wy >> 4);
                s_mask[sy * w + sx] = cw < 0 ? 2 : GeometrySolid(Passability(cw, p), wx & 15, wy & 15);
            }
        memset(s_edge, 0, (size_t)w * kLines);
        for (int y = 0; y < kLines; y++)
            for (int x = 0; x < w; x++) {
                if (s_mask[y * w + x] != 1) continue;
                for (int dy = -1; dy <= 1 && !s_edge[y * w + x]; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < kLines && s_mask[ny * w + nx] == 0) {
                            s_edge[y * w + x] = 1;
                            break;
                        }
                    }
            }
        /* The Boy's plane: walls tinted where no line of it lies (overlay-shapes.ts drawContours). */
        const Rgb wall = { 220, 20, 20 };
        for (int y = 0; y < kLines; y++)
            for (int x = 0; x < w; x++) {
                int thick = 0;
                for (int dy = -1; dy <= 1 && !thick; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < kLines && s_edge[ny * w + nx]) { thick = 1; break; }
                    }
                if (thick && (present & (1 << p))) s_line[y * w + x] = (uint8_t)(p + 1);
                else if (!thick && p == boy_plane && s_mask[y * w + x] == 1) Blend(x + x_min, y, wall, 51);
            }
    }
    for (int y = 0; y < kLines; y++)
        for (int x = 0; x < w; x++)
            if (s_line[y * w + x]) Blend(x + x_min, y, kPlane[s_line[y * w + x] - 1], 256);
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

/* ---- triggers and objects --------------------------------------------------------------------- */

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

/* Every state of every Section 3 object of the room, from the ROM (maps/room.ts readObjects). */
static struct { int room; uint32_t blob; int count; struct { uint8_t x, y, w, h, index; } r[kMaxRects]; } s_objects = { -1 };

static void LoadObjects(int room)
{
    const uint32_t blob = R24(kRoomTable + 4u * (uint32_t)room);
    if (room == s_objects.room && blob == s_objects.blob) return;
    s_objects.room = room;
    s_objects.blob = blob;
    s_objects.count = 0;
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
            s_objects.count++;
        }
    }
}

static void Objects(int cam_x, int cam_y)
{
    LoadObjects(g_ram[kRoomId]);
    const Rgb blue = { 33, 150, 243 }, ink = { 150, 210, 255 };
    for (int i = 0; i < s_objects.count; i++) {
        const int x1 = s_objects.r[i].x * 16 - cam_x, y1 = s_objects.r[i].y * 16 - cam_y;
        const int x2 = x1 + s_objects.r[i].w * 16, y2 = y1 + s_objects.r[i].h * 16;
        Box(x1, y1, x2, y2, blue, 230, 82, 0);
        char text[8];
        Number(text, s_objects.r[i].index, 10);
        Label(x1, y1, x2, y2, text, ink, 1);
    }
}

/* ---- characters ------------------------------------------------------------------------------- */

typedef struct { int16_t x, y; uint8_t radius, friendly, awake; uint16_t slot; } Character;
static struct { int count; Character c[kMaxChars]; } s_now, s_drawn;

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
                c->slot = e;
            }
            if (list < 2) e = W(e + 0x5E);
        }
    }
}

static void Characters(int cam_x, int cam_y)
{
    const Rgb hostile = { 0xff, 0x55, 0x55 }, friendly = { 0x4f, 0xc3, 0xf7 };
    for (int i = 0; i < s_drawn.count; i++) {
        const Character *c = &s_drawn.c[i];
        const int r = c->radius ? c->radius : 4, x = c->x - cam_x, y = c->y - cam_y;
        Box(x - r, y - r / 2, x + r, y + r / 2 + 1, c->friendly ? friendly : hostile, 230, c->awake ? 51 : 26, !c->awake);
    }
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
    const int layers = Layers();
    if (!field || !layers || !IsFieldFrame()) { s_drawn.count = s_now.count = 0; return; }
    s_out.px = (uint32_t *)(uintptr_t)field;
    s_out.w = s_frame_w;
    s_out.extra = (s_frame_w - 256) / 2;
    s_out.layers = layers;
    const int bg2_x = DrawnScroll(g_ppu->hScroll[1], W(kScrollBg2H));
    const int bg2_y = DrawnScroll(g_ppu->vScroll[1], W(kScrollBg2V));
    if (layers & kCollision) Collision(bg2_x, bg2_y, (g_ram[kBoy + 0x18] >> 4) & 3);
    if (layers & kSpecials) Specials(bg2_x, bg2_y);
    if (layers & kObjects) Objects(bg2_x, bg2_y);
    if (layers & kTriggers) Triggers(bg2_x, bg2_y);
    if (layers & kCharacters) Characters(bg2_x, bg2_y + 1);
    s_drawn = s_now;
    Snapshot();
}
