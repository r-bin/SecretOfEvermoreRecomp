/*
 * Widescreen "extended" sprites: draw in the margins the entities the game does not put into OAM.
 *
 * The game never places a sprite at X >= 256 and lets far entities sleep, so in the margins the
 * vanilla picture has map but no characters. This draws them from WRAM, the way everscript-vscode's
 * emulator overlay does (src/games/evermore/emulator/webview/overlays/05-idle-sprites.js), as PPU
 * extra objects: composed like OBJ with a priority (canopy in front or behind), live CGRAM colours
 * (fades, brightness), and below every native OAM entry. Only margin pixels are drawn; the 4:3
 * centre is what the game drew. Presentation only: reads WRAM, the ROM and CGRAM, writes nothing.
 *
 * Facts (everscript-vscode docs/entities/, knowledge entries):
 *   entity lists  $3DDF awake, $3DE1 asleep, linked through +$5E; slots $3DE5..$4FE4 (stride $8E);
 *                 Boy $4E89 and Dog $4F37 are always drawn
 *   entity        +$06/+$08 current sprite (24-bit), +$09/+$0B secondary (the shadow), +$0C palette
 *                 slot, +$12 bit 15/14 hide main/secondary, +$18 plane, +$1A/+$1C/+$1E x/y/z,
 *                 +$3C collision word under the feet (depth, port of $8F:C773)
 *   projectiles   $6387, 8 slots of 44 bytes: +$06/+$08 sprite, +$0C palette slot, +$10 active,
 *                 +$14/+$16/+$18 x/y/z in 1/16 pixels
 *   sprite        $80:9033: count, offset of the first chunk; chunks of 5 bytes (flags, x:s8, y:s8,
 *                 block:u16); flags bit 0 16x16, 1-3 palette, 4-5 priority, 6/7 flip
 *   blocks        $EC:0000 + 3*id (16x16, bank $D9 + hi&$7F) / $D8:0000 + 3*id (8x8, bank $D1 + ..);
 *                 hi bit 7 = groups of 16 bytes behind a mask byte whose bits mark zero words
 *   attribute     $80:9433: (chunk flags & $CC) + the character's word, so the OAM palette is
 *                 ((flags & $0C) + (slot & $0E)) >> 1
 *   never animated an entity spawned but not yet reached by the camera waits in the asleep list
 *                 with sprite pointer 0 (not hidden: +$12 is clear). Drawn in its standing frame:
 *                 record $8E:(+$60), +$32 = stand id, animation record $C4:id (+3 flags: bit 7
 *                 eight facings at 2*facing, bit 6 four at $90:815B[facing]; $90:8124), facing +$22;
 *                 its script's first set-sprite command (0x22..0x2B: bank = cmd + $A8) is the frame
 *                 (everscript-vscode maps/character-animation.ts, resolveCharacterSprite)
 *   ghosts        widescreen E6 only: a spawn record (60 at $5DC3, 16 bytes) with a count left (+$0C) is drawn at
 *                 its point (+$02/+$04) as a dark silhouette of the largest frame seen on a live entity of its
 *                 character (record +$00; its standing frame until one is seen),
 *                 thinned by an ordered dither whose density follows the screen distance from the 4:3 edge (half
 *                 from 32 px out, none at the edge): it fades out as it comes into the 4:3 view and back in as it
 *                 leaves; when it spawns, the enemy takes its place. A placed enemy E6 holds on the room's load
 *                 (SoeAliveHeld) is drawn the same way where it stands, without its shadow
 *
 * Timing: the field drawn at begin_sim_frame shows the OAM built a frame earlier, while WRAM already
 * holds the next positions. So each frame draws the snapshot taken at the previous call, against
 * the camera the PPU draws with.
 */
#include "soe_ws_sprites.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "soe_ws_alive.h"
#include "snes/ppu.h"

enum {
    kAwakeHead = 0x3DDF, kAsleepHead = 0x3DE1,
    kSlotFirst = 0x3DE5, kSlotEnd = 0x4FE5,
    kBoy = 0x4E89, kDog = 0x4F37,
    kProjectiles = 0x6387, kProjectileCount = 8, kProjectileStride = 44,
    kSpawnFirst = 0x5DC3, kSpawnEnd = 0x6183, kSpawnStride = 0x10,
    kGhostColour = 0x1084,   /* BGR555 dark grey of a ghost's pixels */
    kGhostFade = 32,         /* px outside the 4:3 view over which a ghost fades in */
    kMaxItems = 96,          /* sprites (main, shadow, projectile, ghost) per frame */
    kMaxSide = 128,          /* largest sprite edge drawn */
};

