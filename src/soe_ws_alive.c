/*
 * Widescreen experiment E6: the whole room alive, and at most `cap` characters drawn into a fight in the 4:3 view
 * (docs/widescreen/enemies.md, "E6 as built"). This CHANGES the game: an ordinary plugin, never presentation-only,
 * active only while the launcher's "Widescreen (experimental)" feature is on.
 *
 * Three parts:
 *  - Free time (SoeAliveFreeTime; src/soe_bindings.c HleFieldTickFree*, src/soe_frame_driver.c): the script and entity
 *    part of each field frame costs no emulated time, so all of a room's enemies fit a frame. The SNES CPU could not
 *    run them all (the flower field 0x38 ran at ~55 % speed); the recomp's host can.
 *  - Nobody sleeps (SoeAliveDecide, at $8F:C40F entity_sleep_wake): vanilla lets an entity sleep when it is 48 px off
 *    the 4:3 screen and the awake budget $1076 is full; here every sleeper wakes and nobody is put to sleep. Asleep
 *    with +$10 & $0078, HP 0, +$6C >= 0 or a spawner record (+$8C) goes the ROM's way. A segmented body (record +$07
 *    bit 0) wakes only while one of the ten segment lists at $58AF is free: a wake without one leaves $1070/$1072
 *    changed ($8F:C4D6). The budget $1076 = $1070 + (free slots - reserve): the spawner ($8F:DDBB, unchanged) stops
 *    while only `reserve` slots are free, so scripts that create entities find one; the lag rule is overruled.
 *  - The aggro cap (SoeAliveMayAggro, at the three places an enemy picks the party as its target: ai_pick_target
 *    $8F:D879, enemy_ai_check_aggro $8F:D69B, ai_stationary_player $8F:D715): an enemy inside the 4:3 view acts as in
 *    vanilla; one outside it takes the party as a target only while fewer than `cap` characters are in the view (the
 *    Boy and the Dog counted, as the game's budget counts them). Otherwise it goes the ROM's own way for "no target":
 *    it wanders or keeps its default action, and one already chasing from outside lets go.
 *
 * Stateless: every decision follows from WRAM at the call. The ROM code does every change of state; this file only
 * picks the branch. Not gated on the widescreen field gate: a menu or a narrow room in between would let vanilla put
 * the room back to sleep (the hooks run only in the field tick anyway).
 *
 * Settings: the mod feature "experimental" (options cap, count). SOE_WS_ALIVE=0 switches E6 off (kill switch),
 * SOE_WS_FREE=0 only its free time; nothing switches E6 on but the feature. Test knobs: SOE_WS_ALIVE_RESERVE=<n>
 * (default 4), SOE_WS_ALIVE_DEBUG=1 prints count and budget every 60th frame.
 */
#include "soe_ws_alive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "mod_runtime.h"

enum {
    kSlotFirst = 0x3DE5, kSlotEnd = 0x4E89, kStride = 0x8E,   /* the 30 slots the round robin visits */
    kBoy = 0x4E89, kDog = 0x4F37,
    kAwakeCount = 0x1070, kBudget = 0x1076, kFrameCounter = 0x0100, kRoomId = 0x0ADB,
    kSegmentLists = 0x58AF, kSegmentEnd = 0x5DC3, kSegmentStride = 0x82,
    kVanillaBudget = 6,
    /* entity fields */
    kFlags = 0x10, kX = 0x1A, kY = 0x1C, kHp = 0x2A, kNode = 0x6C, kRecord = 0x60, kSpawnRecord = 0x8C,
    kAwake = 0x0100, kAsleep = 0x0200,
    kViewWidth = 256, kViewHeight = 224,   /* the 4:3 view at the rendered camera $59/$5B */
};

#define kModPackage "soe.enhancement.widescreen"
#define kModFeature "experimental"

static struct { int on, cap, enemies_only; } s_mod;
static struct { int read, off, free_off, reserve, debug; } s_env;
static int s_steered;   /* $1076 holds our value: put vanilla's back when E6 stops */

static uint16_t W(uint32_t a) { return (uint16_t)(g_ram[a] | g_ram[a + 1] << 8); }
static void SetW(uint32_t a, uint16_t v) { g_ram[a] = (uint8_t)v; g_ram[a + 1] = (uint8_t)(v >> 8); }

static int EnvInt(const char *name, int dflt)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) : dflt;
}

static void ReadEnv(void)
{
    if (s_env.read) return;
    s_env.read = 1;
    s_env.off = EnvInt("SOE_WS_ALIVE", 1) == 0;
    s_env.free_off = EnvInt("SOE_WS_FREE", 1) == 0;
    s_env.reserve = EnvInt("SOE_WS_ALIVE_RESERVE", 4);
    s_env.debug = EnvInt("SOE_WS_ALIVE_DEBUG", 0);
}

int SoeAliveEnabled(void)
{
    ReadEnv();
    return s_mod.on && !s_env.off;
}

int SoeAliveFreeTime(void)
{
    return SoeAliveEnabled() && !s_env.free_off;
}

