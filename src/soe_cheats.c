/*
 * Practice cheats on Start + a direction (docs/mods/cheats.md). This CHANGES the game: an ordinary plugin, active only
 * while the launcher's "Practice hotkeys" feature is on.
 *
 * The cheats are the practice ROM's (everscript repo in/practice/main.evs), written to the same WRAM; the two
 * toggles are the vanilla glitches it fakes:
 *  - no clip: the entity flag the script command `attribute(.., NO_CLIP)` sets, +$10 bit $0400 (byte +$11 bit $04),
 *    which the dog's wall-clip glitch leaves set;
 *  - Atlas glitch: attack and defence $EFFF in the stats block ($0A3F/$0A41 Boy, $0A89/$0A8B Dog) and the record
 *    (+$A0/+$A2), plus the glitch's invincibility, +$14 bit $08 (fake_atlas()).
 * Both are held every field frame while on (a room load or a level up rebuilds them); switched off, the flag is
 * cleared and each word gets back the game's own value, if it still holds ours (the practice ROM puts fixed $0010 /
 * $0000 back; the plugin's emulator cheats keep the game's value the same way, 01b-cheats.js).
 *  - all items: unlock(ITEM.ALL), once: money, every alchemy formula, charm, weapon and armour, 99 of each
 *    ingredient, consumable and trade good, full ammo;
 *  - weapon levels: unlock(ITEM.WEAPON_LEVEL_3) and DOG_LEVEL_3, once: every level word under level 3 set to it.
 *
 * The chord comes from $0EC6, the held buttons the field tick's joypad matcher ($8C:F121) just compared, in $4218
 * order. It fires once per press, only while the Boy is free to act (+$10 & $21 clear: not halted by a cutscene or a
 * text box), the gate of the everscript hotkey hack (hacks/hotkeys). Start does nothing else in the field.
 *
 * Settings: the mod feature "hotkeys" (options up, right, left, down, target). SOE_CHEATS=0 switches it off (kill
 * switch for the replay tests, which read the launcher's shared selection); SOE_CHEATS=1 switches it on with the
 * options' defaults whatever the launcher holds (the parity check with mods/mesen/soe.practice.cheats.lua).
 */
#include "soe_cheats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "mod_runtime.h"
#include "snes_osd.h"

enum {
    kBoy = 0x4E89, kDog = 0x4F37,   /* entity records */
    kFlags1 = 0x10, kFlags2 = 0x11, kFlags5 = 0x14, kAttackRec = 0xA0, kDefenceRec = 0xA2,
    kHalted = 0x21,                 /* +$10: $01 down, $20 disabled (control(NONE), walk(..)) */
    kNoClip = 0x04,                 /* +$11 */
    kAtlasInvincible = 0x08,        /* +$14 */
    kAtlasValue = 0xEFFF,
    kHeld = 0x0EC6,                 /* INPUT_P1_HELD_BUTTONS, $4218 order */
    kStart = 0x1000, kUp = 0x0800, kDown = 0x0400, kLeft = 0x0200, kRight = 0x0100,
    kItemMax = 99, kMoney = 6666666,
    kWeaponLevel3 = 0x0300,
};

enum Action { kNone, kNoClipToggle, kAtlasToggle, kAllItems, kWeaponLevels };
enum { kTargetBoy = 1, kTargetDog = 2 };

#define kModPackage "soe.practice.cheats"
#define kModFeature "hotkeys"

static struct {
    int on;
    int target;              /* kTargetBoy | kTargetDog */
    enum Action dir[4];      /* up, right, left, down */
} s_mod;
static struct { int noclip, atlas; uint16_t chord; } s_state;

/* The Atlas words and the game's own values while ours is held. */
static const uint16_t kAtlasBoy[] = { 0x0A3F, 0x0A41, kBoy + kAttackRec, kBoy + kDefenceRec };
static const uint16_t kAtlasDog[] = { 0x0A89, 0x0A8B, kDog + kAttackRec, kDog + kDefenceRec };
static struct { uint16_t value; uint8_t kept; } s_kept[2][4];

static uint16_t W(uint32_t a) { return (uint16_t)(g_ram[a] | g_ram[a + 1] << 8); }
static void SetW(uint32_t a, uint16_t v) { g_ram[a] = (uint8_t)v; g_ram[a + 1] = (uint8_t)(v >> 8); }
static void Fill(uint32_t from, uint32_t to, uint8_t v) { memset(&g_ram[from], v, to - from + 1); }

static int Targets(int who) { return (s_mod.target & who) != 0; }

static void HoldNoClip(int on)
{
    const uint16_t rec[2] = { kBoy, kDog };
    for (int i = 0; i < 2; i++) {
        if (!Targets(1 << i)) continue;
        uint8_t *f = &g_ram[rec[i] + kFlags2];
        *f = on ? *f | kNoClip : *f & (uint8_t)~kNoClip;
    }
}

static void HoldAtlas(int on)
{
    const uint16_t rec[2] = { kBoy, kDog };
    const uint16_t *words[2] = { kAtlasBoy, kAtlasDog };
    for (int i = 0; i < 2; i++) {
        if (!Targets(1 << i)) continue;
        for (int k = 0; k < 4; k++) {
            const uint16_t a = words[i][k];
            if (on) {
                if (W(a) != kAtlasValue) { s_kept[i][k].value = W(a); s_kept[i][k].kept = 1; SetW(a, kAtlasValue); }
            } else if (s_kept[i][k].kept) {
                if (W(a) == kAtlasValue) SetW(a, s_kept[i][k].value);
                s_kept[i][k].kept = 0;
            }
        }
        uint8_t *f = &g_ram[rec[i] + kFlags5];
        *f = on ? *f | kAtlasInvincible : *f & (uint8_t)~kAtlasInvincible;
    }
}

