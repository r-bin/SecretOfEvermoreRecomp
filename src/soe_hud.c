/*
 * Adaptive HUD: the field HUD at the top, at the screen edges, or hidden (docs/mods/hud.md).
 *
 * What the game draws (measured 2026-11-10, everscript-vscode tag `hud`):
 *   - BG3, map $0800, rows 24..27 (screen lines 193..223 at the field's BG3 scroll $3FF):
 *     one box per character, columns 3..12 (Boy) and 19..28 (Dog), HP digits on the box's
 *     top edge, the charge gauge inside. Palette 2 (CGRAM 8..11), priority set, glyphs
 *     $3E0..$3FF (VRAM words $1F00..$1FFF). A box's top-left corner is tile $3EA.
 *   - two 16x16 sprites, the portraits: OBJ tile $080 (Boy, palette 6) at (8, 201) and $082
 *     (Dog, palette 7) at (232, 201), always in two adjacent OAM slots, but which ones moves
 *     with the menus (0/1 in the field, 9..12 under a ring menu).
 *   - dialogue and menu windows use BG3 rows 0..23 in palettes 0 and 1, never the HUD rows.
 *
 * How: the PPU's host-overlay capture (snesrecomp docs/HOST_OVERLAY_EXTRACTION.md) renders BG3
 * and the portrait slots of the HUD band into two surfaces of their own and leaves them out of
 * the game picture; SoeHudEndFrame draws them back at the place the options ask for. The
 * capture runs in the optimized renderer only, so the field uses it while the HUD is captured.
 * Each frame checks the band holds a HUD box first: a frame without one (cutscenes, Mode 7,
 * menu pages, the title) is drawn as the game draws it. At the top, a window the game draws
 * there (dialogue, a ring menu's title) wins: the HUD stays at the bottom while it is open.
 *
 * Presentation only: reads VRAM, OAM and PPU registers, writes no guest state.
 *
 * Test overrides (win over the mod): SOE_HUD=0|1, SOE_HUD_POSITION=bottom|top|hidden,
 * SOE_HUD_LAYOUT=centered|edges. SOE_HUD_DEBUG=1 prints each capture; SOE_HUD_SHOT (below)
 * saves whole wide fields.
 */
#include "soe_hud.h"
#include "snes_osd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "mod_runtime.h"
#include "snes/ppu.h"

enum {
    kBg3 = 2,
    kHudRow = 24,             /* first BG3 row of the HUD band */
    kBoxRow = 25,             /* the row of each box's top edge */
    kBoyBoxCol = 3, kDogBoxCol = 19,
    kBoxCorner = 0x0BEA,      /* tile $3EA, palette 2 (priority masked off) */
    kBlankTile = 0x3FF,
    kBoyPortrait = 0x080, kDogPortrait = 0x082,
    kLines = 224,
    kTopLine = 6,             /* the top edge of the HUD's top row when moved up (mirrors the bottom gap) */
    kHalf = 128,              /* left of it the Boy's half, right of it the Dog's */
    kHeld = 0x0EC6,           /* INPUT_P1_HELD_BUTTONS */
    kStart = 0x1000, kR = 0x0010, kX = 0x0040,
};

#define kModPackage "soe.enhancement.hud"
#define kModFeature "hud"

enum Position { kBottom, kTop, kHidden };
enum Layout { kCentered, kEdges };

static struct {
    int on;
    enum Position position;
    enum Layout layout;
} s_mod;

static int s_hotkey_position = -1;
static int s_hotkey_layout = -1;
static uint16_t s_prev_hud_chord = 0;

/* This frame's capture: band lines [y0, kLines), moved by dy and by dx_left / dx_right per half. */
static struct {
    int active, draw, frame_w, extra, y0, dy, dx_left, dx_right;
} s_cap;

static uint32_t s_bg3[kPpuBufWidth * kLines], s_obj[kPpuBufWidth * kLines];
static int s_frame_w = 256;   /* this frame's width, captured or not */

static void ModReset(void)
{
    memset(&s_mod, 0, sizeof s_mod);
    s_hotkey_position = -1;
    s_hotkey_layout = -1;
    s_prev_hud_chord = 0;
}

static int OptionIs(const char *option, const char *value)
{
    char v[32];
    return snes_mod_runtime_feature_option_value_c(kModPackage, kModFeature, option, v, sizeof v) && !strcmp(v, value);
}

static void ModActivate(void)
{
    s_mod.on = 1;
    s_mod.position = OptionIs("position", "top") ? kTop : OptionIs("position", "hidden") ? kHidden : kBottom;
    s_mod.layout = OptionIs("layout", "centered") ? kCentered : kEdges;
}

/* Presentation only: registered as such, it changes the picture and nothing the game reads. */
SNES_MOD_CONSTRUCTOR(soe_register_hud_plugin)
{
    (void)snes_mod_register_reset_callback(ModReset);
    (void)snes_mod_register_presentation_plugin("soe.hud", ModActivate);
}