typedef struct Item {
    uint32_t sprite;         /* 24-bit ROM address of the sprite record */
    int16_t x, y;            /* origin on screen (room position - camera) before the sprite's offsets */
    uint8_t palette_slot;    /* entity +$0C */
    uint8_t priority;        /* OBJ priority: 3 over the canopy, 2 under it */
    int16_t sort;            /* room y: lower is further back */
    uint8_t ghost;           /* a spawn record not spawned yet: a stippled silhouette */
} Item;

typedef struct Snapshot {
    int count;
    int16_t room;
    Item item[kMaxItems];
} Snapshot;

static Snapshot s_prev, s_next;
static int s_center = -1, s_debug, s_shift;
static PpuExtraObject s_objects[kMaxItems];
static uint16_t s_pixels[kMaxItems][kMaxSide * kMaxSide];

static uint16_t W(uint32_t a) { return (uint16_t)(g_ram[a] | g_ram[a + 1] << 8); }

/* ROM by bus address; $80-$BF:8000-FFFF mirror $C0-$FF (HiROM). */
static const uint8_t *Rom(uint32_t bus)
{
    uint32_t bank = bus >> 16;
    if (bank >= 0x80 && bank < 0xC0 && (bus & 0x8000)) bus += 0x400000;
    return (bus >> 16) >= 0xC0 && (bus >> 16) <= 0xFF ? RomPtr(bus) : NULL;
}
static int RomU8(uint32_t bus) { const uint8_t *p = Rom(bus); return p ? *p : -1; }
static int RomU16(uint32_t bus)
{
    const uint8_t *p = Rom(bus), *q = Rom(bus + 1);
    return p && q ? *p | *q << 8 : -1;
}

/* $8F:C773 ported (everscript-vscode src/games/evermore/maps/collision.ts spriteDepth). */
static int Depth(uint32_t e, uint8_t *priority)
{
    uint16_t cw = W(e + 0x3C);
    if (((cw ^ 0x0800) & 0x0F00) == 0) return 0;   /* gate nibble 8: not drawn */
    int tile_plane = cw & 0x30, own_plane = g_ram[e + 0x18] & 0x30;
    int front = own_plane != tile_plane ? own_plane < tile_plane : (cw & 0x1000) != 0;
    *priority = front ? 3 : 2;
    return 1;
}

static void Add(Snapshot *s, uint32_t sprite, int x, int y, uint8_t slot, uint8_t priority, int sort)
{
    if (s->count >= kMaxItems || (sprite >> 16) < 0xC0 || (sprite & 0xffff) < 3) return;
    s->item[s->count].sprite = sprite;
    s->item[s->count].x = (int16_t)x;
    s->item[s->count].y = (int16_t)y;
    s->item[s->count].palette_slot = slot;
    s->item[s->count].priority = priority;
    s->item[s->count].sort = (int16_t)sort;
    s->item[s->count].ghost = 0;
    s->count++;
}

/* Animation script command widths ($90:8000 handlers; everscript-vscode maps/animation-opcodes.ts),
 * 0 = unknown or a jump: the walk stops there rather than guess. Holds 0x01..0x1E and the
 * set-sprite family 0x22..0x2B are handled before the table. */
static int CommandLength(int c)
{
    static const uint8_t kLen[0x5D] = {
        [0x00] = 1, [0x1f] = 3, [0x20] = 2, [0x21] = 1, [0x2c] = 4, [0x2e] = 2, [0x2f] = 2, [0x30] = 1,
        [0x31] = 3, [0x32] = 4, [0x33] = 5, [0x34] = 6, [0x35] = 3, [0x36] = 4, [0x37] = 2, [0x38] = 2,
        [0x39] = 4, [0x3a] = 4, [0x3c] = 9, [0x3d] = 5, [0x3e] = 9, [0x3f] = 3, [0x40] = 3, [0x41] = 1,
        [0x42] = 2, [0x43] = 1, [0x44] = 1, [0x45] = 3, [0x46] = 3, [0x47] = 5, [0x48] = 1, [0x49] = 3,
        [0x4a] = 3, [0x4b] = 3, [0x4c] = 6, [0x4d] = 3, [0x4e] = 1, [0x4f] = 1, [0x50] = 5, [0x51] = 1,
        [0x52] = 1, [0x53] = 1, [0x54] = 3, [0x55] = 1, [0x56] = 3, [0x58] = 1, [0x59] = 7, [0x5a] = 2,
        [0x5b] = 1, [0x5c] = 1,
    };
    if (c >= 0x01 && c <= 0x1e) return 1;
    return c < (int)sizeof kLen ? kLen[c] : 0;
}

