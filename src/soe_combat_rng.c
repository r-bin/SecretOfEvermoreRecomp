/*
 * Combat RNG: each roll of a fight drawn over the one it lands on, green when it went the player's way, red when
 * not. docs/mods/combat-rng.md.
 *
 * Facts (everscript-vscode docs/entities/combat.md §4-§6 and "Where the rolls are drawn"):
 *   generator     $80:859B leaves its result in $0133 as well as in A, so a roll seen at the generator's entry
 *                 is read back at the next entry or at the frame's end, with no CPU of our own
 *   callers       told apart by the return address the JSL left at S+1..S+3 (the byte before the next
 *                 instruction):
 *     $8F:BA9A    the to-hit roll in $8F:BA06: Y the target, DP $4C the attacker, DP $12 the threshold; a hit
 *                 when (r & $7FFF) < threshold, so the chance is threshold / 32768
 *     $80:AD6B    random_below, called from $8F:C08C (its return $8F:C08F at S+5..S+7, a PHB byte between):
 *                 the damage roll; DP $02 = n = base + 1, Y the target, DP $4C the attacker; offset =
 *                 n * r >> 16, damage = (2 * (offset + base) + base) / 4
 *     $90:8387    animation command hold_random ($90:837C): Y the entity, [DP $5D] -> [range, base]; the frame
 *                 lasts base + (range * (r & $FF) >> 8) ticks
 *   knockback     $8F:C2C4 starts the character record's +$40 animation through $90:8124 (X the id, Y the
 *                 entity); a hold_random before that entity's next animation start is its knockback's
 *   strike sweep  $8F:B5FC for a strike [dx dy w h] at the attacker + (dx, dy) ($9087BA), per target: the
 *                 other side only ((+$10 ^ attacker side) & 4), skipped when +$10 & $5002 (also $0020 when an
 *                 enemy strikes), HP 0, +$16 & $20, +$36 = the attacker (cleared 21 ticks after the roll set
 *                 it, $8F:B00F), +$14 & $070D; reached when 2(|y + 16 + (+$44) - sy| - r) < h and
 *                 2(|x + (+$42) - sx| - r) < w, r the word at record +$0D; then height and plane
 *
 * A strike leaves nothing in WRAM: the frame's strikes come from replaying each entity's tick
 * (src/soe_anim_walk.c), and the gates are read at the frame's end, after the tick. Positions may have moved by
 * a step since the strike, so a whiff's pixels are good to a pixel or two.
 *
 * Presentation only: the hooks read the CPU and WRAM, the labels go into the finished picture. Field frames only.
 * Key 9 (while the mod is on) cycles short, detailed, off. Test overrides: SOE_COMBAT_RNG=0|1,
 * SOE_COMBAT_RNG_DETAIL=short|detailed (replaces the option), SOE_COMBAT_RNG_LOG=1 (each label on stderr).
 */
#include "soe_combat_rng.h"
#include "soe_anim_walk.h"
#include "soe_widescreen.h"
#include "snes_osd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "cpu_state.h"
#include "desktop/sdl_compat.h"
#include "mod_runtime.h"
#include "snes/ppu.h"

#define kModPackage "soe.debug.combat_rng"
#define kModFeature "combat_rng"

enum {
    kLines = 224,
    kRngResult = 0x0133,
    kCellsW = 0x08EE, kCellsH = 0x08F0,
    kScrollBg2H = 0x0112, kScrollBg2V = 0x0114,
    kAwakeHead = 0x3DDF, kSlotFirst = 0x3DE5, kSlotEnd = 0x4FE5,
    kBoy = 0x4E89, kDog = 0x4F37,
    kFromHitRoll = 0x8FBA9A, kFromRandomBelow = 0x80AD6B, kFromDamage = 0x8FC08F, kFromHoldRandom = 0x908387,
    kMaxChars = 48, kMaxLabels = 48, kMaxRolls = 32,
    kLabelLife = 90, kLabelFade = 20, kLockTicks = 21, kWhiffReach = 32,
};