static int Enabled(void)
{
    const char *v = getenv("SOE_HUD");
    if (v && *v) return atoi(v) != 0;
    return s_mod.on || snes_mod_runtime_feature_enabled_c(kModPackage, kModFeature);
}

static enum Position PositionMode(void)
{
    if (s_hotkey_position >= 0) return (enum Position)s_hotkey_position;
    const char *v = getenv("SOE_HUD_POSITION");
    if (v && *v) return !strcmp(v, "top") ? kTop : !strcmp(v, "hidden") ? kHidden : kBottom;
    if (snes_mod_runtime_feature_enabled_c(kModPackage, kModFeature)) {
        if (OptionIs("position", "top")) return kTop;
        if (OptionIs("position", "hidden")) return kHidden;
        return kBottom;
    }
    return s_mod.on ? s_mod.position : kBottom;
}

static enum Layout LayoutMode(void)
{
    if (s_hotkey_layout >= 0) return (enum Layout)s_hotkey_layout;
    const char *v = getenv("SOE_HUD_LAYOUT");
    if (v && *v) return !strcmp(v, "centered") ? kCentered : kEdges;
    if (snes_mod_runtime_feature_enabled_c(kModPackage, kModFeature)) {
        return OptionIs("layout", "centered") ? kCentered : kEdges;
    }
    return s_mod.on ? s_mod.layout : kEdges;
}

static uint16_t Bg3Word(int row, int col)
{
    return g_ppu->vram[(PPU_bgTilemapAdr(g_ppu, kBg3) + (uint32_t)(row & 31) * 32 + (uint32_t)(col & 31)) & 0x7fff];
}

/* The screen line BG3 row `row` starts on, at this frame's scroll (32x32 map, 8x8 tiles). */
static int Bg3RowLine(int row)
{
    return (row * 8 - g_ppu->vScroll[kBg3]) & 0xff;
}

static int HudShown(void)
{
    if ((g_ppu->bgmode & 7) != 1 || (g_ppu->inidisp & 0x80) || g_ppu->bgXsc[kBg3] & 3) return 0;
    return (Bg3Word(kBoxRow, kBoyBoxCol) & 0x1fff) == kBoxCorner ||
           (Bg3Word(kBoxRow, kDogBoxCol) & 0x1fff) == kBoxCorner;
}

/* Something of the game's own on BG3 where the HUD would go: a dialogue window in the rows that
 * start on lines [0, 32). Not the row whose last line wraps onto line 0 (row 31 at the field's
 * scroll): rows 28..31 hold no map in the field (VRAM words $0B80..$0BFF, other data). */
static int TopTaken(void)
{
    for (int row = 0; row < 32; row++) {
        if (Bg3RowLine(row) >= 32) continue;
        for (int col = 0; col < 32; col++)
            if ((Bg3Word(row, col) & 0x3ff) != kBlankTile) return 1;
    }
    return 0;
}

/* The first portrait slot in the band and how many (1 or 2, adjacent), or -1. */
static int PortraitSlots(int y0, int *count)
{
    int first = -1, last = -1;
    for (int i = 0; i < 128; i++) {
        const uint16_t pos = g_ppu->oam[i * 2], attr = g_ppu->oam[i * 2 + 1];
        const int tile = attr & 0x1ff, large = (g_ppu->highOam[i >> 2] >> ((i & 3) * 2)) & 2, y = pos >> 8;
        if (!large || (tile != kBoyPortrait && tile != kDogPortrait) || y < y0 || y >= kLines) continue;
        if (first < 0) first = i;
        last = i;
    }
    if (first < 0 || last - first > 1) return -1;
    *count = last - first + 1;
    return first;
}

static void Unbind(void)
{
    if (!s_cap.active || !g_ppu) return;
    PpuBindOverlaySurface(g_ppu, kPpuOverlaySource_Bg3, NULL, 0);
    PpuBindOverlaySurface(g_ppu, kPpuOverlaySource_Obj, NULL, 0);
    s_cap.active = 0;
}

