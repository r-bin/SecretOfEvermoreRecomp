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
 *  - Spawners: a record's box (every 4:3 view that could hold its point: 256 x 224 either way) is "full" with
 *    `cap` - 2 enemies (4: vanilla's budget less the Boy and the Dog; `cap` with Count = enemies only), enemies placed
 *    or spawned, alive, not friendly.
 *    On a room's load (its first 90 logic frames, the fade-in; SoeAliveSpawnPick at the spawner $8F:DDBB) every record
 *    whose box is not full spawns wherever its point is, one per frame. Later the ROM decides when and where (timer,
 *    point 16..48 px off the 4:3 screen) and the spawn step $8F:DE43 (SoeAliveSpawnAllowed) lets it through only while
 *    the record's box is not full and the player's 4:3 view holds fewer than `cap` characters (as the aggro cap counts
 *    them). Records still to spawn are drawn as ghosts in the margins (src/soe_ws_sprites.c).
 *  - Placed enemies (an enter script's, +$8C = 0): most rooms have no spawner at all (the flower field 0x38 places
 *    7 Mosquitoes and 14 Wimpy Flowers), and vanilla keeps the far ones asleep where they stand. On the room's load
 *    they are taken nearest the Boy first; one whose box already holds `cap` - 2 enemies (placed ones taken, spawned
 *    ones) is held: put to sleep where it stands and drawn as a ghost like a spawn record. A held enemy wakes once it
 *    is 16 px or more outside the 4:3 view, its box has room and the 4:3 view holds fewer than `cap` characters: the
 *    same rule as the spawn step, so it turns from ghost to enemy where vanilla would pop one in.
 *
 * State: which placed enemies of the current room are held (per slot, with the character record to see a slot
 * reused); everything else follows from WRAM at the call. The ROM code does every change of state; this file only
 * picks the branch. Not gated on the widescreen field gate: a menu or a narrow room in between would let vanilla put
 * the room back to sleep (the hooks run only in the field tick anyway).
 *
 * Settings: the mod feature "experimental" (options cap, count). SOE_WS_ALIVE=0 switches E6 off (kill switch),
 * SOE_WS_FREE=0 only its free time; nothing switches E6 on but the feature. Test knobs: SOE_WS_ALIVE_RESERVE=<n>
 * (default 4), SOE_WS_ALIVE_DEBUG=1 prints count and budget every 60th frame, SOE_WS_ALIVE_AFTER=<$0B19> keeps E6 off
 * until that logic frame (a vanilla replay stays in sync up to there, then E6 takes over).
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
    kFlags = 0x10, kX = 0x1A, kY = 0x1C, kHp = 0x2A, kNode = 0x6C, kRecord = 0x60, kTarget = 0x24, kSpawnRecord = 0x8C,
    kAwake = 0x0100, kAsleep = 0x0200,
    kViewWidth = 256, kViewHeight = 224,   /* the 4:3 view at the rendered camera $59/$5B */
    kFieldCounter = 0x0B19, kRoomCap = 0x2435, kLiveCount = 0x1074,
    /* spawn records: 60 of 16 bytes, +$02/+$04 point, +$0C count left */
    kSpawnFirst = 0x5DC3, kSpawnEnd = 0x6183, kSpawnStride = 0x10, kSpawnX = 0x02, kSpawnY = 0x04, kSpawnCount = 0x0C,
    kEntryFrames = 90,       /* logic frames after a room change that count as its load (the fade-in) */
    kSlotCount = (kSlotEnd - kSlotFirst) / kStride,
    kWakeMargin = 16,        /* a held enemy wakes only this far outside the 4:3 view (the spawner's ring starts here) */
};

#define kModPackage "soe.enhancement.widescreen"
#define kModFeature "experimental"

static struct { int on, cap, enemies_only; } s_mod;
static struct { int read, off, free_off, reserve, debug; long after; } s_env;
static int s_steered;   /* $1076 holds our value: put vanilla's back when E6 stops */
/* The current room since its load: placed enemies taken (kTaken) or held (kHeld), by slot. */
enum { kUnseen = 0, kTaken, kHeld };
static struct { uint16_t room, since; uint8_t state[kSlotCount]; uint16_t record[kSlotCount]; } s_room = { .room = 0xFFFF };
/* Enemies whose last aggro check (SoeAliveMayAggro) let them take the party, by slot, with their character record:
 * +$24 alone is no marker (idle enemies hold a party pointer too). */