enum Mode { kOff, kShort, kDetailed };
enum Luck { kBad = -1, kNeutral = 0, kGood = 1 };

typedef struct { uint8_t r, g, b; } Rgb;
static const Rgb kInk[3] = { { 255, 85, 85 }, { 240, 240, 240 }, { 90, 230, 110 } };   /* bad, neutral, good */

static struct { int on, detailed; } s_mod;
static int s_hotkey_mode = -1;
static unsigned s_prev_keys = 0;
static int s_live = 0;          /* the hooks look: the mod on and a field frame */
static unsigned s_frame = 0;      /* the host frame number the rolls belong to */

/* ---- options ---------------------------------------------------------------------------------- */

static int OptionIs(const char *id, const char *value)
{
    char v[16];
    return snes_mod_runtime_feature_option_value_c(kModPackage, kModFeature, id, v, sizeof v) && !strcmp(v, value);
}

static void ModReset(void)
{
    memset(&s_mod, 0, sizeof s_mod);
    s_hotkey_mode = -1;
    s_prev_keys = 0;
    s_live = 0;
}

static void ModActivate(void)
{
    s_mod.on = 1;
    s_mod.detailed = OptionIs("detail", "detailed");
}

SNES_MOD_CONSTRUCTOR(soe_register_combat_rng_plugin)
{
    (void)snes_mod_register_reset_callback(ModReset);
    (void)snes_mod_register_presentation_plugin("soe.combat_rng", ModActivate);
}

static int Enabled(void)
{
    const char *on = getenv("SOE_COMBAT_RNG");
    if (on && *on && atoi(on) == 0) return 0;
    return (on && *on) || s_mod.on || snes_mod_runtime_feature_enabled_c(kModPackage, kModFeature);
}

static int ConfiguredMode(void)
{
    const char *detail = getenv("SOE_COMBAT_RNG_DETAIL");
    if (detail && *detail) return strcmp(detail, "detailed") ? kShort : kDetailed;
    if (snes_mod_runtime_feature_enabled_c(kModPackage, kModFeature))
        return OptionIs("detail", "detailed") ? kDetailed : kShort;
    return s_mod.detailed ? kDetailed : kShort;
}

static int Mode(void)
{
    if (!Enabled()) return kOff;
    return s_hotkey_mode >= 0 ? s_hotkey_mode : ConfiguredMode();
}

/* ---- memory ----------------------------------------------------------------------------------- */

static uint16_t W(uint32_t a) { return (uint16_t)(g_ram[a] | g_ram[a + 1] << 8); }
static uint8_t R8(uint32_t a) { return g_rom[(a & 0x3F0000) | (a & 0xFFFF)]; }
/* A byte as the CPU reads it through a long pointer: WRAM in $7E/$7F and below $2000 in the system banks. */
static uint8_t Bus8(uint32_t a)
{
    const uint8_t bank = (uint8_t)(a >> 16);
    if (bank == 0x7E || bank == 0x7F) return g_ram[a - 0x7E0000];
    if ((bank < 0x40 || (bank >= 0x80 && bank < 0xC0)) && (a & 0xFFFF) < 0x2000) return g_ram[a & 0x1FFF];
    return R8(a);
}
static uint8_t Bank8E(uint16_t a) { return a < 0x2000 ? g_ram[a] : R8(0x8E0000u | a); }
static uint16_t Bank8E16(uint16_t a) { return (uint16_t)(Bank8E(a) | Bank8E((uint16_t)(a + 1)) << 8); }
static uint16_t Dp16(const CpuState *cpu, uint8_t off)
{
    const uint16_t a = (uint16_t)(cpu->D + off);
    return a < 0x1FFF ? W(a) : 0;
}
/* The 24-bit return address a JSL left `depth` bytes above S. */
static uint32_t Return24(const CpuState *cpu, int depth)
{
    const uint16_t s = (uint16_t)(cpu->S + depth);
    if (s >= 0x1FFD) return 0;
    return g_ram[s + 1] | (uint32_t)g_ram[s + 2] << 8 | (uint32_t)g_ram[s + 3] << 16;
}