/* The sprite a character stands as at `facing`, or 0. Cached: records and scripts are ROM. */
static uint32_t StandingSprite(uint16_t record, int facing)
{
    static struct { uint16_t record; uint8_t facing; uint32_t sprite; } cache[64];
    static int used;
    facing &= 0x0e;
    for (int i = 0; i < used; i++)
        if (cache[i].record == record && cache[i].facing == facing) return cache[i].sprite;
    uint32_t sprite = 0, secondary = 0;
    int id = RomU16(0x8E0000u | (uint16_t)(record + 0x32));
    if (id > 0) {
        uint32_t anim = 0xC40000u + (uint32_t)id;
        int flags = RomU8(anim + 3);
        if (flags & 0x80) anim += 2u * (uint32_t)facing;
        else if (flags & 0x40) anim += (uint32_t)RomU16(0x90815Bu + (uint32_t)facing);
        int lo = RomU16(anim), bank = RomU8(anim + 2);
        uint32_t p = (uint32_t)bank << 16 | (uint32_t)lo;
        for (int i = 0; i < 64 && lo >= 0 && bank >= 0; i++) {
            int c = RomU8(p) & 0x7f;
            if (c < 0) break;
            if (c >= 0x22 && c <= 0x2b) { sprite = (uint32_t)(c + 0xA8) << 16 | (uint32_t)RomU16(p + 1); break; }
            if (c == 0x57) {   /* segmented: the first group's sprite */
                if (RomU8(p + 1) > 0) sprite = (uint32_t)RomU8(p + 5) << 16 | (uint32_t)RomU16(p + 3);
                break;
            }
            if (c == 0x2c && !secondary) secondary = (uint32_t)RomU8(p + 3) << 16 | (uint32_t)RomU16(p + 1);
            if (c == 0x2d) break;
            int len = CommandLength(c);
            if (!len || c == 0x3b) break;
            p += (uint32_t)len;
        }
    }
    if (!sprite) sprite = secondary;
    if (used < (int)(sizeof cache / sizeof cache[0])) {
        cache[used].record = record, cache[used].facing = (uint8_t)facing, cache[used].sprite = sprite;
        used++;
    }
    return sprite;
}

/* A sprite's bounding box area from its chunk table (count, offset, then 5 bytes per chunk: flags, x, y, block), or 0. */
static int SpriteArea(uint32_t sprite)
{
    int count = RomU8(sprite), first = RomU8(sprite + 1);
    if (!sprite || count <= 0 || first < 0 || count > 64) return 0;
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    for (int i = 0; i < count; i++) {
        uint32_t c = sprite + (uint32_t)first + 5u * (uint32_t)i;
        int flags = RomU8(c), cx = (int8_t)RomU8(c + 1), cy = (int8_t)RomU8(c + 2), n = (flags & 1) ? 16 : 8;
        if (flags < 0) return 0;
        if (cx < x0) x0 = cx;
        if (cy < y0) y0 = cy;
        if (cx + n > x1) x1 = cx + n;
        if (cy + n > y1) y1 = cy + n;
    }
    return (x1 - x0) * (y1 - y0);
}

/* The largest frame seen on a live entity of each character record: a ghost's silhouette. The standing frame alone
 * can be a poor one (a Wimpy Flower's is its burrowed patch on the ground). */
static struct { uint16_t record; uint32_t sprite; int area; } s_seen[64];

static void SeeSprite(uint16_t record, uint32_t sprite)
{
    int i = 0;
    while (i < 64 && s_seen[i].record && s_seen[i].record != record) i++;
    if (i == 64) return;
    if (s_seen[i].record && s_seen[i].sprite == sprite) return;
    const int area = SpriteArea(sprite);
    if (!s_seen[i].record || area > s_seen[i].area) s_seen[i].record = record, s_seen[i].sprite = sprite, s_seen[i].area = area;
}

