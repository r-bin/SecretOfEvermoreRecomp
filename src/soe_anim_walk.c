/*
 * The strike an entity's last tick ran (soe_anim_walk.h).
 *
 * A strike lives for one tick and leaves nothing in WRAM, so each frame the commands the entity's last tick read are
 * replayed: from the previous resume pointer (+$00, bank +$02), or from the restart point +$03 when an animation
 * began, through the first command with bit 7, both ways at a branch. A path counts only when it ends where the
 * entity now stands: resume pointer after the last command or after a hold, or unchanged with the timer +$05 one
 * lower (everscript-vscode docs/entities/animation_script.md "The machine"; widths: maps/animation-opcodes.ts).
 */
#include "soe_anim_walk.h"

#include "common_rtl.h"

static uint8_t R8(uint32_t a) { return g_rom[(a & 0x3F0000) | (a & 0xFFFF)]; }
static uint16_t R16(uint32_t a) { return (uint16_t)(R8(a) | R8(a + 1) << 8); }

SoeAnimCursor SoeAnimCursorOf(uint16_t e)
{
    SoeAnimCursor c;
    c.anim = (uint32_t)(g_ram[e] | g_ram[e + 1] << 8) | (uint32_t)g_ram[e + 2] << 16;
    c.restart = (uint16_t)(g_ram[e + 3] | g_ram[e + 4] << 8);
    c.timer = g_ram[e + 5];
    return c;
}

/* Bytes of the animation command at p, or 0 when its width is unknown (maps/animation-opcodes.ts). */
static int CommandLength(uint32_t p)
{
    static const uint8_t kLen[0x65] = {
        [0x00] = 1, [0x1f] = 3, [0x20] = 2, [0x21] = 1, [0x2c] = 4, [0x2d] = 1, [0x2e] = 2, [0x2f] = 2,
        [0x30] = 1, [0x31] = 3, [0x32] = 4, [0x33] = 5, [0x34] = 6, [0x35] = 3, [0x36] = 4, [0x37] = 2,
        [0x38] = 2, [0x39] = 4, [0x3a] = 4, [0x3b] = 3, [0x3c] = 9, [0x3d] = 5, [0x3e] = 9, [0x3f] = 3,
        [0x40] = 3, [0x41] = 1, [0x42] = 2, [0x43] = 1, [0x44] = 1, [0x45] = 3, [0x46] = 3, [0x47] = 5,
        [0x48] = 1, [0x49] = 3, [0x4a] = 3, [0x4b] = 3, [0x4c] = 6, [0x4d] = 3, [0x4e] = 1, [0x4f] = 1,
        [0x50] = 5, [0x51] = 1, [0x52] = 1, [0x53] = 1, [0x54] = 3, [0x55] = 1, [0x56] = 3, [0x58] = 1,
        [0x59] = 7, [0x5a] = 2, [0x5b] = 1, [0x5c] = 1, [0x5d] = 1, [0x5e] = 25, [0x5f] = 4, [0x60] = 49,
        [0x61] = 13, [0x62] = 28, [0x63] = 13, [0x64] = 52,
    };
    const int c = R8(p) & 0x7f;
    if (c >= 0x01 && c <= 0x1e) return 1;     /* hold */
    if (c >= 0x22 && c <= 0x2b) return 3;     /* sprite */
    if (c == 0x57) {                          /* segments: a count, then [count sprite:3] groups */
        const int total = R8(p + 1);
        int assigned = 0, groups = 0;
        uint32_t q = p + 2;
        while (assigned < total && groups++ < 64) {
            const int n = R8(q);
            assigned += n ? n : total;
            q += 4;
        }
        return (int)(q - p);
    }
    return c < 0x65 ? kLen[c] : 0;
}

/* Runs one tick's commands from `p` the way the animation machine reads them, both ways at a branch. True
 * for a path that ends where `now` says the tick ended: its resume pointer after the last command or after a
 * hold, or unchanged with the frame timer counted down; *strike is the path's strike command, or 0. */
static int WalkTick(uint32_t p, const SoeAnimCursor *was, const SoeAnimCursor *now, uint32_t hold, int steps,
                    uint32_t *strike)
{
    const uint32_t bank = p & 0xFF0000;
    for (; steps < 64; steps++) {
        const uint8_t raw = R8(p);
        const int c = raw & 0x7f, len = CommandLength(p);
        if (!len) return 0;
        uint32_t next = p + (uint32_t)len;
        if (c == 0x47) *strike = p;
        if ((c >= 0x01 && c <= 0x1e) || c == 0x1f || c == 0x20) hold = next;
        if (c == 0x3b) next = bank | R16(p + 1);
        if (c == 0x2d) next = bank | now->restart;
        if ((c == 0x39 || c == 0x3a || c == 0x54) && !(raw & 0x80)) {
            uint32_t taken = *strike;
            if (WalkTick(bank | R16(p + (c == 0x54 ? 1 : 2)), was, now, hold, steps + 1, &taken)) {
                *strike = taken;
                return 1;
            }
        }
        if (raw & 0x80) {
            const uint16_t resume = (uint16_t)now->anim;
            return resume == (uint16_t)next || (hold && resume == (uint16_t)hold) ||
                   (now->anim == was->anim && now->timer + 1 == was->timer);
        }
        p = next;
    }
    return 0;
}

uint32_t SoeAnimTickStrike(const SoeAnimCursor *was, const SoeAnimCursor *now)
{
    if (was->anim == now->anim && was->timer == now->timer) return 0;   /* no tick this frame */
    const uint32_t starts[2] = { was->anim, (now->anim & 0xFF0000) | now->restart };
    for (int i = 0; i < 2; i++) {
        uint32_t strike = 0;
        if (WalkTick(starts[i], was, now, 0, 0, &strike)) return strike;
    }
    return 0;
}
