/*
 * Widescreen, first approach: field backgrounds only.
 *
 * What it does (docs/widescreen/progress.md, step 1):
 *   - widens the frame when config `Widescreen = 1` (or SOE_WIDESCREEN=1): to the window's
 *     shape by default, or SOE_WS_ASPECT=W:H, SOE_WS_WIDTH=<px>, SNESRECOMP_WS_EXTRA=<px per side>,
 *     up to the PPU's 800 px; DisplayAspect sets the pixel shape;
 *   - on field frames only (BG mode 1, 16x16 tiles on BG1 and BG2, camera mode 0)
 *     opens the side margins, cut to the active camera-limit box $23E9..$23EF so
 *     a wide view never shows a neighbouring part of the map (the huts of 0x51);
 *   - fills the margin cells of BG1 and BG2 from the room grid in bank $7F
 *     (word = 2*(row*width + col), tile word at $7F:grid+0 for BG1, +2 for BG2),
 *     because the game streams only a 17 x 15-cell window into the tilemap ring;
 *   - pillarboxes everything else (menu pages, Mode 7, other camera modes).
 * Sprites stay as the game places them: it never puts one at X >= 256, so enemies
 * still appear at the original edge (the "vanilla" enemy mode).
 *
 * Presentation only: reads WRAM and the PPU, writes neither. Addresses and their
 * evidence: everscript-vscode docs/entities/off-screen-behaviour.md, tag `widescreen`.
 *
 * Kill switches: SOE_WS_PREFILL=0 (draw the raw ring), SOE_WS_EDGES=0 (ignore the
 * camera limits, use the whole budget). SOE_WS_CHECK=1 compares the prefill rule
 * with the game's own tilemap words for the on-screen cells and prints the result.
 */
#include "soe_widescreen.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "desktop/config.h"
#include "desktop/display_aspect.h"
#include "snes/ppu.h"
#include "snes/ws_shadow.h"
#include "widescreen.h"

enum {
    kBank7F = 0x10000,      /* g_ram offset of $7F:0000 */
    kRoomId = 0x0ADB,
    kRoomCellsW = 0x08EE,
    kRoomCellsH = 0x08F0,
    kLimitLeft = 0x23E9,
    kLimitRight = 0x23ED,
    kCameraMode = 0x241F,   /* index into the camera-mode table $90:8E68 */
    kScrollBg1H = 0x010E,   /* BG scroll shadows, copied to the PPU in NMI */
    kScrollBg1V = 0x0110,
    kScrollBg2H = 0x0112,
    kScrollBg2V = 0x0114,
};

static uint16_t Ram16(uint32_t a) { return (uint16_t)(g_ram[a] | g_ram[a + 1] << 8); }
static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int EnvFlag(const char *name, int dflt)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) != 0 : dflt;
}

/* Widescreen on: config `Widescreen = 1`, or SOE_WIDESCREEN=1 (tests force it on or off). */
static int Enabled(void)
{
    const char *v = getenv("SOE_WIDESCREEN");
    return v && *v ? atoi(v) != 0 : g_config.widescreen;
}

/* Horizontal:vertical size of one SNES pixel as presented (config DisplayAspect). */
static double PixelAspect(void)
{
    int n, d;
    SnesDisplayAspect_GetPixelAspect(SnesDisplayAspect_Clamp(g_config.display_aspect), &n, &d);
    return (double)n / d;
}

/* Frame width that shows a 224-line picture at `view_aspect`; even, so the centre stays put. */
static int WidthForAspect(double view_aspect)
{
    int w = (int)(view_aspect * 224.0 / PixelAspect() + 0.5);
    w += w & 1;
    return Clamp(w, 256, kPpuBufWidth);
}

/* SOE_WS_ASPECT: "adaptive" (default: follow the window), or any "W:H" (16:9, 21:9, 32:9, 5:4 ...).
 * Returns 0 for adaptive. */
static double FixedAspect(void)
{
    const char *a = getenv("SOE_WS_ASPECT");
    double w, h;
    if (a && sscanf(a, "%lf:%lf", &w, &h) == 2 && w > 0 && h > 0) return w / h;
    return 0;
}

/* The width this run asks for. Order: SOE_WS_WIDTH=<px>, SNESRECOMP_WS_EXTRA=<px per side>,
 * a fixed aspect, else the window's shape (16:9 before the window exists). */