static uint32_t GhostSprite(uint16_t record, int facing)
{
    for (int i = 0; i < 64 && s_seen[i].record; i++)
        if (s_seen[i].record == record) return s_seen[i].sprite;
    return StandingSprite(record, facing);
}

static void SnapshotEntity(Snapshot *s, uint32_t e)
{
    uint16_t hide = W(e + 0x12);
    uint32_t main = (uint32_t)g_ram[e + 0x08] << 16 | W(e + 0x06);
    uint32_t shadow = (uint32_t)g_ram[e + 0x0B] << 16 | W(e + 0x09);
    if (!main && !shadow && W(e + 0x60) >= 0x8000) main = StandingSprite(W(e + 0x60), g_ram[e + 0x22]);
    uint8_t priority;
    if (!Depth(e, &priority)) return;
    int x = (int16_t)W(e + 0x1A), y = (int16_t)W(e + 0x1C), z = (int16_t)W(e + 0x1E);
    uint8_t slot = g_ram[e + 0x0C];
    const uint16_t record = W(e + 0x60);
    if (SoeAliveHeld((uint16_t)e)) {
        main = GhostSprite(record, g_ram[e + 0x22]);
        const int before = s->count;
        Add(s, main, x, y - (z >> 4), slot, priority, y);
        if (s->count > before) s->item[before].ghost = 1;
        return;
    }
    if (record >= 0x8000 && W(e + 0x2A) && (W(e + 0x10) & 0x0100) && !(hide & 0x8000) && W(e + 0x06))
        SeeSprite(record, main);
    if (!(hide & 0x4000)) Add(s, shadow, x, y, slot, priority, y - 1);
    if (!(hide & 0x8000)) Add(s, main, x, y - (z >> 4), slot, priority, y);
}

/* Spawn records with a count left, as ghosts at their points. */
static void SnapshotGhosts(Snapshot *s)
{
    for (uint32_t r = kSpawnFirst; r < kSpawnEnd; r += kSpawnStride) {
        if (!W(r + 0x0C) || W(r) < 0x8000) continue;
        const uint32_t sprite = GhostSprite(W(r), 4);   /* facing down */
        const int x = (int16_t)W(r + 0x02), y = (int16_t)W(r + 0x04), before = s->count;
        Add(s, sprite, x, y, 0, 2, y);
        if (s->count > before) s->item[before].ghost = 1;
    }
}

/* Every live entity, both lists, then the Boy and the Dog, then the projectiles (and the ghosts). */
static void TakeSnapshot(Snapshot *s, int ghosts)
{
    uint8_t seen[(kSlotEnd - kSlotFirst) / 8 + 1];
    memset(seen, 0, sizeof seen);
    s->count = 0;
    s->room = (int16_t)W(0x0ADB);
    for (int list = 0; list < 2; list++) {
        uint16_t cur = W(list ? kAsleepHead : kAwakeHead);
        for (int guard = 0; guard < 40 && cur >= kSlotFirst && cur < kSlotEnd; guard++) {
            int bit = cur - kSlotFirst;
            if (seen[bit >> 3] & (1 << (bit & 7))) break;
            seen[bit >> 3] |= (uint8_t)(1 << (bit & 7));
            SnapshotEntity(s, cur);
            cur = W(cur + 0x5E);
        }
    }
    for (int k = 0; k < 2; k++) {
        uint16_t e = k ? kDog : kBoy;
        int bit = e - kSlotFirst;
        if (!(seen[bit >> 3] & (1 << (bit & 7)))) SnapshotEntity(s, e);
    }
    for (int p = 0; p < kProjectileCount; p++) {
        uint32_t e = kProjectiles + (uint32_t)p * kProjectileStride;
        if (!W(e + 0x10)) continue;
        int x = (int16_t)W(e + 0x14) >> 4, y = (int16_t)W(e + 0x16) >> 4, z = (int16_t)W(e + 0x18) >> 4;
        Add(s, (uint32_t)g_ram[e + 0x08] << 16 | W(e + 0x06), x, y - z, g_ram[e + 0x0C], 3, y);
    }
    if (ghosts) SnapshotGhosts(s);
}