static int IsEntity(uint16_t e) { return e >= kSlotFirst && e < kSlotEnd; }
static int IsParty(uint16_t e) { return e == kBoy || e == kDog || (IsEntity(e) && (W(e + 0x10) & 0x0004)); }

static int DrawnScroll(uint16_t reg, uint16_t shadow)
{
    return (int16_t)shadow + (int)((reg - shadow + 512) & 1023) - 512;
}

static int IsFieldFrame(void)
{
    return g_ppu && !(g_ppu->inidisp & 0x80) && g_ppu->bgmode == 0x39 && g_ppu->bgXsc[0] == 0x00 &&
           g_ppu->bgXsc[1] == 0x04 && g_ram[kCellsW] != 0 && W(kCellsH) != 0;
}

/* ---- labels ----------------------------------------------------------------------------------- */

/* A label floats up from over its entity's head and fades. A new one starts above any it would cover. */
static struct {
    int count;
    struct { int16_t x, y; uint16_t slot; uint8_t life; char text[24]; Rgb ink; } l[kMaxLabels];
} s_labels;

static int TextWidth(const char *text) { return 6 * (int)strlen(text) - 1; }
/* Where a label is now, in room pixels: it rises a pixel every 4 frames. */
static int LabelY(int i) { return s_labels.l[i].y - (kLabelLife - s_labels.l[i].life) / 4; }

static void AddLabel(uint16_t slot, int luck, const char *text)
{
    if (!IsEntity(slot) || !*text) return;
    static int log = -1;
    if (log < 0) log = getenv("SOE_COMBAT_RNG_LOG") && atoi(getenv("SOE_COMBAT_RNG_LOG"));
    if (log) fprintf(stderr, "[combat_rng] frame %u entity $%04X: %s\n", s_frame, slot, text);
    if (s_labels.count >= kMaxLabels) memmove(&s_labels.l[0], &s_labels.l[1], sizeof s_labels.l[0] * --s_labels.count);
    const uint16_t record = W(slot + 0x60);
    const int r = record ? Bank8E(record + 0x0D) : 8, x = (int16_t)W(slot + 0x1A), w = TextWidth(text);
    int y = (int16_t)W(slot + 0x1C) - 24 - r;
    for (int moved = 1, tries = 0; moved && tries < 8; tries++) {
        moved = 0;
        for (int i = 0; i < s_labels.count; i++)
            if (abs(LabelY(i) - y) < 9 && 2 * abs(s_labels.l[i].x - x) < w + TextWidth(s_labels.l[i].text) + 4) {
                y = LabelY(i) - 9;
                moved = 1;
            }
    }
    s_labels.l[s_labels.count].x = (int16_t)x;
    s_labels.l[s_labels.count].y = (int16_t)y;
    s_labels.l[s_labels.count].slot = slot;
    s_labels.l[s_labels.count].life = kLabelLife;
    snprintf(s_labels.l[s_labels.count].text, sizeof s_labels.l[0].text, "%s", text);
    s_labels.l[s_labels.count].ink = kInk[luck + 1];
    s_labels.count++;
}

/* From the point of view of the party: a roll good for the attacker is good when the attacker is the party's. */
static int PartyLuck(uint16_t attacker, int good_for_attacker)
{
    return IsParty(attacker) ? good_for_attacker : -good_for_attacker;
}

/* ---- rolls ------------------------------------------------------------------------------------ */

enum RollKind { kNoRoll, kHitRoll, kDamageRoll, kKnockbackRoll };
static struct { int kind; uint16_t attacker, target, a, b; } s_pending;
/* The rolls of the last ticks, so the strike sweep leaves a pair it already saw alone. */
static struct { int count; struct { uint16_t attacker, target; unsigned frame; } r[kMaxRolls]; } s_rolls;
/* Entities in knockback: from the start of their record's +$40 animation to their next animation start. */
static struct { int count; uint16_t slot[kMaxChars]; } s_knocked;