static int WantedWidth(int drawable_w, int drawable_h)
{
    const char *px = getenv("SOE_WS_WIDTH");
    if (px && *px) return Clamp(atoi(px) & ~1, 256, kPpuBufWidth);
    int pinned = PpuWsExtraOverride();
    if (pinned >= 0) return Clamp(256 + 2 * pinned, 256, kPpuBufWidth);
    double fixed = FixedAspect();
    if (fixed > 0) return WidthForAspect(fixed);
    if (drawable_w > 0 && drawable_h > 0) return WidthForAspect((double)drawable_w / drawable_h);
    return WidthForAspect(16.0 / 9.0);
}

void SoeWsPrepareFrame(int drawable_w, int drawable_h, int *frame_w, int *frame_h)
{
    *frame_h = 224;
    *frame_w = Enabled() ? WantedWidth(drawable_w, drawable_h) : 256;
}

/* The host opens the window before the first frame decides its width, so it asks here:
 * a 240-line window at the shape of the frame we will draw. */
int SoeWsWindowBaseWidth(int frame_w)
{
    int w = Enabled() ? WantedWidth(0, 0) : frame_w;
    return (int)(240.0 * w * PixelAspect() / 224.0 + 0.5);
}

/* The field set-up writes BGMODE $39: mode 1, BG3 priority, 16x16 tiles on BG1/BG2. */
static int IsFieldFrame(void)
{
    return PPU_mode(g_ppu) == 1 && PPU_bigTiles(g_ppu, 0) && PPU_bigTiles(g_ppu, 1) &&
           Ram16(kCameraMode) == 0;
}

static uint16_t CellTileWord(int layer, int cx, int cy, int cells_w)
{
    uint16_t grid = Ram16(kBank7F + 2u * (uint32_t)(cy * cells_w + cx));
    return Ram16(kBank7F + (uint16_t)(grid + 2 * layer));
}

static struct { unsigned frames; uint64_t same, differ; } s_check;

/* Compare the prefill rule with the words the game itself streamed for the cells
 * on screen. The streamed window starts at the scroll's cell. */
static void CheckOnScreenCells(int cam_x, int cam_y, int cells_w, int cells_h)
{
    const uint16_t *vram = PpuRenderVram(g_ppu);
    for (int layer = 0; layer < 2; layer++) {
        uint32_t base = PPU_bgTilemapAdr(g_ppu, layer);
        for (int cy = cam_y >> 4; cy < ((cam_y + 224) >> 4) && cy < cells_h; cy++)
            for (int cx = cam_x >> 4; cx < ((cam_x + 256) >> 4) && cx < cells_w; cx++) {
                uint16_t game = vram[(base + (uint32_t)(cy & 31) * 32 + (uint32_t)(cx & 31)) & 0x7fff];
                if (game == CellTileWord(layer, cx, cy, cells_w)) s_check.same++;
                else s_check.differ++;
            }
    }
    if (++s_check.frames % 600 == 0)
        fprintf(stderr, "[widescreen] prefill check: %llu cells match, %llu differ\n",
                (unsigned long long)s_check.same, (unsigned long long)s_check.differ);
}

static void PrefillColumns(int x0, int x1, int cam_y, int cells_w, int cells_h)
{
    for (int cx = x0 >> 4; cx <= (x1 - 1) >> 4; cx++) {
        if (cx < 0 || cx >= cells_w) continue;
        for (int cy = cam_y >> 4; cy <= (cam_y + 224) >> 4 && cy < cells_h; cy++)
            /* Force, not Prefill: the room set-up clears the whole ring by DMA, and the
             * shadow keeps those blank uploads as authoritative history that Prefill
             * never replaces. The grid is the live source of the same words. */
            for (int layer = 0; layer < 2; layer++)
                WsShadowForceTile(layer, (uint32_t)cx, (uint32_t)cy, CellTileWord(layer, cx, cy, cells_w));
    }
}

/* The scroll the PPU draws this field with, in room pixels. The NMI loaded the PPU from the
 * shadows before this field ran; the shadows in WRAM already hold the next frame's camera, so
 * using them puts the margins a frame ahead of the centre (a seam while scrolling, rule P1).
 * The registers are 10 bits; the shadow supplies the high bits. */
static int DrawnScroll(uint16_t ppu_scroll, uint16_t shadow)
{
    int delta = (int)((ppu_scroll - shadow + 512) & 1023) - 512;
    return (int16_t)shadow + delta;
}

