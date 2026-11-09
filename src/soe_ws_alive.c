/*
 * Widescreen experiment E6: the whole room alive, at most `cap` entities in the 4:3 view
 * (docs/widescreen/enemies.md, "The whole room alive, six in the 4:3 view"). This CHANGES the game:
 * an ordinary plugin, never presentation-only, and no replay of a vanilla run stays in sync with it on.
 *
 * The hook is $8F:C40F entity_sleep_wake (src/soe_bindings.c, HleEntitySleepWake). Vanilla lets an entity
 * sleep only when it is far off screen and the awake budget $1076 is full; here nothing sleeps for distance,
 * and the view holds at most `cap` counted entities: one that reaches the door (vanilla's wake window,
 * 16 px around the 4:3 view) while the view is full is held, i.e. put to sleep by the game's own path. The
 * ROM code does every change of state; this file only picks the branch.
 *
 * Stateless: every decision follows from WRAM at the call, so save states and replays of the mode
 * reproduce it, and nothing needs resetting on a room change. Rules (numbers as in the doc):
 *   1 an awake entity stays awake wherever it is; a sleeper outside the door wakes;
 *   2 a sleeper inside the door wakes only while fewer than `cap` are counted there;
 *   3 who gets a freed place: whoever the round robin ($0EBC, 6 slots a frame) visits first;
 *   4 the budget $1076 = $1070 + (free slots - reserve): the spawner ($8F:DDBB, unchanged) stops while
 *     only `reserve` slots are free, so scripts that create entities find one; the lag rule is overruled;
 *   5 exempt (never held, still counted): +$10 & $0030, HP 0, +$6C >= 0; asleep with & $0078, HP 0 or a
 *     spawner record (+$8C) goes the ROM's way;
 *   6 too many in the view (the camera moved onto them): the counted ones farthest from the Boy sleep, a
 *     spawner-made one goes back to its record ($8F:DF41), as vanilla does with far ones.
 * A segmented body (record +$07 bit 0) wakes only while one of the ten segment lists at $58AF is free: a
 * wake without one leaves $1070/$1072 changed ($8F:C4D6).
 *
 * Settings: the mod feature "experimental" (options cap, count), SOE_WS_ALIVE=0|1 overrides it,
 * SOE_WS_ALIVE_CAP=<n>, SOE_WS_ALIVE_RESERVE=<n> (default 4), SOE_WS_ALIVE_COUNT=all|enemies,
 * SOE_WS_ALIVE_DEBUG=1 prints the count and budget every 60th frame.
 */
#include "soe_ws_alive.h"
#include "soe_widescreen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "mod_runtime.h"

enum {
    kSlotFirst = 0x3DE5, kSlotEnd = 0x4E89, kStride = 0x8E,   /* the 30 slots the round robin visits */
    kBoy = 0x4E89, kDog = 0x4F37,
    kAwakeCount = 0x1070, kBudget = 0x1076, kFrameCounter = 0x0100,
    kSegmentLists = 0x58AF, kSegmentEnd = 0x5DC3, kSegmentStride = 0x82,
    kVanillaBudget = 6,
    /* entity fields */
    kFlags = 0x10, kX = 0x1A, kY = 0x1C, kHp = 0x2A, kNode = 0x6C, kRecord = 0x60, kSpawnRecord = 0x8C,
    kAwake = 0x0100, kAsleep = 0x0200,
    /* the door: vanilla's wake window [-16, 272) around the 4:3 view, 224 lines tall here */
    kDoorLeft = -16, kDoorRight = 272, kDoorTop = -16, kDoorBottom = 240,
};

#define kModPackage "soe.enhancement.widescreen"
#define kModFeature "experimental"

static struct { int on, cap, enemies_only; } s_mod;
static struct { int read, on, cap, reserve, enemies_only, debug; } s_env;
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
    s_env.on = EnvInt("SOE_WS_ALIVE", -1);
    s_env.cap = EnvInt("SOE_WS_ALIVE_CAP", 0);
    s_env.reserve = EnvInt("SOE_WS_ALIVE_RESERVE", 4);
    const char *c = getenv("SOE_WS_ALIVE_COUNT");
    s_env.enemies_only = c && *c ? !strcmp(c, "enemies") : -1;
    s_env.debug = EnvInt("SOE_WS_ALIVE_DEBUG", 0);
}

int SoeAliveEnabled(void)
{
    ReadEnv();
    return s_env.on >= 0 ? s_env.on != 0 : s_mod.on;
}