static void NoteRoll(uint16_t attacker, uint16_t target)
{
    if (s_rolls.count >= kMaxRolls) memmove(&s_rolls.r[0], &s_rolls.r[1], sizeof s_rolls.r[0] * --s_rolls.count);
    s_rolls.r[s_rolls.count].attacker = attacker;
    s_rolls.r[s_rolls.count].target = target;
    s_rolls.r[s_rolls.count].frame = s_frame;
    s_rolls.count++;
}

static int RolledSince(uint16_t attacker, uint16_t target, unsigned frames)
{
    for (int i = 0; i < s_rolls.count; i++)
        if (s_rolls.r[i].attacker == attacker && s_rolls.r[i].target == target && s_frame - s_rolls.r[i].frame <= frames)
            return 1;
    return 0;
}

static int Knocked(uint16_t e)
{
    for (int i = 0; i < s_knocked.count; i++)
        if (s_knocked.slot[i] == e) return i;
    return -1;
}

static void Resolve(int mode)
{
    const int kind = s_pending.kind;
    s_pending.kind = kNoRoll;
    if (kind == kNoRoll || mode == kOff) return;
    const uint16_t r = W(kRngResult), attacker = s_pending.attacker, target = s_pending.target;
    char text[24];
    if (kind == kHitRoll) {
        const unsigned threshold = s_pending.a, roll = r & 0x7FFF;
        const int hit = roll < threshold, chance = (int)((threshold * 100 + 16384) >> 15);
        NoteRoll(attacker, target);
        if (mode == kDetailed)
            snprintf(text, sizeof text, "%s %d%% ROLL %d", hit ? "HIT" : "MISS", chance, (int)((roll * 100) >> 15));
        else if (!hit || threshold < 0x7FFF)
            snprintf(text, sizeof text, "%s", hit ? "HIT" : "MISS");
        else
            return;   /* a certain hit says nothing */
        AddLabel(target, PartyLuck(attacker, hit ? kGood : kBad), text);
    } else if (kind == kDamageRoll) {
        const unsigned n = s_pending.a, base = n - 1, offset = (n * r) >> 16;
        const int dmg = (int)((2 * (offset + base) + base) >> 2), lo = (int)((3 * base) >> 2);
        const int hi = (int)((2 * (2 * base) + base) >> 2), pct = base ? (int)(offset * 100 / base) : 50;
        const int luck = PartyLuck(attacker, pct >= 67 ? kGood : pct <= 33 ? kBad : kNeutral);
        if (mode == kDetailed) snprintf(text, sizeof text, "DMG %d (%d-%d)", dmg, lo, hi);
        else if (pct >= 75 || pct <= 25) snprintf(text, sizeof text, "%s DMG", pct >= 75 ? "HIGH" : "LOW");
        else return;
        AddLabel(target, luck, text);
    } else if (kind == kKnockbackRoll) {
        const unsigned range = s_pending.a, base = s_pending.b, extra = (range * (r & 0xFF)) >> 8;
        const int pct = range > 1 ? (int)(extra * 100 / (range - 1)) : 50;
        const int luck = (IsParty(target) ? -1 : 1) * (pct >= 67 ? kBad : pct <= 33 ? kGood : kNeutral);
        if (mode == kDetailed)
            snprintf(text, sizeof text, "KB +%u (%u-%u)", base + extra, base, base + (range ? range - 1 : 0));
        else
            snprintf(text, sizeof text, "%s", pct >= 67 ? "LONG KB" : pct <= 33 ? "SHORT KB" : "KB");
        AddLabel(target, luck, text);
    }
}