static int Cap(void) { return s_mod.cap > 0 ? s_mod.cap : 6; }

/* Counts against the cap: everything, as vanilla's budget does, or only what is not friendly ($0002) or
 * on the party's side ($0004). */
static int Counted(uint32_t e) { return !s_mod.enemies_only || !(W(e + kFlags) & 0x0006); }

static int InView(uint32_t e, uint16_t cam_x, uint16_t cam_y)
{
    int dx = (int16_t)(W(e + kX) - cam_x), dy = (int16_t)(W(e + kY) - cam_y);
    return dx >= 0 && dx < kViewWidth && dy >= 0 && dy < kViewHeight;
}

/* Awake and counted characters in the 4:3 view, the Boy and the Dog among them. */
static int ViewCount(uint16_t cam_x, uint16_t cam_y)
{
    int n = 0;
    if (!s_mod.enemies_only)
        for (uint32_t p = kBoy; p <= kDog; p += kDog - kBoy) n += (W(p + kFlags) & kAwake) && InView(p, cam_x, cam_y);
    for (uint32_t e = kSlotFirst; e < kSlotEnd; e += kStride)
        if ((W(e + kFlags) & kAwake) && Counted(e) && InView(e, cam_x, cam_y)) n++;
    return n;
}

static int FreeSlots(void)
{
    int n = 0;
    for (uint32_t e = kSlotFirst; e < kSlotEnd; e += kStride) n += W(e + kFlags) == 0;
    return n;
}

/* Record +$07 bit 0 (character record in $8E, offset >= $8000 for every live entity). */
static int Segmented(uint32_t e)
{
    uint16_t at = (uint16_t)(W(e + kRecord) + 7);
    if (!(at & 0x8000)) return 0;
    const uint8_t *p = RomPtr(0xCE0000u | at);
    return p && (*p & 1);
}

static int SegmentListFree(void)
{
    for (uint32_t x = kSegmentLists; x < kSegmentEnd; x += kSegmentStride)
        if (W(x) == 0) return 1;
    return 0;
}

/* The budget: the spawner stops while only `reserve` slots are free. */
static void SteerBudget(void)
{
    int room = FreeSlots() - s_env.reserve;
    SetW(kBudget, (uint16_t)(W(kAwakeCount) + (room > 0 ? room : 0)));
    s_steered = 1;
}

static void Debug(uint16_t cam_x, uint16_t cam_y)
{
    static uint16_t s_last;
    const uint16_t frame = W(kFrameCounter);
    if (!s_env.debug || frame == s_last || frame % 60) return;
    s_last = frame;
    fprintf(stderr, "[alive] frame %u: room %u, in view %d/%d, awake %u, asleep %u, free %d, budget %u\n",
            frame, W(kRoomId), ViewCount(cam_x, cam_y), Cap(), W(kAwakeCount), W(0x1072), FreeSlots(), W(kBudget));
}

uint16_t SoeAliveDecide(uint16_t slot, uint16_t cam_x, uint16_t cam_y)
{
    if (!SoeAliveEnabled() || slot < kSlotFirst || slot >= kSlotEnd) {
        if (s_steered) { SetW(kBudget, kVanillaBudget); s_steered = 0; }
        return kAliveRom;
    }
    const uint32_t e = slot;
    const uint16_t flags = W(e + kFlags);
    SteerBudget();
    Debug(cam_x, cam_y);
    if (flags & kAwake) return kAliveKeep;
    if (!(flags & kAsleep)) return kAliveRom;
    if (W(e + kHp) == 0 || (flags & 0x0078) || !(W(e + kNode) & 0x8000) || W(e + kSpawnRecord))
        return kAliveRom;
    if (Segmented(e) && !SegmentListFree()) return kAliveKeep;
    return kAliveWake;
}

int SoeAliveMayAggro(uint16_t slot, uint16_t cam_x, uint16_t cam_y)
{
    if (!SoeAliveEnabled() || slot < kSlotFirst || slot >= kSlotEnd) return 1;
    return InView(slot, cam_x, cam_y) || ViewCount(cam_x, cam_y) < Cap();
}

static void ModReset(void)
{
    memset(&s_mod, 0, sizeof s_mod);
}

static void ModActivate(void)
{
    char v[32];
    s_mod.on = 1;
    s_mod.cap = 6;
    if (snes_mod_runtime_feature_option_value_c(kModPackage, kModFeature, "cap", v, sizeof v)) s_mod.cap = atoi(v);
    s_mod.enemies_only = snes_mod_runtime_feature_option_value_c(kModPackage, kModFeature, "count", v, sizeof v) &&
                         !strcmp(v, "enemies");
}

/* Changes the game: registered as an ordinary plugin. */
SNES_MOD_CONSTRUCTOR(soe_register_alive_plugin)
{
    (void)snes_mod_register_reset_callback(ModReset);
    (void)snes_mod_register_activation_plugin("soe.widescreen.alive", ModActivate);
}