/* One 8x8 or 16x16 block as 4-bit indices (row-major). */
static int DecodeBlock(int id, int large, uint8_t *out)
{
    const uint32_t table = large ? 0xEC0000 : 0xD80000;
    const int base_bank = large ? 0xD9 : 0xD1, bytes = large ? 128 : 32;
    int lo = RomU16(table + 3u * (uint32_t)id), hi = RomU8(table + 3u * (uint32_t)id + 2);
    if (lo < 0 || hi < 0) return 0;
    uint32_t p = (uint32_t)(base_bank + (hi & 0x7F)) << 16 | (uint32_t)lo;
    uint8_t raw[128];
    memset(raw, 0, sizeof raw);
    if (!(hi & 0x80)) {
        for (int i = 0; i < bytes; i++) { int v = RomU8(p + (uint32_t)i); if (v < 0) return 0; raw[i] = (uint8_t)v; }
    } else {
        for (int g = 0; g < bytes; g += 16) {
            int mask = RomU8(p++);
            if (mask < 0) return 0;
            for (int w = 0; w < 8; w++) {
                if (mask & (1 << w)) continue;
                int a = RomU8(p), b = RomU8(p + 1);
                if (a < 0 || b < 0) return 0;
                raw[g + 2 * w] = (uint8_t)a, raw[g + 2 * w + 1] = (uint8_t)b;
                p += 2;
            }
        }
    }
    const int size = large ? 16 : 8;
    for (int t = 0; t < (large ? 4 : 1); t++) {
        const uint8_t *tile = raw + 32 * t;
        int x0 = (t & 1) * 8, y0 = (t >> 1) * 8;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                int bit = 7 - c;
                int v = (tile[2 * r] >> bit & 1) | (tile[2 * r + 1] >> bit & 1) << 1 |
                        (tile[16 + 2 * r] >> bit & 1) << 2 | (tile[16 + 2 * r + 1] >> bit & 1) << 3;
                out[(y0 + r) * size + x0 + c] = (uint8_t)v;
            }
    }
    return size;
}

/* BGR555 to the grey of the same brightness (Rec. 601 weights). */
static uint16_t Grey(uint16_t bgr)
{
    int r = bgr & 31, g = bgr >> 5 & 31, b = bgr >> 10 & 31;
    int y = (r * 77 + g * 150 + b * 29 + 128) >> 8;
    return (uint16_t)(y | y << 5 | y << 10);
}

/* Compose one sprite into `px` (BGR555 | $8000, 0 = transparent); returns 0 when nothing to draw. */
static int Compose(const Item *it, int grey, uint16_t *px, int *ox, int *oy, int *w, int *h, int *math)
{
    int count = RomU8(it->sprite), first = RomU8(it->sprite + 1);
    if (count <= 0 || first < 0 || count > 64) return 0;
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    for (int i = 0; i < count; i++) {
        uint32_t c = it->sprite + (uint32_t)first + 5u * (uint32_t)i;
        int flags = RomU8(c), cx = (int8_t)RomU8(c + 1), cy = (int8_t)RomU8(c + 2), n = (flags & 1) ? 16 : 8;
        if (flags < 0) return 0;
        if (cx < x0) x0 = cx;
        if (cy < y0) y0 = cy;
        if (cx + n > x1) x1 = cx + n;
        if (cy + n > y1) y1 = cy + n;
    }
    *w = x1 - x0, *h = y1 - y0;
    if (*w <= 0 || *h <= 0 || *w > kMaxSide || *h > kMaxSide) return 0;
    memset(px, 0, sizeof(uint16_t) * (size_t)(*w) * (size_t)(*h));
    *math = 0;
    /* The first chunk ends up in front (lower OAM index): paint the last first. Within one
     * sprite the character's priority applies to every chunk (mask $CC). */
    for (int i = count - 1; i >= 0; i--) {
        uint32_t c = it->sprite + (uint32_t)first + 5u * (uint32_t)i;
        int flags = RomU8(c), cx = (int8_t)RomU8(c + 1), cy = (int8_t)RomU8(c + 2), block = RomU16(c + 3);
        uint8_t ix[256];
        int size = DecodeBlock(block, flags & 1, ix);
        if (!size) continue;
        int pal = (((flags & 0x0C) + (it->palette_slot & 0x0E)) >> 1) & 7;
        if (pal >= 4 && !it->ghost) *math = 1;
        for (int r = 0; r < size; r++)
            for (int q = 0; q < size; q++) {
                int v = ix[((flags & 0x80) ? size - 1 - r : r) * size + ((flags & 0x40) ? size - 1 - q : q)];
                if (!v) continue;
                const int dx = cx - x0 + q, dy = cy - y0 + r;
                uint16_t bgr = it->ghost ? kGhostColour : g_ppu->cgram[128 + pal * 16 + v] & 0x7FFF;
                if (grey && !it->ghost) bgr = Grey(bgr);
                px[dy * (*w) + dx] = (uint16_t)(0x8000 | bgr);
            }
    }
    *ox = -x0, *oy = -y0;
    return 1;
}

