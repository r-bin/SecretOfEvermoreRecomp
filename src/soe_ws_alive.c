/*
 * Widescreen experiment E6: the room alive around the view, at most `cap` entities in the 4:3 view
 * (docs/widescreen/enemies.md, "E6 as built"). This CHANGES the game: an ordinary plugin, never
 * presentation-only, active only while the launcher's "Widescreen (experimental)" feature is on.
 *
 * The hook is $8F:C40F entity_sleep_wake (src/soe_bindings.c, HleEntitySleepWake). Vanilla lets an entity
 * sleep when it is 48 px off the 4:3 screen and the awake budget $1076 is full. Here:
 *   1 the active area is the wide view plus `radius` px on every side; inside it nothing sleeps for distance and
 *     every sleeper wakes, outside it (plus 32 px, so nothing flickers at the edge) entities sleep as in vanilla
 *     (a spawner-made one goes back to its record, $8F:DF41);
 *   2 the radius follows the frame time, as the game's own lag rule follows it with the budget: 4 NMIs without
 *     a logic frame ($0100 against $0B19) within 64 (vanilla misses one now and then: 5 in 1200 frames of the flower field) bring it in to the
 *     farthest awake entity less a quarter, 120 clean frames grow it by 32 px, from the wide view + 48 px up to
 *     the whole room; a room starts at 256 px. So the game keeps its speed, and what the player can see never freezes;
 *   3 the 4:3 view takes at most `cap` counted entities: one that reaches the door (vanilla's wake window,
 *     16 px around it) while it is full is held, i.e. put to sleep by the game's own path, and drawn grey
 *     (src/soe_ws_sprites.c); whoever the round robin ($0EBC, 6 slots a frame) visits first gets a freed place;
 *   4 the budget $1076 = $1070 + (free slots - reserve): the spawner ($8F:DDBB, unchanged) stops while only
 *     `reserve` slots are free, so scripts that create entities find one; the lag rule's budget is overruled;
 *   5 exempt (never held or put to sleep, still counted): +$10 & $0030, HP 0, +$6C >= 0; asleep with & $0078,
 *     HP 0 or a spawner record (+$8C) goes the ROM's way;
 *   6 too many in the view (the camera moved onto them): the counted ones farthest from the Boy sleep.
 * A segmented body (record +$07 bit 0) wakes only while one of the ten segment lists at $58AF is free: a
 * wake without one leaves $1070/$1072 changed ($8F:C4D6).
 *
 * The radius is the only state kept here (reset with each room); everything else follows from WRAM at the call.
 * The ROM code does every change of state; this file only picks the branch.
 *
 * Settings: the mod feature "experimental" (options cap, count, far). SOE_WS_ALIVE=0 switches E6 off (kill
 * switch); nothing switches it on but the feature. Test knobs: SOE_WS_ALIVE_RESERVE=<n> (default 4),
 * SOE_WS_ALIVE_DEBUG=1 prints count, budget and radius every 60th frame.
 */
#include "soe_ws_alive.h"
#include "soe_widescreen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "mod_runtime.h"
#include "widescreen.h"

enum {
    kSlotFirst = 0x3DE5, kSlotEnd = 0x4E89, kStride = 0x8E,   /* the 30 slots the round robin visits */
    kBoy = 0x4E89, kDog = 0x4F37,
    kAwakeCount = 0x1070, kBudget = 0x1076, kFrameCounter = 0x0100, kFieldCounter = 0x0B19, kRoomId = 0x0ADB,
    kSegmentLists = 0x58AF, kSegmentEnd = 0x5DC3, kSegmentStride = 0x82,
    kVanillaBudget = 6,
    /* entity fields */
    kFlags = 0x10, kX = 0x1A, kY = 0x1C, kHp = 0x2A, kNode = 0x6C, kRecord = 0x60, kSpawnRecord = 0x8C,
    kAwake = 0x0100, kAsleep = 0x0200,
    /* the door: vanilla's wake window [-16, 272) around the 4:3 view, 224 lines tall here */
    kDoorLeft = -16, kDoorRight = 272, kDoorTop = -16, kDoorBottom = 240,
    /* the active area's margin beyond the wide view: vanilla's sleep distance at least, the room at most */
    kRadiusMin = 48, kRadiusStart = 256, kRadiusMax = 4096, kHysteresis = 32,
    kGrowAfter = 120, kGrowStep = 32,
    kLagWindow = 64, kLagShrink = 4,   /* missed NMIs within the last 64 that count as the game slowing down */
};

