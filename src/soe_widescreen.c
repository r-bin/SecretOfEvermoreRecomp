/*
 * Widescreen, first approach: field backgrounds only.
 *
 * What it does (docs/widescreen/progress.md, step 1):
 *   - is switched on by the mod package soe.enhancement.widescreen (launcher, Mods page;
 *     mods/preloaded/packages/), whose options pick the view (adaptive or a fixed aspect) and
 *     the room edges; the frame follows the window's shape or the fixed aspect, up to the PPU's
 *     800 px, and DisplayAspect sets the pixel shape;
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
 * Each mode is its own feature of the package (one plugin, so enabling one turns the other off),
 * with its own options: `widescreen` (vanilla) and `extended` (vanilla extended).
 *
 * Test overrides (win over the mod): SOE_WIDESCREEN=0|1, SOE_WS_ASPECT=W:H|adaptive,
 * SOE_WS_WIDTH=<px>, SNESRECOMP_WS_EXTRA=<px per side>, SOE_WS_EDGES=limits|room|off,
 * SOE_WS_FOREGROUND=limits|terrain, SOE_WS_MODE=vanilla|extended, SOE_WS_LOOK=colour|grey.
 * Kill switch SOE_WS_PREFILL=0 (draw the raw ring). SOE_WS_CHECK=1 compares the prefill rule
 * with the game's own tilemap words for the on-screen cells and prints the result.
 */
#include "soe_widescreen.h"
#include "soe_ws_sprites.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "mod_runtime.h"
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
    kLimitTop = 0x23EB,
    kLimitBottom = 0x23EF,
    kBlankCell = 0x8000,    /* $7F:8000, the tile word the room set-up clears both maps with */
};

static uint16_t Ram16(uint32_t a) { return (uint16_t)(g_ram[a] | g_ram[a + 1] << 8); }
static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int EnvFlag(const char *name, int dflt)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) != 0 : dflt;
}

/* The mod package and its features, one per mode; both claim the plugin "soe.widescreen". */
#define kModPackage "soe.enhancement.widescreen"
#define kModVanilla "widescreen"
#define kModExtended "extended"

enum Edges { kEdgesLimits, kEdgesRoom, kEdgesOff };
/* BG1, the canopy in front: clipped to the camera-limit box, or shown as far as the terrain. */
enum Foreground { kForegroundLimits, kForegroundTerrain };

/* Settings from the mod, read on activation. */
static struct {
    int on, extended, grey;
    char aspect[16];
    enum Edges edges;
    enum Foreground foreground;
} s_mod;

static int OptionIs(const char *feature, const char *option, const char *value)
{
    char v[32];
    return snes_mod_runtime_feature_option_value_c(kModPackage, feature, option, v, sizeof v) && !strcmp(v, value);
}

/* The mod alone decides; the legacy config flag `Widescreen` is not read. */
static void ModReset(void)
{
    memset(&s_mod, 0, sizeof(s_mod));
}

static void ModActivate(void)
{
    char v[32];
    s_mod.on = 1;
    s_mod.extended = snes_mod_runtime_feature_enabled_c(kModPackage, kModExtended);
    const char *feature = s_mod.extended ? kModExtended : kModVanilla;
    strcpy(s_mod.aspect, "adaptive");
    if (snes_mod_runtime_feature_option_value_c(kModPackage, feature, "aspect", v, sizeof v))
        snprintf(s_mod.aspect, sizeof s_mod.aspect, "%s", v);
    s_mod.edges = OptionIs(feature, "edges", "room") ? kEdgesRoom : kEdgesLimits;
    s_mod.foreground = OptionIs(feature, "foreground", "terrain") ? kForegroundTerrain : kForegroundLimits;
    s_mod.grey = s_mod.extended && OptionIs(feature, "look", "grey");
}

/* Presentation only: this code changes what is drawn and touches no CPU, WRAM, VRAM, OAM,
 * CGRAM, APU or save state (ctest replay_0x_widescreen holds it to that). */
SNES_MOD_CONSTRUCTOR(soe_register_widescreen_plugin)
{
    (void)snes_mod_register_reset_callback(ModReset);
    (void)snes_mod_register_presentation_plugin("soe.widescreen", ModActivate);
}

/* Widescreen on: the mod, or SOE_WIDESCREEN=0|1 (tests). The window is sized after the mod
 * selection is committed but before plugins activate, so ask the runtime then. */
static int Enabled(void)
{
    const char *v = getenv("SOE_WIDESCREEN");
    if (v && *v) return atoi(v) != 0;
    return s_mod.on || snes_mod_runtime_feature_enabled_c(kModPackage, kModVanilla) ||
           snes_mod_runtime_feature_enabled_c(kModPackage, kModExtended);
}

/* Vanilla extended: entities drawn in the margins from WRAM (src/soe_ws_sprites.c). SOE_WS_MODE
 * = vanilla|extended overrides the mod. */
static int ExtendedMode(void)
{
    const char *v = getenv("SOE_WS_MODE");
    if (v && *v) return strcmp(v, "vanilla") != 0;
    return s_mod.extended;
}

static enum Edges EdgesMode(void)
{
    const char *v = getenv("SOE_WS_EDGES");
    if (v && *v) return !strcmp(v, "room") ? kEdgesRoom : !strcmp(v, "off") || !strcmp(v, "0") ? kEdgesOff : kEdgesLimits;
    return s_mod.edges;
}

static enum Foreground ForegroundMode(void)
{
    const char *v = getenv("SOE_WS_FOREGROUND");
    if (v && *v) return !strcmp(v, "terrain") ? kForegroundTerrain : kForegroundLimits;
    return s_mod.foreground;
}