int SoeHudPrepareFrame(int frame_w)
{
    const uint16_t held = (uint16_t)(g_ram[kHeld] | (g_ram[kHeld + 1] << 8));
    const uint16_t chord = (held & kStart) ? (held & (kR | kX)) : 0;
    if (chord && chord != s_prev_hud_chord) {
        if (chord & kR) {
            enum Position cur = PositionMode();
            enum Position next = (cur == kBottom) ? kTop : (cur == kTop) ? kHidden : kBottom;
            s_hotkey_position = next;
            snes_osd_push(next == kBottom ? "HUD: bottom" : next == kTop ? "HUD: top" : "HUD: hidden", 0);
        } else if (chord & kX) {
            enum Layout cur = LayoutMode();
            enum Layout next = (cur == kEdges) ? kCentered : kEdges;
            s_hotkey_layout = next;
            snes_osd_push(next == kEdges ? "HUD layout: screen edges" : "HUD layout: centred", 0);
        }
    }
    s_prev_hud_chord = chord;
    const enum Position position = Enabled() ? PositionMode() : kBottom;
    const enum Layout layout = LayoutMode();
    const int extra = (frame_w - 256) / 2;
    s_frame_w = frame_w;
    if (!g_ppu || frame_w < 256 || frame_w > kPpuBufWidth || !Enabled() ||
        (position == kBottom && (layout == kCentered || extra <= 0)) || !HudShown()) {
        Unbind();
        return 0;
    }
    const int y0 = Bg3RowLine(kHudRow);
    if (y0 < kLines / 2) { Unbind(); return 0; }
    const int moved_up = position == kTop && !TopTaken();
    if (position == kTop && !moved_up && (layout == kCentered || extra <= 0)) { Unbind(); return 0; }

    s_cap.frame_w = frame_w;
    s_cap.extra = extra;
    s_cap.y0 = y0;
    s_cap.dy = moved_up ? kTopLine - Bg3RowLine(kBoxRow) : 0;
    s_cap.dx_left = layout == kEdges ? -extra : 0;
    s_cap.dx_right = layout == kEdges ? extra : 0;
    s_cap.draw = position != kHidden;

    const size_t pitch = (size_t)frame_w * 4;
    PpuBindOverlaySurface(g_ppu, kPpuOverlaySource_Bg3, (uint8_t *)s_bg3, pitch);
    PpuBindOverlaySurface(g_ppu, kPpuOverlaySource_Obj, (uint8_t *)s_obj, pitch);
    PpuSetOverlayCapture(g_ppu, kPpuOverlaySource_Bg3, -extra, y0, frame_w, kLines - y0,
                         kPpuOverlayFlag_RemoveFromGame);
    int count = 0;
    const int first = PortraitSlots(y0, &count);
    if (first >= 0) {
        PpuSetOverlayCapture(g_ppu, kPpuOverlaySource_Obj, -extra, y0, frame_w, kLines - y0,
                             kPpuOverlayFlag_RemoveFromGame);
        PpuSetOverlayOamRange(g_ppu, (uint8_t)first, (uint8_t)count);
    } else {
        PpuBindOverlaySurface(g_ppu, kPpuOverlaySource_Obj, NULL, 0);
    }
    s_cap.active = 1;
    if (getenv("SOE_HUD_DEBUG"))
        fprintf(stderr, "[hud] y0 %d dy %d slots %d+%d renderer %u\n", y0, s_cap.dy, first, count,
                g_ppu->renderFlags);
    return 1;
}

/* Copies one surface's band to the field, each half moved by its own offset. */
static void Blit(uint32_t *field, const uint32_t *surface)
{
    const int w = s_cap.frame_w;
    for (int y = s_cap.y0; y < kLines; y++) {
        const int dst_y = y + s_cap.dy;
        if (dst_y < 0 || dst_y >= kLines) continue;
        const uint32_t *src = surface + (size_t)y * w;
        uint32_t *dst = field + (size_t)dst_y * w;
        for (int x = 0; x < w; x++) {
            if (!(src[x] >> 24)) continue;
            const int dst_x = x + (x - s_cap.extra < kHalf ? s_cap.dx_left : s_cap.dx_right);
            if (dst_x >= 0 && dst_x < w) dst[dst_x] = src[x];
        }
    }
}

/* SOE_HUD_SHOT=<dir> with SOE_HUD_SHOT_FRAMES=<n,n,...>: the whole field of those frames as
 * <dir>/<n>-<width>.bgrx (the script's `dump` keeps only the 4:3 picture). */
static void Shot(const uint8_t *field, unsigned number)
{
    const char *dir = getenv("SOE_HUD_SHOT"), *frames = getenv("SOE_HUD_SHOT_FRAMES");
    if (!dir || !frames) return;
    for (const char *p = frames; *p; p++) {
        if (strtoul(p, NULL, 10) == number && (p == frames || p[-1] == ',')) {
            const int w = s_frame_w;
            char path[512];
            snprintf(path, sizeof path, "%s/%u-%d.bgrx", dir, number, w);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(field, 4, (size_t)w * kLines, f); fclose(f); }
            return;
        }
    }
}

void SoeHudEndFrame(const uint8_t *field, unsigned number)
{
    if (!field) return;
    if (s_cap.active && s_cap.draw) {
        uint32_t *px = (uint32_t *)(uintptr_t)field;
        if (g_ppu->overlayRenderBuffer[kPpuOverlaySource_Obj]) Blit(px, s_obj);
        Blit(px, s_bg3);   /* BG3 with priority in mode 1 draws over every sprite */
    }
    Shot(field, number);
}