#define kModPackage "soe.enhancement.widescreen"
#define kModFeature "experimental"

static struct { int on, cap, enemies_only, always; } s_mod;
static struct { int read, off, reserve, debug; } s_env;
static int s_steered;   /* $1076 holds our value: put vanilla's back when E6 stops */
static struct { uint16_t room, nmi, field; int radius, clean, started; uint64_t missed; } s_load;

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
    s_env.reserve = EnvInt("SOE_WS_ALIVE_RESERVE", 4);
    s_env.debug = EnvInt("SOE_WS_ALIVE_DEBUG", 0);
}

int SoeAliveEnabled(void)
{
    ReadEnv();
    return s_mod.on && !s_env.off;
}

static int Cap(void) { return s_mod.cap > 0 ? s_mod.cap : 6; }

/* Counts against the cap: everything, as vanilla's budget does, or only what is not friendly ($0002) or
 * on the party's side ($0004). */
static int Counted(uint32_t e) { return !s_mod.enemies_only || !(W(e + kFlags) & 0x0006); }

static int InDoor(uint32_t e, uint16_t cam_x, uint16_t cam_y)
{
    int dx = (int16_t)(W(e + kX) - cam_x), dy = (int16_t)(W(e + kY) - cam_y);
    return dx >= kDoorLeft && dx < kDoorRight && dy >= kDoorTop && dy < kDoorBottom;
}

/* Inside the wide view (g_ws_extra px each side of the 4:3 one) grown by `grow` px on every side. */
static int InArea(uint32_t e, uint16_t cam_x, uint16_t cam_y, int grow)
{
    int dx = (int16_t)(W(e + kX) - cam_x), dy = (int16_t)(W(e + kY) - cam_y);
    int side = g_ws_extra > 0 ? g_ws_extra : 0;
    return dx >= -side - grow && dx < 256 + side + grow && dy >= -grow && dy < 224 + grow;
}