/* Margin characters in grey: SOE_WS_LOOK=grey|colour overrides the mod. */
static int GreyLook(void)
{
    const char *v = getenv("SOE_WS_LOOK");
    if (v && *v) return !strcmp(v, "grey");
    return s_mod.grey;
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

/* The fixed view aspect: SOE_WS_ASPECT, else the mod's option; any "W:H" (16:9, 21:9, 32:9,
 * 5:4 ...). Returns 0 for adaptive (follow the window). */
static double FixedAspect(void)
{
    const char *a = getenv("SOE_WS_ASPECT");
    if (!a || !*a) a = s_mod.aspect;
    double w, h;
    if (sscanf(a, "%lf:%lf", &w, &h) == 2 && w > 0 && h > 0) return w / h;
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

/* A field frame carries the exact register set the field set-up `$8E:E08B` writes: BGMODE $39
 * (mode 1, BG3 priority, 16x16 tiles on BG1 and BG2 only), maps BG1 $00 / BG2 $04 / BG3 $08,
 * BG12NBA $22; plus camera mode 0 and a camera-limit box at least one screen wide. Anything
 * else (intro and title screens, menu pages, Mode 7, other camera modes) is pillarboxed: the
 * intro logo is mode 1 with 16x16 tiles too, but BGMODE $79, both BGs on map $00, no room. */
static int IsFieldFrame(void)
{
    if (g_ppu->bgmode != 0x39 || g_ppu->bgXsc[0] != 0x00 || g_ppu->bgXsc[1] != 0x04 ||
        g_ppu->bgXsc[2] != 0x08 || (g_ppu->bgTileAdr & 0xff) != 0x22)
        return 0;
    int left = (int16_t)Ram16(kLimitLeft), right = (int16_t)Ram16(kLimitRight);
    return Ram16(kCameraMode) == 0 && right - left >= 256 && g_ram[kRoomCellsW] != 0;
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

/* Room pixels BG1 may show; cells wholly outside get the blank word. */
typedef struct Box { int left, top, right, bottom; } Box;

static int CellOutside(const Box *box, int cx, int cy)
{
    return cx * 16 + 16 <= box->left || cx * 16 >= box->right || cy * 16 + 16 <= box->top || cy * 16 >= box->bottom;
}

static void PrefillColumns(int x0, int x1, int cam_y, int cells_w, int cells_h, const Box *fg)
{
    for (int cx = x0 >> 4; cx <= (x1 - 1) >> 4; cx++) {
        if (cx < 0 || cx >= cells_w) continue;
        for (int cy = cam_y >> 4; cy <= (cam_y + 224) >> 4 && cy < cells_h; cy++)
            /* Force, not Prefill: the room set-up clears the whole ring by DMA, and the
             * shadow keeps those blank uploads as authoritative history that Prefill
             * never replaces. The grid is the live source of the same words. */
            for (int layer = 0; layer < 2; layer++) {
                uint16_t word = layer == 0 && fg && CellOutside(fg, cx, cy) ? Ram16(kBank7F + kBlankCell)
                                                                           : CellTileWord(layer, cx, cy, cells_w);
                WsShadowForceTile(layer, (uint32_t)cx, (uint32_t)cy, word);
            }
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
        SoeWsSpritesReset();
        s_room = room;
    }

    if (EnvFlag("SOE_WS_DEBUG", 0) && number % 60 == 0)
        fprintf(stderr, "[widescreen] frame %u: mode %d big %d/%d camera mode %u extra %d\n", number,
                PPU_mode(g_ppu), !!PPU_bigTiles(g_ppu, 0), !!PPU_bigTiles(g_ppu, 1), Ram16(kCameraMode),
                g_ws_extra);
    if (!IsFieldFrame()) {
        PpuSetExtraSideSpace(g_ppu, 0, 0, 0);   /* pillarbox */
        SoeWsSpritesReset();
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
    enum Edges edges = EdgesMode();
    if (edges != kEdgesOff) {
        int lo = edges == kEdgesRoom ? 0 : (int16_t)Ram16(kLimitLeft);
        int hi = edges == kEdgesRoom ? cells_w * 16 : (int16_t)Ram16(kLimitRight);
        left = Clamp(cam_x - lo, 0, g_ws_extra);
        right = Clamp(hi - (cam_x + 256), 0, g_ws_extra);
    }
    PpuSetExtraSideSpace(g_ppu, left, right, 0);
    s_left = left, s_right = right;
    SoeWsSpritesFrame(ExtendedMode(), GreyLook(), cam_x, cam_y, left, right);

    if (!EnvFlag("SOE_WS_PREFILL", 1) || cells_w == 0) return;
    WsShadowSetWorld(0, (uint32_t)bg1_x, (uint32_t)bg1_y);
    WsShadowSetWorld(1, (uint32_t)bg2_x, (uint32_t)bg2_y);
    WsShadowFrame(g_ppu);
    /* With the margins cut to the limit box BG1 already is; past it (whole room) the canopy can
     * stop at the box while the terrain goes on. */
    Box box = {(int16_t)Ram16(kLimitLeft), (int16_t)Ram16(kLimitTop), (int16_t)Ram16(kLimitRight),
               (int16_t)Ram16(kLimitBottom)};
    const Box *fg = ForegroundMode() == kForegroundLimits ? &box : NULL;
    PrefillColumns(cam_x - left, cam_x, cam_y, cells_w, cells_h, fg);
    PrefillColumns(cam_x + 256, cam_x + 256 + right, cam_y, cells_w, cells_h, fg);
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