static struct { uint8_t on[kSlotCount]; uint16_t record[kSlotCount]; } s_chase;

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
    s_env.after = EnvInt("SOE_WS_ALIVE_AFTER", 0);
}

int SoeAliveEnabled(void)
{
    ReadEnv();
    return s_mod.on && !s_env.off && (!s_env.after || (long)(g_ram[0x0B19] | g_ram[0x0B1A] << 8) >= s_env.after);
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

/* Distance² from the Boy, for the order in which chasers keep their place. */
static long BoyDistance(uint32_t e)
{
    const long dx = (int16_t)(W(e + kX) - W(kBoy + kX)), dy = (int16_t)(W(e + kY) - W(kBoy + kY));
    return dx * dx + dy * dy;
}

static int Chases(uint32_t e)
{
    const int i = (int)(e - kSlotFirst) / kStride;
    const uint16_t t = W(e + kTarget);
    return s_chase.on[i] && s_chase.record[i] == W(e + kRecord) && (t == kBoy || t == kDog);
}

/* The crowd the party faces: ViewCount, plus enemies outside the 4:3 view that chase the Boy or the Dog (+$24) and,
 * when `before` is an enemy, are nearer the Boy than it (ties: the lower slot), so the nearest chasers keep their place
 * and the farthest lets go. Without it, every awake enemy of the room would chase while the view itself is not full
 * (the flower field's seven Mosquitoes all went for the Dog from across the room). */
static int Crowd(uint16_t cam_x, uint16_t cam_y, uint32_t before)
{
    int n = ViewCount(cam_x, cam_y);
    const long limit = before ? BoyDistance(before) : 0;
    for (uint32_t e = kSlotFirst; e < kSlotEnd; e += kStride) {
        const uint16_t f = W(e + kFlags);
        if (e == before || !(f & kAwake) || (f & 0x0006) || !Chases(e) || InView(e, cam_x, cam_y)) continue;
        if (before) {
            const long d = BoyDistance(e);
            if (d > limit || (d == limit && e > before)) continue;
        }
        n++;
    }
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

/* Logic frames since the room changed; a new room forgets the last one's held enemies. */
static uint16_t RoomFrames(void)
{
    const uint16_t room = W(kRoomId), field = W(kFieldCounter);
    if (room != s_room.room) {
        memset(&s_room, 0, sizeof s_room);
        memset(&s_chase, 0, sizeof s_chase);
        s_room.room = room, s_room.since = field;
    }
    return (uint16_t)(field - s_room.since);
}

static int Loading(void) { return RoomFrames() < kEntryFrames; }

static int SlotIndex(uint32_t e) { return (int)(e - kSlotFirst) / kStride; }

/* What the load made of the enemy in slot `e`; kUnseen once its slot holds another character. */
static int LoadState(uint32_t e)
{
    const int i = SlotIndex(e);
    return s_room.record[i] == W(e + kRecord) ? s_room.state[i] : kUnseen;
}

/* A hostile enemy the room's enter script placed, alive, awake or asleep, which the round robin would wake. */
static int Placed(uint32_t e)
{
    const uint16_t f = W(e + kFlags);
    return (f & (kAwake | kAsleep)) && !(f & 0x0006) && !(f & 0x0078) && W(e + kHp) && !W(e + kSpawnRecord) &&
           (W(e + kNode) & 0x8000) && !Segmented(e);
}

/* Enemies (placed or spawned, alive, not friendly or on the party's side) that could share a 4:3 view with (x, y):
 * within 256 x 224 of it either way. Held ones and, during the load, placed ones not yet taken do not count. A box
 * of one view around the point let nearly every record of the east jungle 0x5b spawn (18 of 18); this one lets 10. */
static int EnemiesAround(uint16_t x, uint16_t y)
{
    int n = 0;
    const int loading = Loading();
    for (uint32_t e = kSlotFirst; e < kSlotEnd; e += kStride) {
        const uint16_t f = W(e + kFlags);
        if (!(f & (kAwake | kAsleep)) || (f & 0x0006) || !W(e + kHp)) continue;
        const int state = LoadState(e);
        if (state == kHeld || (loading && state == kUnseen && Placed(e))) continue;
        int dx = (int16_t)(W(e + kX) - x), dy = (int16_t)(W(e + kY) - y);
        if (dx > -kViewWidth && dx < kViewWidth && dy > -kViewHeight && dy < kViewHeight) n++;
    }
    return n;
}

/* A box holds enough enemies: the cap less the Boy and the Dog, unless the cap counts enemies only. */
static int Crowded(uint16_t x, uint16_t y)
{
    const int limit = s_mod.enemies_only ? Cap() : Cap() - 2;
    return EnemiesAround(x, y) >= limit;
}

static int BoxFull(uint32_t r) { return Crowded(W(r + kSpawnX), W(r + kSpawnY)); }

/* During the load: takes the placed enemies seen for the first time, nearest the Boy first, holding each whose box is
 * crowded already. */
static void TakePlaced(void)
{
    if (!Loading()) return;
    const int bx = (int16_t)W(kBoy + kX), by = (int16_t)W(kBoy + kY);
    for (;;) {
        uint32_t best = 0;
        long best_d = 0;
        for (uint32_t e = kSlotFirst; e < kSlotEnd; e += kStride) {
            if (LoadState(e) != kUnseen || !Placed(e)) continue;
            const long dx = (int16_t)W(e + kX) - bx, dy = (int16_t)W(e + kY) - by, d = dx * dx + dy * dy;
            if (!best || d < best_d) best = e, best_d = d;
        }
        if (!best) return;
        const int i = SlotIndex(best);
        s_room.record[i] = W(best + kRecord);
        s_room.state[i] = Crowded(W(best + kX), W(best + kY)) ? kHeld : kTaken;
    }
}

/* A held enemy may wake: out of sight by the margin, its box has room, the 4:3 view is not crowded. */
static int HeldMayWake(uint32_t e, uint16_t cam_x, uint16_t cam_y)
{
    const int dx = (int16_t)(W(e + kX) - cam_x), dy = (int16_t)(W(e + kY) - cam_y);
    const int near = dx >= -kWakeMargin && dx < kViewWidth + kWakeMargin && dy >= -kWakeMargin &&
                     dy < kViewHeight + kWakeMargin;
    return !near && !Crowded(W(e + kX), W(e + kY)) && Crowd(cam_x, cam_y, 0) < Cap();
}

int SoeAliveHeld(uint16_t slot)
{
    if (!SoeAliveEnabled() || slot < kSlotFirst || slot >= kSlotEnd || (slot - kSlotFirst) % kStride) return 0;
    return LoadState(slot) == kHeld;
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
    if (LoadState(e) == kHeld) {
        if (flags & kAwake) {
            if (Loading()) return kAliveSleep;
            s_room.state[SlotIndex(e)] = kTaken;   /* woken by the game itself: let it be */
            return kAliveKeep;
        }
        if (!HeldMayWake(e, cam_x, cam_y)) return kAliveKeep;
        s_room.state[SlotIndex(e)] = kTaken;
    }
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
    const int may = InView(slot, cam_x, cam_y) || Crowd(cam_x, cam_y, slot) < Cap();
    if ((slot - kSlotFirst) % kStride == 0) {
        const int i = (slot - kSlotFirst) / kStride;
        s_chase.on[i] = (uint8_t)may, s_chase.record[i] = W((uint32_t)slot + kRecord);
    }
    return may;
}

int SoeAliveSpawnAllowed(uint16_t record, uint16_t cam_x, uint16_t cam_y)
{
    if (!SoeAliveEnabled()) return 1;
    return !BoxFull(record) && Crowd(cam_x, cam_y, 0) < Cap();
}

uint16_t SoeAliveSpawnPick(void)
{
    if (!SoeAliveEnabled()) return kAliveSpawnRom;
    if (!Loading()) return kAliveSpawnRom;   /* loaded: the ROM's spawner */
    SteerBudget();
    if (W(kAwakeCount) >= W(kBudget) || (int16_t)W(kLiveCount) >= (int16_t)W(kRoomCap)) return 0;
    for (uint32_t r = kSpawnFirst; r < kSpawnEnd; r += kSpawnStride)
        if (W(r + kSpawnCount) && !BoxFull(r)) return (uint16_t)r;
    return 0;
}

void SoeAliveFrame(void)
{
    if (!SoeAliveEnabled()) return;
    SteerBudget();
    TakePlaced();
}

static void ModReset(void)
{
    memset(&s_mod, 0, sizeof s_mod);
    memset(&s_room, 0, sizeof s_room);
    s_room.room = 0xFFFF;
    memset(&s_chase, 0, sizeof s_chase);
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