/* How far outside the wide view `e` is (0 inside), the larger of the two axes. */
static int Beyond(uint32_t e, uint16_t cam_x, uint16_t cam_y)
{
    int dx = (int16_t)(W(e + kX) - cam_x), dy = (int16_t)(W(e + kY) - cam_y);
    int side = g_ws_extra > 0 ? g_ws_extra : 0;
    int bx = dx < -side ? -side - dx : dx >= 256 + side ? dx - (256 + side) + 1 : 0;
    int by = dy < 0 ? -dy : dy >= 224 ? dy - 224 + 1 : 0;
    return bx > by ? bx : by;
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
    if (!s_mod.enemies_only) n += !!(W(kBoy + kFlags) & kAwake) + !!(W(kDog + kFlags) & kAwake);
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

/* The farthest awake entity that the radius can put to sleep, measured as Beyond(). */
static int FarthestAwake(uint16_t cam_x, uint16_t cam_y)
{
    int far = 0;
    for (uint32_t e = kSlotFirst; e < kSlotEnd; e += kStride)
        if ((W(e + kFlags) & kAwake) && !ExemptAwake(e)) {
            int b = Beyond(e, cam_x, cam_y);
            if (b > far) far = b;
        }
    return far;
}

/* Rule 2. Missed NMIs are those the field logic did not keep up with: $0100 counts NMIs, $0B19 logic frames.
 * (The hook is not called every frame: the round robin visits empty slots too.) A new room starts at 256 px
 * and grows from there: waking the whole room at once made the first frames stutter. */
static void FollowFrameTime(uint16_t cam_x, uint16_t cam_y)
{
    const uint16_t room = W(kRoomId), nmi = W(kFrameCounter), field = W(kFieldCounter);
    if (!s_load.started || room != s_load.room) {
        memset(&s_load, 0, sizeof s_load);
        s_load.started = 1, s_load.room = room, s_load.nmi = nmi, s_load.field = field;
        s_load.radius = s_mod.always ? kRadiusMax : kRadiusStart;
        return;
    }
    if (field == s_load.field) return;
    const uint16_t nmis = (uint16_t)(nmi - s_load.nmi), frames = (uint16_t)(field - s_load.field);
    s_load.nmi = nmi, s_load.field = field;
    if (s_mod.always) { s_load.radius = kRadiusMax; return; }
    if (nmis >= 64 || frames >= 64 || nmis < frames) { s_load.missed = 0; s_load.clean = 0; return; }   /* a load, a menu */
    const int missed = nmis - frames;
    for (int i = 0; i < frames; i++) s_load.missed <<= 1;   /* one bit per NMI, 1 = missed */
    for (int i = 0; i < missed; i++) s_load.missed = s_load.missed << 1 | 1;
    s_load.clean = missed ? 0 : s_load.clean + frames;
    if (__builtin_popcountll(s_load.missed) >= kLagShrink) {
        int r = FarthestAwake(cam_x, cam_y);
        if (r > s_load.radius) r = s_load.radius;
        r = r - r / 4 - 16;
        if (r < kRadiusMin) r = kRadiusMin;
        if (s_env.debug) fprintf(stderr, "[alive] nmi %u: slowing down, radius %d -> %d\n", nmi, s_load.radius, r);
        s_load.radius = r;
        s_load.missed = 0;   /* judge the new radius on its own frames */
    } else if (s_load.clean >= kGrowAfter) {
        s_load.clean = 0;
        if (s_load.radius < kRadiusMax) s_load.radius += kGrowStep;
        if (s_env.debug) fprintf(stderr, "[alive] nmi %u: clean, radius %d\n", nmi, s_load.radius);
    }
}

static void Debug(uint16_t cam_x, uint16_t cam_y)
{
    static uint16_t s_last;
    const uint16_t frame = W(kFrameCounter);
    if (!s_env.debug || frame == s_last || frame % 60) return;
    s_last = frame;
    fprintf(stderr, "[alive] frame %u: room %u, in view %d/%d, awake %u, asleep %u, free %d, budget %u, radius %d\n",
            frame, W(kRoomId), ViewCount(cam_x, cam_y), Cap(), W(kAwakeCount), W(0x1072), FreeSlots(), W(kBudget),
            s_load.radius);
}

uint16_t SoeAliveDecide(uint16_t slot, uint16_t cam_x, uint16_t cam_y)
{
    if (!SoeAliveEnabled() || !SoeWsFieldGate() || slot < kSlotFirst || slot >= kSlotEnd) {
        if (s_steered) { SetW(kBudget, kVanillaBudget); s_steered = 0; }
        return kAliveRom;
    }
    const uint32_t e = slot;
    const uint16_t flags = W(e + kFlags);
    FollowFrameTime(cam_x, cam_y);
    SteerBudget();
    Debug(cam_x, cam_y);
    if (flags & kAwake) {
        if (ExemptAwake(e)) return kAliveKeep;
        if (!InArea(e, cam_x, cam_y, s_load.radius + kHysteresis)) return kAliveSleep;
        if (!Counted(e) || !InDoor(e, cam_x, cam_y)) return kAliveKeep;
        const int excess = ViewCount(cam_x, cam_y) - Cap();
        return excess > 0 && AmongFarthest(e, excess, cam_x, cam_y) ? kAliveSleep : kAliveKeep;
    }
    if (!(flags & kAsleep)) return kAliveRom;
    if (W(e + kHp) == 0 || (flags & 0x0078) || !(W(e + kNode) & 0x8000) || W(e + kSpawnRecord))
        return kAliveRom;
    if (!InArea(e, cam_x, cam_y, s_load.radius)) return kAliveKeep;
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
    s_mod.always = snes_mod_runtime_feature_option_value_c(kModPackage, kModFeature, "far", v, sizeof v) &&
                   !strcmp(v, "always");
}

/* Changes the game: registered as an ordinary plugin. */
SNES_MOD_CONSTRUCTOR(soe_register_alive_plugin)
{
    (void)snes_mod_register_reset_callback(ModReset);
    (void)snes_mod_register_activation_plugin("soe.widescreen.alive", ModActivate);
}