void SoeCombatRngOnRandom(const CpuState *cpu)
{
    if (!s_live) return;
    Resolve(Mode());
    const uint32_t from = Return24(cpu, 0);
    if (from == kFromHitRoll) {
        s_pending.kind = kHitRoll;
        s_pending.a = Dp16(cpu, 0x12);
    } else if (from == kFromRandomBelow && Return24(cpu, 4) == kFromDamage) {
        s_pending.kind = kDamageRoll;
        s_pending.a = Dp16(cpu, 0x02);
    } else if (from == kFromHoldRandom && Knocked(cpu->Y) >= 0) {
        const uint16_t lo = Dp16(cpu, 0x5D);
        const uint32_t p = lo | (uint32_t)(Dp16(cpu, 0x5F) & 0xFF) << 16;
        s_pending.kind = kKnockbackRoll;
        s_pending.a = Bus8(p);
        s_pending.b = Bus8(p + 1);
    } else {
        return;
    }
    s_pending.target = cpu->Y;
    s_pending.attacker = Dp16(cpu, 0x4C);
}

void SoeCombatRngOnAnimStart(const CpuState *cpu)
{
    if (!s_live || !IsEntity(cpu->Y)) return;
    const uint16_t e = cpu->Y, record = W(e + 0x60);
    const int i = Knocked(e);
    if (record && cpu->X == Bank8E16((uint16_t)(record + 0x40))) {
        if (i < 0 && s_knocked.count < kMaxChars) s_knocked.slot[s_knocked.count++] = e;
    } else if (i >= 0) {
        s_knocked.slot[i] = s_knocked.slot[--s_knocked.count];
    }
}

/* ---- strikes that rolled nothing -------------------------------------------------------------- */

static struct { int count; struct { uint16_t slot; SoeAnimCursor cursor; } c[kMaxChars]; } s_prev;
/* Gate labels already shown: a (attacker, target, reason) shows once per lockout, about one attack. */
static struct { int count; struct { uint16_t attacker, target; uint8_t reason; unsigned frame; } g[kMaxRolls]; } s_gates;

static int GateShown(uint16_t attacker, uint16_t target, int reason)
{
    for (int i = 0; i < s_gates.count; i++)
        if (s_gates.g[i].attacker == attacker && s_gates.g[i].target == target && s_gates.g[i].reason == reason &&
            s_frame - s_gates.g[i].frame <= kLockTicks)
            return 1;
    if (s_gates.count >= kMaxRolls) memmove(&s_gates.g[0], &s_gates.g[1], sizeof s_gates.g[0] * --s_gates.count);
    s_gates.g[s_gates.count].attacker = attacker;
    s_gates.g[s_gates.count].target = target;
    s_gates.g[s_gates.count].reason = (uint8_t)reason;
    s_gates.g[s_gates.count].frame = s_frame;
    s_gates.count++;
    return 0;
}

/* Pixels the strike falls short on one axis: 0 when 2(|d| - r) < size reaches. */
static int Short(int d, int r, int size)
{
    const int e = 2 * (abs(d) - r) - size;
    return e < 0 ? 0 : e / 2 + 1;
}

enum Gate { kWhiff, kUntargetable, kInvulnerable, kLocked, kState, kLevel, kHeight };
static const char *const kGateText[] = { "WHIFF", "UNTARGETABLE", "INVULNERABLE", "LOCKED", "BUSY", "OTHER LEVEL", "HEIGHT" };

/* Why the sweep would pass over a target the strike's box reaches ($8F:B5FC's order). */
static int Gate(uint16_t attacker, uint16_t t, uint16_t plane)
{
    const uint16_t flags = W(t + 0x10);
    if (flags & (0x5002 | (attacker < kBoy ? 0x0020 : 0))) return kUntargetable;
    if (W(t + 0x16) & 0x0020) return kInvulnerable;
    if (W(t + 0x36) == attacker) return kLocked;
    if (W(t + 0x14) & 0x070D) return kState;
    if (!(plane & 0x40) && !(W(t + 0x18) & 0x40) && W(t + 0x18) != plane) return kLevel;
    return kHeight;
}