static int Cap(void) { return s_env.cap > 0 ? s_env.cap : s_mod.cap > 0 ? s_mod.cap : 6; }
static int EnemiesOnly(void) { return s_env.enemies_only >= 0 ? s_env.enemies_only : s_mod.enemies_only; }

/* Counts against the cap: everything, as vanilla's budget does, or only what is not friendly ($0002) or
 * on the party's side ($0004). */
static int Counted(uint32_t e) { return !EnemiesOnly() || !(W(e + kFlags) & 0x0006); }

static int InDoor(uint32_t e, uint16_t cam_x, uint16_t cam_y)
{
    int dx = (int16_t)(W(e + kX) - cam_x), dy = (int16_t)(W(e + kY) - cam_y);
    return dx >= kDoorLeft && dx < kDoorRight && dy >= kDoorTop && dy < kDoorBottom;
}

/* Vanilla's own reasons to stay awake, but the budget ($8F:C419..$C42E). */
static int ExemptAwake(uint32_t e)
{
    return (W(e + kFlags) & 0x0030) || W(e + kHp) == 0 || !(W(e + kNode) & 0x8000);
}

static int32_t DistanceToBoy(uint32_t e)
{
    int32_t dx = (int16_t)(W(e + kX) - W(kBoy + kX)), dy = (int16_t)(W(e + kY) - W(kBoy + kY));
    return dx * dx + dy * dy;
}

/* Awake and counted inside the door; the Boy and the Dog wherever they are, as $1070 counts them. */
static int ViewCount(uint16_t cam_x, uint16_t cam_y)
{
    int n = 0;
    if (!EnemiesOnly()) n += !!(W(kBoy + kFlags) & kAwake) + !!(W(kDog + kFlags) & kAwake);
    for (uint32_t e = kSlotFirst; e < kSlotEnd; e += kStride)
        if ((W(e + kFlags) & kAwake) && Counted(e) && InDoor(e, cam_x, cam_y)) n++;
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

/* Rule 6: `e` is among the `excess` counted, non-exempt entities in the door farthest from the Boy
 * (equal distance: the higher slot counts as farther). */
static int AmongFarthest(uint32_t e, int excess, uint16_t cam_x, uint16_t cam_y)
{
    const int32_t d = DistanceToBoy(e);
    int farther = 0;
    for (uint32_t o = kSlotFirst; o < kSlotEnd; o += kStride) {
        if (o == e || !(W(o + kFlags) & kAwake) || !Counted(o) || ExemptAwake(o) || !InDoor(o, cam_x, cam_y)) continue;
        const int32_t od = DistanceToBoy(o);
        if (od > d || (od == d && o > e)) farther++;
    }
    return farther < excess;
}

/* Rule 4. */
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
    fprintf(stderr, "[alive] frame %u: room %u, in view %d/%d, awake %u, asleep %u, free %d, budget %u\n", frame,
            W(0x0ADB), ViewCount(cam_x, cam_y), Cap(), W(kAwakeCount), W(0x1072), FreeSlots(), W(kBudget));
}

uint16_t SoeAliveDecide(uint16_t slot, uint16_t cam_x, uint16_t cam_y)
{
    if (!SoeAliveEnabled() || !SoeWsFieldGate() || slot < kSlotFirst || slot >= kSlotEnd) {
        if (s_steered) { SetW(kBudget, kVanillaBudget); s_steered = 0; }
        return kAliveRom;
    }
    const uint32_t e = slot;
    const uint16_t flags = W(e + kFlags);
    SteerBudget();
    Debug(cam_x, cam_y);
    if (flags & kAwake) {
        if (ExemptAwake(e) || !Counted(e) || !InDoor(e, cam_x, cam_y)) return kAliveKeep;
        const int excess = ViewCount(cam_x, cam_y) - Cap();
        return excess > 0 && AmongFarthest(e, excess, cam_x, cam_y) ? kAliveSleep : kAliveKeep;
    }
    if (!(flags & kAsleep)) return kAliveRom;
    if (W(e + kHp) == 0 || (flags & 0x0078) || !(W(e + kNode) & 0x8000) || W(e + kSpawnRecord))
        return kAliveRom;
    if (Segmented(e) && !SegmentListFree()) return kAliveKeep;
    if (Counted(e) && InDoor(e, cam_x, cam_y) && ViewCount(cam_x, cam_y) >= Cap()) return kAliveKeep;
    return kAliveWake;
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