/* unlock(ITEM.ALL) (everscript in/core 08_items.evs). */
static void AllItems(void)
{
    static const uint16_t purses[] = { 0x0AC6, 0x0AC9, 0x0ACC, 0x0ACF };   /* talons, jewels, gold, credits: 24 bits */
    for (unsigned i = 0; i < sizeof purses / sizeof *purses; i++) {
        const uint32_t a = purses[i];
        if ((uint32_t)(g_ram[a] | g_ram[a + 1] << 8 | g_ram[a + 2] << 16) < kMoney) {
            g_ram[a] = (uint8_t)kMoney; g_ram[a + 1] = (uint8_t)(kMoney >> 8); g_ram[a + 2] = (uint8_t)(kMoney >> 16);
        }
    }
    Fill(0x2258, 0x225D, 0xFF);                     /* alchemy formulas */
    g_ram[0x2261] = 0xF4; Fill(0x2262, 0x2264, 0xFF);   /* charms (the low bits of $2261 are no charm) */
    Fill(0x22DA, 0x22DB, 0xFF);                     /* weapons */
    Fill(0x22FF, 0x2344, kItemMax);                 /* 22 ingredients, 8 consumables, 40 armour pieces */
    Fill(0x2345, 0x2347, 0xFF);                     /* bazooka ammo */
    for (uint32_t a = 0x2517; a <= 0x252F; a += 2) SetW(a, kItemMax);   /* 13 trade goods (words; $FF breaks the menu) */
}

/* unlock(ITEM.WEAPON_LEVEL_3) and DOG_LEVEL_3: level x $100 + progress, Boy $0ADD (fist) .. $0AF7 (bazooka), Dog $0B07. */
static void WeaponLevels(void)
{
    for (uint32_t a = 0x0ADD; a <= 0x0AF7; a += 2) if (W(a) < kWeaponLevel3) SetW(a, kWeaponLevel3);
    if (W(0x0B07) < kWeaponLevel3) SetW(0x0B07, kWeaponLevel3);
}

static void Run(enum Action a)
{
    switch (a) {
    case kNoClipToggle:
        s_state.noclip = !s_state.noclip;
        if (!s_state.noclip) HoldNoClip(0);
        snes_osd_push(s_state.noclip ? "No clip on" : "No clip off", 0);
        break;
    case kAtlasToggle:
        s_state.atlas = !s_state.atlas;
        if (!s_state.atlas) HoldAtlas(0);
        snes_osd_push(s_state.atlas ? "Atlas glitch on" : "Atlas glitch off", 0);
        break;
    case kAllItems: AllItems(); snes_osd_push("All items", 0); break;
    case kWeaponLevels: WeaponLevels(); snes_osd_push("Weapon levels 3", 0); break;
    case kNone: break;
    }
}

static void ModActivate(void);

void SoeCheatsFieldFrame(void)
{
    static int forced = -1;   /* SOE_CHEATS=1: on with the options' defaults, without the launcher (tools/compare) */
    if (forced < 0) {
        const char *e = getenv("SOE_CHEATS");
        forced = e && !strcmp(e, "1");
        if (forced && !s_mod.on) ModActivate();
    }
    if (!s_mod.on) return;
    const uint16_t held = W(kHeld);
    const uint16_t chord = (held & kStart) ? held & (kUp | kDown | kLeft | kRight) : 0;
    if (chord != s_state.chord && !(g_ram[kBoy + kFlags1] & kHalted)) {
        if (chord == kUp) Run(s_mod.dir[0]);
        else if (chord == kRight) Run(s_mod.dir[1]);
        else if (chord == kLeft) Run(s_mod.dir[2]);
        else if (chord == kDown) Run(s_mod.dir[3]);
    }
    s_state.chord = chord;
    if (s_state.noclip) HoldNoClip(1);
    if (s_state.atlas) HoldAtlas(1);
}

static enum Action OptionAction(const char *id, enum Action dflt)
{
    char v[32];
    if (!snes_mod_runtime_feature_option_value_c(kModPackage, kModFeature, id, v, sizeof v)) return dflt;
    if (!strcmp(v, "noclip")) return kNoClipToggle;
    if (!strcmp(v, "atlas")) return kAtlasToggle;
    if (!strcmp(v, "items")) return kAllItems;
    if (!strcmp(v, "levels")) return kWeaponLevels;
    return kNone;
}

static void ModReset(void)
{
    memset(&s_mod, 0, sizeof s_mod);
    memset(&s_state, 0, sizeof s_state);
    memset(s_kept, 0, sizeof s_kept);
}

static void ModActivate(void)
{
    const char *off = getenv("SOE_CHEATS");
    if (off && !strcmp(off, "0")) return;
    char v[32];
    s_mod.on = 1;
    s_mod.dir[0] = OptionAction("up", kNoClipToggle);
    s_mod.dir[1] = OptionAction("right", kAtlasToggle);
    s_mod.dir[2] = OptionAction("left", kAllItems);
    s_mod.dir[3] = OptionAction("down", kWeaponLevels);
    s_mod.target = kTargetBoy | kTargetDog;
    if (snes_mod_runtime_feature_option_value_c(kModPackage, kModFeature, "target", v, sizeof v))
        s_mod.target = !strcmp(v, "boy") ? kTargetBoy : !strcmp(v, "dog") ? kTargetDog : kTargetBoy | kTargetDog;
}

/* Changes the game: registered as an ordinary plugin. */
SNES_MOD_CONSTRUCTOR(soe_register_cheats_plugin)
{
    (void)snes_mod_register_reset_callback(ModReset);
    (void)snes_mod_register_activation_plugin("soe.cheats", ModActivate);
}