static void Strike(int mode, uint16_t attacker, uint32_t at)
{
    const int sx = (int16_t)W(attacker + 0x1A) + (int8_t)R8(at + 1), sy = (int16_t)W(attacker + 0x1C) + (int8_t)R8(at + 2);
    const int w = R8(at + 3), h = R8(at + 4);
    const uint16_t plane = W(attacker + 0x18), side = (W(attacker + 0x10) ^ 4) & 4;
    uint16_t nearest = 0;
    int nearest_px = kWhiffReach + 1, any = 0;
    uint16_t t = W(kAwakeHead);
    for (int guard = 0; guard < kMaxChars && IsEntity(t); guard++, t = W(t + 0x5E)) {
        if (((W(t + 0x10) ^ side) & 4) || W(t + 0x2A) == 0) continue;
        if (RolledSince(attacker, t, 1)) { any = 1; continue; }
        const uint16_t record = W(t + 0x60);
        const int r = record ? Bank8E16((uint16_t)(record + 0x0D)) : 0;
        const int px_y = Short((int16_t)W(t + 0x1C) + 16 + (int16_t)W(t + 0x44) - sy, r, h);
        const int px_x = Short((int16_t)W(t + 0x1A) + (int16_t)W(t + 0x42) - sx, r, w);
        const int px = px_x > px_y ? px_x : px_y;
        if (px) {
            if (px < nearest_px && !(W(t + 0x10) & 0x5002)) { nearest_px = px; nearest = t; }
            continue;
        }
        any = 1;
        const int gate = Gate(attacker, t, plane);
        if (gate == kLocked && mode != kDetailed) continue;   /* the same attack's later strike frames */
        if (GateShown(attacker, t, gate)) continue;
        char text[24];
        if (mode == kShort) snprintf(text, sizeof text, "IMMUNE");
        else if (gate == kLocked) snprintf(text, sizeof text, "LOCKED %dF", (int16_t)W(t + 0x38) + 1);
        else snprintf(text, sizeof text, "%s", kGateText[gate]);
        AddLabel(t, PartyLuck(attacker, kBad), text);
    }
    if (any || !nearest || (mode == kShort && GateShown(attacker, nearest, kWhiff))) return;   /* short: once an attack */
    char text[24];
    if (mode == kDetailed) snprintf(text, sizeof text, "WHIFF %dPX", nearest_px);
    else snprintf(text, sizeof text, "WHIFF");
    AddLabel(nearest, PartyLuck(attacker, kBad), text);
}

/* Each awake entity's tick of this frame against the frame before: its strike, if it ran one. */
static void Strikes(int mode)
{
    struct { int count; struct { uint16_t slot; SoeAnimCursor cursor; } c[kMaxChars]; } now = { 0 };
    uint16_t e = W(kAwakeHead);
    for (int guard = 0; guard < kMaxChars && IsEntity(e); guard++, e = W(e + 0x5E)) {
        now.c[now.count].slot = e;
        now.c[now.count].cursor = SoeAnimCursorOf(e);
        for (int i = 0; i < s_prev.count; i++)
            if (s_prev.c[i].slot == e) {
                const uint32_t at = SoeAnimTickStrike(&s_prev.c[i].cursor, &now.c[now.count].cursor);
                if (at) Strike(mode, e, at);
                break;
            }
        now.count++;
    }
    memcpy(&s_prev, &now, sizeof now);
}

/* ---- drawing ---------------------------------------------------------------------------------- */

static struct { uint32_t *px; int w, extra, ws_left, ws_right; } s_out;

static void Blend(int sx, int sy, Rgb c, int alpha256)
{
    const int x = sx + s_out.extra;
    if (x < 0 || x >= s_out.w || sy < 0 || sy >= kLines) return;
    if (x < s_out.extra - s_out.ws_left || x >= s_out.extra + 256 + s_out.ws_right) return;
    uint32_t *p = &s_out.px[(size_t)sy * s_out.w + x];
    const uint32_t d = *p;
    const int r = (int)(d >> 16 & 255), g = (int)(d >> 8 & 255), b = (int)(d & 255);
    *p = 0xFF000000u | (uint32_t)(r + ((c.r - r) * alpha256 >> 8)) << 16 |
         (uint32_t)(g + ((c.g - g) * alpha256 >> 8)) << 8 | (uint32_t)(b + ((c.b - b) * alpha256 >> 8));
}