/* Margin columns drawn this frame; the rest of the margin is cleared in SoeWsEndFrame. */
static int s_left, s_right;

void SoeWsBeginFrame(unsigned number)
{
    s_left = s_right = 0;
    if (!g_ppu || g_ws_extra <= 0) return;

    static uint16_t s_room = 0xffff;
    uint16_t room = Ram16(kRoomId);
    if (room != s_room) {
        WsShadowReset();
        s_room = room;
    }

    if (EnvFlag("SOE_WS_DEBUG", 0) && number % 60 == 0)
        fprintf(stderr, "[widescreen] frame %u: mode %d big %d/%d camera mode %u extra %d\n", number,
                PPU_mode(g_ppu), !!PPU_bigTiles(g_ppu, 0), !!PPU_bigTiles(g_ppu, 1), Ram16(kCameraMode),
                g_ws_extra);
    if (!IsFieldFrame()) {
        PpuSetExtraSideSpace(g_ppu, 0, 0, 0);   /* pillarbox */
        return;
    }

    int bg1_x = DrawnScroll(g_ppu->hScroll[0], Ram16(kScrollBg1H));
    int bg1_y = DrawnScroll(g_ppu->vScroll[0], Ram16(kScrollBg1V));
    int bg2_x = DrawnScroll(g_ppu->hScroll[1], Ram16(kScrollBg2H));
    int bg2_y = DrawnScroll(g_ppu->vScroll[1], Ram16(kScrollBg2V));
    /* Camera mode 0: BG2 scroll = camera, vertical one line less. */
    int cam_x = bg2_x, cam_y = bg2_y + 1;
    int cells_w = g_ram[kRoomCellsW], cells_h = Ram16(kRoomCellsH);
    int left = g_ws_extra, right = g_ws_extra;
    if (EnvFlag("SOE_WS_EDGES", 1)) {
        left = Clamp(cam_x - (int16_t)Ram16(kLimitLeft), 0, g_ws_extra);
        right = Clamp((int16_t)Ram16(kLimitRight) - (cam_x + 256), 0, g_ws_extra);
    }
    PpuSetExtraSideSpace(g_ppu, left, right, 0);
    s_left = left, s_right = right;

    if (!EnvFlag("SOE_WS_PREFILL", 1) || cells_w == 0) return;
    WsShadowSetWorld(0, (uint32_t)bg1_x, (uint32_t)bg1_y);
    WsShadowSetWorld(1, (uint32_t)bg2_x, (uint32_t)bg2_y);
    WsShadowFrame(g_ppu);
    PrefillColumns(cam_x - left, cam_x, cam_y, cells_w, cells_h);
    PrefillColumns(cam_x + 256, cam_x + 256 + right, cam_y, cells_w, cells_h);
    if (EnvFlag("SOE_WS_CHECK", 0)) CheckOnScreenCells(cam_x, cam_y, cells_w, cells_h);
    if (EnvFlag("SOE_WS_DEBUG", 0) && number % 60 == 0) {
        WsShadowMarginStat st;
        WsShadowGetMarginStats(1, &st);
        fprintf(stderr, "[widescreen] BG2 margins: west %llu/%llu east %llu/%llu (hit/miss), seeded %llu, raw %llu/%llu\n",
                (unsigned long long)st.westHit, (unsigned long long)st.westMiss, (unsigned long long)st.eastHit,
                (unsigned long long)st.eastMiss, (unsigned long long)st.prefillSeed,
                (unsigned long long)st.westRawFallback, (unsigned long long)st.eastRawFallback);
    }
}

/* The PPU leaves margin columns it was not asked to draw untouched, so they kept old pixels
 * (smeared rows past a room edge). Black them: outside the room there is nothing to show. */
void SoeWsEndFrame(const uint8_t *field, unsigned number)
{
    (void)number;
    if (!field || g_ws_extra <= 0) return;
    const int width = 256 + 2 * g_ws_extra;
    uint32_t *px = (uint32_t *)(uintptr_t)field;
    const int left_black = g_ws_extra - s_left, right_from = g_ws_extra + 256 + s_right;
    for (int y = 0; y < 224; y++) {
        uint32_t *row = px + (size_t)y * width;
        for (int x = 0; x < left_black; x++) row[x] = 0;
        for (int x = right_from; x < width; x++) row[x] = 0;
    }
}