/* A ghost's opacity as a 4x4 ordered dither in screen space, so the pattern stays put while it moves: a pixel stays
 * where the Bayer threshold is below its density, half at kGhostFade px or more outside the 4:3 view, none inside. */
static void GhostFade(uint16_t *px, int w, int h, int sx, int sy)
{
    static const uint8_t kBayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    for (int r = 0; r < h; r++)
        for (int q = 0; q < w; q++) {
            const int x = sx + q, y = sy + r;
            const int out = x < 0 ? -x : x >= 256 ? x - 255 : 0;
            const int density = (out >= kGhostFade ? kGhostFade : out) * 8 / kGhostFade;   /* of 16 */
            if (kBayer[y & 3][x & 3] >= density) px[r * w + q] = 0;
        }
}

void SoeWsSpritesReset(void)
{
    s_prev.count = s_next.count = 0;
    if (g_ppu) PpuSetExtraObjects(g_ppu, NULL, 0);
}

void SoeWsSpritesFrame(int enabled, int grey, int ghosts, int cam_x, int cam_y, int left, int right)
{
    if (!g_ppu) return;
    if (s_center < 0) { const char *c = getenv("SOE_WS_SPRITES_CENTER"); s_center = c && *c == '1';
                      const char *d = getenv("SOE_WS_DEBUG"); s_debug = d && *d == '1';
                      const char *sh = getenv("SOE_WS_SPRITES_SHIFT"); s_shift = sh ? atoi(sh) : 0; }
    TakeSnapshot(&s_next, ghosts);
    PpuSetExtraObjects(g_ppu, NULL, 0);
    if (enabled && s_prev.room == s_next.room && (left || right)) {
        /* Back to front by room y; a lower order wins, so the front one gets the lowest. */
        int index[kMaxItems], n = s_prev.count;
        for (int i = 0; i < n; i++) index[i] = i;
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && s_prev.item[index[j]].sort < s_prev.item[index[j - 1]].sort; j--) {
                int t = index[j]; index[j] = index[j - 1]; index[j - 1] = t;
            }
        int drawn = 0;
        for (int k = 0; k < n; k++) {
            const Item *it = &s_prev.item[index[k]];
            int ox, oy, w, h, math;
            uint16_t *px = s_pixels[drawn];
            if (!Compose(it, grey, px, &ox, &oy, &w, &h, &math)) continue;
            int sx = it->x - ox - cam_x + s_shift, sy = it->y - oy - cam_y;
            if (sx + w <= -left || sx >= 256 + right || sy + h <= 0 || sy >= 224) continue;
            /* Margins only: the 4:3 centre is the game's own picture. SOE_WS_SPRITES_CENTER=1 draws
             * the centre too (under native OAM), to check the placement against the game's own;
             * SOE_WS_SPRITES_SHIFT=<px> moves them, the control that such a check can fail. */
            if (!s_center)
            for (int r = 0; r < h; r++)
                for (int q = 0; q < w; q++)
                    if (sx + q >= 0 && sx + q < 256) px[r * w + q] = 0;
            if (it->ghost) GhostFade(px, w, h, sx, sy);
            PpuExtraObject *o = &s_objects[drawn];
            o->x = sx, o->y = sy;
            o->width = (uint16_t)w, o->height = (uint16_t)h, o->stride = (uint16_t)w;
            o->priority = it->priority;
            o->math = (uint8_t)math;
            o->oamSlot = 127;
            o->order = (uint32_t)(n - k);
            o->pixels = px;
            drawn++;
        }
        PpuSetExtraObjects(g_ppu, s_objects, (size_t)drawn);
        static unsigned s_frames;
        if (s_debug && ++s_frames % 60 == 0)
            fprintf(stderr, "[widescreen] sprites: %d listed, %d drawn\n", n, drawn);
    }
    Snapshot t = s_prev; s_prev = s_next; s_next = t;
}