/* Our own 5x7 letters, one byte per row, bit 4 the leftmost pixel. */
static const char kGlyphs[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%+-()/";
static const uint8_t kFont[][7] = {
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 },
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
    { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 }, { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 },
    { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }, { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 },
    { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }, { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 },
};

static int GlyphInk(const char *text, int px, int py)
{
    if (py < 0 || py >= 7 || px < 0) return 0;
    const int i = px / 6, col = px % 6;
    if (i >= (int)strlen(text) || col >= 5) return 0;
    const char *g = text[i] ? strchr(kGlyphs, text[i]) : NULL;
    return g && (kFont[g - kGlyphs][py] >> (4 - col) & 1);
}

/* Each glyph pixel in its ink, ringed by a 1-pixel black outline, centred on x. */
static void Text(int x, int y, const char *text, Rgb ink, int alpha256)
{
    const Rgb black = { 0, 0, 0 };
    const int w = TextWidth(text), x0 = x - w / 2;
    for (int py = -1; py <= 7; py++)
        for (int px = -1; px <= w; px++) {
            int on = GlyphInk(text, px, py), edge = 0;
            for (int dy = -1; dy <= 1 && !on && !edge; dy++)
                for (int dx = -1; dx <= 1; dx++) edge |= GlyphInk(text, px + dx, py + dy);
            if (on || edge) Blend(x0 + px, y + py, on ? ink : black, (on ? 256 : 200) * alpha256 >> 8);
        }
}

static void Labels(int cam_x, int cam_y)
{
    int kept = 0;
    for (int i = 0; i < s_labels.count; i++) {
        const int alpha = s_labels.l[i].life >= kLabelFade ? 256 : 256 * s_labels.l[i].life / kLabelFade;
        Text(s_labels.l[i].x - cam_x, LabelY(i) - cam_y, s_labels.l[i].text, s_labels.l[i].ink, alpha);
        if (--s_labels.l[i].life) s_labels.l[kept++] = s_labels.l[i];
    }
    s_labels.count = kept;
}

/* ---- hotkey ----------------------------------------------------------------------------------- */

/* Key 9, on the press: short, detailed, off, around. */
static void Hotkey(void)
{
    const uint8_t *keyboard = snesrecomp_sdl_get_keyboard_state();
    const unsigned keys = keyboard && keyboard[SDL_SCANCODE_9];
    if (keys && !s_prev_keys) {
        static const char *const kNames[3] = { "Combat RNG: off", "Combat RNG: short", "Combat RNG: detailed" };
        const int mode = s_hotkey_mode >= 0 ? s_hotkey_mode : ConfiguredMode();
        s_hotkey_mode = mode == kShort ? kDetailed : mode == kDetailed ? kOff : kShort;
        snes_osd_push(kNames[s_hotkey_mode], 0);
    }
    s_prev_keys = keys;
}

/* ---- frame ------------------------------------------------------------------------------------ */

static int s_frame_w = 256;

void SoeCombatRngPrepareFrame(int frame_w)
{
    s_frame_w = frame_w;
}

void SoeCombatRngEndFrame(const uint8_t *field, unsigned number)
{
    const int field_frame = IsFieldFrame();
    if (field_frame && Enabled()) Hotkey();
    const int mode = Mode();
    Resolve(mode);
    s_live = mode != kOff && field_frame;
    if (!field || !s_live) {
        s_labels.count = s_prev.count = s_knocked.count = 0;
        s_frame = number + 1;
        return;
    }
    Strikes(mode);
    s_out.px = (uint32_t *)(uintptr_t)field;
    s_out.w = s_frame_w;
    s_out.extra = (s_frame_w - 256) / 2;
    SoeWsMargins(&s_out.ws_left, &s_out.ws_right);
    Labels(DrawnScroll(g_ppu->hScroll[1], W(kScrollBg2H)), DrawnScroll(g_ppu->vScroll[1], W(kScrollBg2V)) + 1);
    s_frame = number + 1;   /* the rolls of the next simulated frame are that frame's */
}
