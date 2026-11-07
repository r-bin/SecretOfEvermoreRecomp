/*
 * Secret of Evermore: game-specific runtime glue for the snesrecomp host.
 * Ours (MIT); the rest of src/ comes from the snesrecomp scaffold.
 *
 * What SoE needs from the host (docs/game-rtl.md):
 *   - Reset $80:8020 sets up the hardware, enables NMI + auto-joypad ($4200 = $81) and jumps to
 *     the main loop at $80:8B66, which never returns: the host cuts frames at vblank.
 *   - The NMI handler $80:823A copies the PPU shadows $7E0106-$0125 and counts frames in $7E0100.
 * The framework's beam-aligned driver does both (one frame = one PPU field, NMI at the vblank
 * edge), and boots SoE into the intro. SoE-specific frame handling goes here once needed.
 */

#include "game_rtl.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "beam_frame_driver.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "desktop/config.h"
#include "snes/apu.h"
#include "snes/cart.h"
#include "snes/dsp.h"
#include "snes/interp_bridge.h"
#include "snes/joypad.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"
#include "soe_frame_driver.h"

extern CpuState g_cpu;
extern Snes *g_snes;
extern uint8_t g_ram[0x20000];

/*
 * tools/compare (docs/debugging.md). The key is the number of NMIs counted since power-on: the increases of the
 * frame counter $7E0100, from the 0 the reset code writes. The game zeroes $0100 again when the attract demo loops,
 * so the raw counter is not a key; a drop counts as a restart from 0.
 * - SOE_COMPARE_STRIDE: hash memory each time the key reaches a new multiple of the stride and write
 *   "H <key> <crc> ..." to fd 3, one CRC-32 per region of SOE_COMPARE_REGIONS (wram, vram, cgram, oam, sram, aram,
 *   dsp, sound, picture; tools/compare/README.md). aram, dsp and sound need SOE_COMPARE_AT=nmi: they
 *   are read at the SPC700's next instruction on a copy of the APU (CmpSpcEnter). picture is a word hash, not a CRC.
 * - SOE_COMPARE_DUMP=<key>: write "D <key>\n" and the raw regions once.
 * - SOE_COMPARE_PEEK: WRAM offsets (hex, "a-b" ranges, comma separated) written as "P <frame> <key> <master clock> <hex bytes>"
 *   after every emulated frame (frame 0 = the first from power-on): a probe for a few bytes frame by frame.
 * - SOE_COMPARE_QUIT_AT=<key>: exit once the key reaches it (a replay's script cannot wait for a key).
 * - SOE_COMPARE_AT=nmi: sample (hash, dump, quit) as each NMI is taken instead of after each frame; the key is
 *   then the NMI's number since power-on. An instruction boundary, the same on MesenCE; a frame's end is not:
 *   the recomp's CPU finishes the instruction that crossed V=225, MesenCE stops inside it.
 *   SOE_COMPARE_NMI_LOG adds "N <nmi> <master clock> <interrupted pc24>" per NMI: where cycles part.
 *   SOE_COMPARE_NMI_BEAM with it: "[nmibeam] <nmi> <H clock> <line>" on stderr (a probe: it walks the beam there).
 * - SOE_COMPARE_SAVE_ROOT=<dir>: battery RAM from that folder (empty: cleared SRAM), see SoeAfterConfig.
 * - SOE_COMPARE_MESEN_STATE=<file>: start from a MesenCE state (src/soe_mesen_state.c).
 * - SOE_COMPARE_PADS=<file>: replay input, a little-endian pad word per MesenCE field (soe_compare_pad).
 * - SOE_COMPARE_WRAM=, _SRAM=, _ARAM=, _DSP=, _HW=<file>: the memory, S-DSP registers and what else survives a reset
 *   of a running console (a MesenCE movie's), loaded before the first instruction (soe_compare_start_memory).
 * Nothing else touches the disk: compare reads fd 3 from a pipe.
 */
enum { CMP_WRAM, CMP_VRAM, CMP_CGRAM, CMP_OAM, CMP_SRAM, CMP_ARAM, CMP_DSP, CMP_SOUND, CMP_PICTURE, CMP_REGIONS };
static struct {
    int state; /* 0 unread, 1 off, 2 on */
    unsigned stride;
    long dump_at;
    int regions[CMP_REGIONS], nregions;
    uint32_t peek[256];
    int npeek;
    long last, frames, quit_at;
    long raw_last, nmis; /* the last $0100 seen (-1 before the reset code's 0) and the key */
    int at_nmi;          /* SOE_COMPARE_AT=nmi */
    int nmi_log;         /* SOE_COMPARE_NMI_LOG */
    int nmi_beam;        /* SOE_COMPARE_NMI_BEAM */
    long nmis_taken;
    FILE *out;
} s_cmp;

static uint32_t s_crc_table[256];
static void CmpNmi(void);
static void CmpInit(void);

static uint32_t CmpCrc(uint32_t c, const uint8_t *p, size_t n) {
    if (!s_crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v >> 1) ^ (v & 1 ? 0xEDB88320u : 0);
            s_crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = s_crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

/* Replay input by field, not by host frame: a press of any length lands where MesenCE puts it, and a host frame
 * holding two fields moves nothing (docs/imperfections/host-frames-and-fields.md). MesenCE polls at line 225 of each
 * field, master clock 306900 + n * 357368 (a field's length varies by a few clocks; the rounding absorbs it), and
 * takes word n + 1 there: the file has the layout of the Lua script's pads. A word is the "BYsSudlrAXLR" pad, bit 15
 * first; the host's input word has B in bit 0. */
static uint64_t s_first_poll = 306900;
void soe_compare_pad_first(uint64_t clock) { s_first_poll = clock; }

void soe_compare_pad(uint64_t beam) {
    enum { FIELD = 357368 };
    static int state = -1;
    static uint16_t *pads;
    static size_t count;
    if (state < 0) {
        const char *path = getenv("SOE_COMPARE_PADS");
        FILE *f = path ? fopen(path, "rb") : NULL;
        state = f != NULL;
        if (!f) return;
        fseek(f, 0, SEEK_END);
        count = (size_t)ftell(f) / 2;
        rewind(f);
        pads = calloc(count + 1, 2);
        if (fread(pads, 2, count, f) != count) count = 0;
        fclose(f);
    }
    if (!state || beam + FIELD / 2 < s_first_poll) return;
    const size_t word = (size_t)((beam + FIELD / 2 - s_first_poll) / FIELD) + 1;
    const uint16_t pad = word < count ? pads[word] : 0;
    uint16_t input = 0;
    for (int i = 0; i < 12; i++) if (pad & (0x8000u >> i)) input |= (uint16_t)(1u << i);
    g_snes->input1_currentState = input;
    joypad_set_pad(0, input);
}

static void load_start(const char *variable, uint8_t *to, size_t size) {
    const char *path = getenv(variable);
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return;
    if (fread(to, 1, size, f) != size) fprintf(stderr, "%s: short file %s\n", variable, path);
    fclose(f);
}

/* A run from a MesenCE state takes its first NMI in the first host frame, before the frame hook reads the settings. */
void soe_compare_ready(void) { if (!s_cmp.state) CmpInit(); }

void soe_compare_start_memory(void) {
    load_start("SOE_COMPARE_WRAM", g_ram, 0x20000);
    load_start("SOE_COMPARE_SRAM", g_sram, (size_t)g_sram_size);
    soe_mesen_state_load_apu();
    /* FastROM on. */
    uint8_t hw;
    if (!getenv("SOE_COMPARE_HW")) return;
    load_start("SOE_COMPARE_HW", &hw, 1);
    g_memsel = hw;
    /* The running console's automatic reads have shifted its controller out: $4016 reads 1 at $80:848D, not a fresh 0. */
    joypad_auto_read(g_snes);
}

static void CmpInit(void) {
    const char *stride = getenv("SOE_COMPARE_STRIDE"), *dump = getenv("SOE_COMPARE_DUMP");
    const char *peek = getenv("SOE_COMPARE_PEEK"), *quit = getenv("SOE_COMPARE_QUIT_AT");
    s_cmp.state = 1;
    if (!stride && !dump && !peek) return;
    s_cmp.quit_at = quit ? strtol(quit, NULL, 10) : -1;
    const char *at = getenv("SOE_COMPARE_AT");
    s_cmp.at_nmi = at && !strcmp(at, "nmi");
    s_cmp.nmi_log = getenv("SOE_COMPARE_NMI_LOG") != NULL;
    s_cmp.nmi_beam = getenv("SOE_COMPARE_NMI_BEAM") != NULL;
    if (s_cmp.at_nmi) soe_frame_driver_nmi_hook = CmpNmi;
    s_cmp.raw_last = -1;
    s_cmp.out = fdopen(3, "wb");
    if (!s_cmp.out) return;
    s_cmp.stride = stride ? (unsigned)strtoul(stride, NULL, 10) : 0;
    s_cmp.dump_at = dump ? strtol(dump, NULL, 10) : -1;
    s_cmp.last = -1;
    const char *r = getenv("SOE_COMPARE_REGIONS");
    if (!r) r = "wram,vram";
    static const char *const names[] = { "wram", "vram", "cgram", "oam", "sram", "aram", "dsp", "sound", "picture" };
    for (const char *p = r; *p && s_cmp.nregions < CMP_REGIONS;) {
        for (int i = 0; i < CMP_REGIONS; i++)
            if (!strncmp(p, names[i], strlen(names[i]))) s_cmp.regions[s_cmp.nregions++] = i;
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }
    for (const char *p = peek; p && *p;) {
        char *end;
        unsigned long a = strtoul(p, &end, 16), b = a;
        if (*end == '-') b = strtoul(end + 1, &end, 16);
        for (unsigned long o = a; o <= b && o < 0x20000 && s_cmp.npeek < 256; o++) s_cmp.peek[s_cmp.npeek++] = (uint32_t)o;
        p = *end == ',' ? end + 1 : NULL;
    }
    /* Against MesenCE: its 4th MB is file page $200000 over and over (a loop in BaseCartridge counts pages in bytes);
     * a cartridge's is the 3rd MB, as ours. $80:D113 reads there (docs/imperfections/mesen-rom-mirror.md). */
    /* SOE_COMPARE_FADE: brightness scaled as MesenCE scales it (truncated, not rounded as snesrecomp does), so the picture
     * shows what else differs (docs/imperfections/fade-rounding.md). Before the first frame: the renderer caches it. */
    if (getenv("SOE_COMPARE_FADE")) g_ppu_brightness_truncate = 1;
    Cart *cart = g_snes->cart;
    if (getenv("SOE_COMPARE_MESEN_ROM_MIRROR") && cart->romSize == 0x400000)
        for (uint32_t o = 0x300000; o < 0x400000; o += 0x1000) memcpy(cart->rom + o, cart->rom + 0x200000, 0x1000);
    s_cmp.state = 2;
}

/*
 * The sound side at a key. Each side's SPC700 stops at its own place when an NMI is taken: MesenCE's runs in catch-up
 * batches, the last at the start of line 225, and can stop inside an instruction; ours has run an instruction at its
 * first clock and been synced up to the NMI's clock. So both read at the first instruction boundary at least
 * CMP_SPC_AHEAD cycles after the NMI's clock, past anything either has run. Here that is a run ahead on the APU as it
 * stands, undone afterwards, so a compare run plays as any other. An opcode here is done at its first clock, so
 * the last clock of the one before is that boundary with the S-DSP where MesenCE's stands (portClock reads 1 above its
 * cycle / 2 at the next opcode).
 */
#define CMP_SPC_RATIO ((double)(32040 * 64) / 21477270.0) /* Spc::UpdateClockRatio */
#define CMP_SPC_AHEAD 16
static struct { Apu apu; Spc spc; Dsp dsp; } s_spc_saved;

static void CmpSpcEnter(void) {
    Apu *apu = g_snes->apu;
    RtlApuLock();
    s_spc_saved.apu = *apu;
    s_spc_saved.spc = *apu->spc;
    s_spc_saved.dsp = *apu->dsp;
    apu->dsp->suppressOutput = true;
    const uint64_t half = (uint64_t)((double)g_cpu.master_cycles * CMP_SPC_RATIO);
    const uint64_t stop = (half + 1) / 2 + CMP_SPC_AHEAD;
    while (apu->portClock < stop || apu->cpuCyclesLeft != 1) apu_runCycles(apu, 1);
}

static void CmpSpcLeave(void) {
    Apu *apu = g_snes->apu;
    *apu->dsp = s_spc_saved.dsp;
    *apu->spc = s_spc_saved.spc;
    *apu = s_spc_saved.apu;
    RtlApuUnlock();
}

/* The S-DSP's state in MesenCE's terms, 32 bits a value; the order is SOUND_FIELDS in tools/compare/mesen.mts. */
enum { CMP_SOUND_WORDS = 30 + 8 * 20 };
static size_t CmpSound(uint8_t *out) {
    const DspClock *c = &g_snes->apu->dsp->clk;
    int32_t w[CMP_SOUND_WORDS], n = 0;
    w[n++] = c->step; w[n++] = c->counter; w[n++] = c->noise;
    w[n++] = c->out[0]; w[n++] = c->out[1]; w[n++] = c->voiceOut;
    for (int i = 0; i < 8; i++) { w[n++] = c->echoHist[i][0]; w[n++] = c->echoHist[i][1]; }
    w[n++] = c->echoHistPos; w[n++] = c->echoPtr; w[n++] = c->echoOff; w[n++] = c->echoLen;
    w[n++] = c->echoIn[0]; w[n++] = c->echoIn[1]; w[n++] = c->echoOut[0]; w[n++] = c->echoOut[1];
    for (int i = 0; i < 8; i++) {
        const DspClockVoice *v = &c->v[i];
        w[n++] = v->env; w[n++] = v->envPrev; w[n++] = v->pos; w[n++] = v->brrAdr;
        w[n++] = v->brrOff; w[n++] = v->konDelay; w[n++] = v->envx; w[n++] = v->bufPos;
        for (int k = 0; k < 12; k++) w[n++] = v->buf[k];
    }
    memcpy(out, w, sizeof(w));
    return sizeof(w);
}

/* The 224 lines drawn before this NMI as 0x00RRGGBB, as MesenCE's getScreenBuffer has them. */
enum { CMP_PICTURE_BYTES = 256 * 224 * 4 };
static size_t CmpPicture(uint8_t *out) {
    Ppu *ppu = g_snes->ppu;
    const size_t left = (size_t)ppu->extraLeftRight * 4;
    for (int y = 0; y < 224; y++) {
        const uint8_t *row = ppu->renderBuffer ? ppu->renderBuffer + (size_t)y * ppu->renderPitch + left : NULL;
        for (int x = 0; x < 256; x++) {
            uint8_t *o = out + ((size_t)y * 256 + (size_t)x) * 4;
            if (row) memcpy(o, row + (size_t)x * 4, 3); else memset(o, 0, 3);
            o[3] = 0;
        }
    }
    return CMP_PICTURE_BYTES;
}

/* A picture's hash: one multiply a pixel, which MesenCE's Lua can afford every frame. */
static uint32_t CmpWordHash(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i + 4 <= n; i += 4) {
        uint32_t w;
        memcpy(&w, p + i, 4);
        h = (h ^ w) * 16777619u;
    }
    return h;
}

static int CmpSpcSide(void) {
    for (int i = 0; i < s_cmp.nregions; i++)
        if (s_cmp.regions[i] == CMP_ARAM || s_cmp.regions[i] == CMP_DSP || s_cmp.regions[i] == CMP_SOUND) return s_cmp.at_nmi;
    return 0;
}

/* One region as bytes: the PPU keeps VRAM/CGRAM/OAM as little-endian words, the same bytes snes9x holds. */
static const uint8_t *CmpRegion(int r, size_t *n, uint8_t *scratch) {
    Ppu *ppu = g_snes->ppu;
    switch (r) {
    case CMP_WRAM: *n = 0x20000; return g_ram;
    case CMP_VRAM: *n = sizeof(ppu->vram); return (const uint8_t *)ppu->vram;
    case CMP_CGRAM: *n = sizeof(ppu->cgram); return (const uint8_t *)ppu->cgram;
    case CMP_SRAM: *n = (size_t)g_sram_size; return g_sram;
    case CMP_ARAM: *n = sizeof(g_snes->apu->ram); return g_snes->apu->ram;
    case CMP_DSP: *n = sizeof(g_snes->apu->dsp->ram); return g_snes->apu->dsp->ram;
    case CMP_SOUND: *n = CmpSound(scratch); return scratch;
    case CMP_PICTURE: *n = CmpPicture(scratch); return scratch;
    default:
        memcpy(scratch, ppu->oam, sizeof(ppu->oam));
        memcpy(scratch + sizeof(ppu->oam), ppu->highOam, sizeof(ppu->highOam));
        *n = sizeof(ppu->oam) + sizeof(ppu->highOam);
        return scratch;
    }
}

/* Hash, dump or quit at `key`, once per key. */
static void CmpAt(long key) {
    static uint8_t scratch[CMP_PICTURE_BYTES];
    if (key < 0 || key <= s_cmp.last) return;
    s_cmp.last = key;
    if (s_cmp.dump_at >= 0) {
        if (key < s_cmp.dump_at) return;
        fprintf(s_cmp.out, "D %ld\n", key);
        const int spc = CmpSpcSide();
        if (spc) CmpSpcEnter();
        for (int i = 0; i < s_cmp.nregions; i++) {
            size_t n;
            const uint8_t *p = CmpRegion(s_cmp.regions[i], &n, scratch);
            fwrite(p, 1, n, s_cmp.out);
        }
        if (spc) CmpSpcLeave();
        fflush(s_cmp.out);
        s_cmp.dump_at = -1;
        if (s_cmp.quit_at >= 0) exit(0);
        return;
    }
    const int quit_now = s_cmp.quit_at >= 0 && key >= s_cmp.quit_at;
    if (s_cmp.stride && (key % s_cmp.stride == 0 || quit_now)) {
        fprintf(s_cmp.out, "H %ld", key);
        const int spc = CmpSpcSide();
        if (spc) CmpSpcEnter();
        for (int i = 0; i < s_cmp.nregions; i++) {
            size_t n;
            const uint8_t *p = CmpRegion(s_cmp.regions[i], &n, scratch);
            fprintf(s_cmp.out, " %08x", s_cmp.regions[i] == CMP_PICTURE ? CmpWordHash(p, n) : CmpCrc(0, p, n));
        }
        if (spc) CmpSpcLeave();
        fputc('\n', s_cmp.out);
        fflush(s_cmp.out);
    }
    if (quit_now) {
        fflush(s_cmp.out);
        exit(0);
    }
}

/* SOE_COMPARE_AT=nmi: sample as each NMI is taken; the key is the NMI's number since power-on (1 = the first). */
static void CmpNmi(void) {
    if (s_cmp.state != 2) return;
    ++s_cmp.nmis_taken;
    if (s_cmp.nmi_log) { /* "N <nmi> <master clock> <interrupted pc24>": the frame is P, PCL, PCH, PB above S */
        const uint16_t sp = g_cpu.S;
        const uint32_t pc = g_ram[(uint16_t)(sp + 2)] | g_ram[(uint16_t)(sp + 3)] << 8 | (uint32_t)g_ram[(uint16_t)(sp + 4)] << 16;
        fprintf(s_cmp.out, "N %ld %llu %06x\n", s_cmp.nmis_taken, (unsigned long long)g_cpu.master_cycles, pc);
        if (s_cmp.nmi_beam) { /* SOE_COMPARE_NMI_BEAM: and the beam's H clock and line on stderr (walks the beam here) */
            snes_sync_master_clock(g_snes, g_cpu.master_cycles);
            fprintf(stderr, "[nmibeam] %ld %u %u\n", s_cmp.nmis_taken, (unsigned)g_snes->hPos, (unsigned)g_snes->vPos);
        }
    }
    CmpAt(s_cmp.nmis_taken);
}

static void CmpFrame(void) {
    if (!s_cmp.state) CmpInit();
    if (s_cmp.state != 2) return;
    long raw = g_ram[0x100] | (g_ram[0x101] << 8);
    if (s_cmp.raw_last < 0) {
        if (raw == 0) s_cmp.raw_last = 0; /* counts from the 0 the reset code writes, as in snes9x */
    } else if (raw != s_cmp.raw_last) {
        s_cmp.nmis += raw > s_cmp.raw_last ? raw - s_cmp.raw_last
                    : s_cmp.raw_last > 0xFF00 ? raw + 0x10000 - s_cmp.raw_last /* wrapped */
                    : raw;                                                      /* zeroed: the demo looped */
        s_cmp.raw_last = raw;
    }
    long key = s_cmp.at_nmi ? s_cmp.nmis_taken : s_cmp.raw_last < 0 ? -1 : s_cmp.nmis;
    if (s_cmp.npeek) {
        fprintf(s_cmp.out, "P %ld %ld %llu", s_cmp.frames, key, (unsigned long long)g_snes->beamMasterLast);
        for (int i = 0; i < s_cmp.npeek; i++) fprintf(s_cmp.out, " %02x", g_ram[s_cmp.peek[i]]);
        fputc('\n', s_cmp.out);
        fflush(s_cmp.out);
    }
    s_cmp.frames++;
    if (!s_cmp.at_nmi) CmpAt(key);
}

/*
 * The SPC700 runs from power-on. snesrecomp's APU runs on guest time only once its first CPU->APU port write anchors
 * that time; before it, only the audio callback moves it, and headless runs have none. So the sound driver's boot ROM
 * started at the game's first port access and the upload waited 46,700 master clocks for $BBAA that MesenCE does not
 * (docs/imperfections/apu-time-in-long-frames.md). Writing port 0's power-on value 0 at guest cycle 0 anchors it.
 */
static void AnchorApuAtPowerOn(void) {
    static bool done;
    if (done) return;
    done = true;
    apu_schedulePortWrite(g_snes->apu, 0, 0, 0);
}

/* SOE_SAVE_STATE_AT=<n>: save slot 0 after host frame n, as the F1 hotkey would (tests/save_state_resume.sh: a
 * script can load a slot but not save one). */
static void SaveStateForTest(void) {
    static long at = -2, frame;
    if (at == -2) { const char *e = getenv("SOE_SAVE_STATE_AT"); at = e ? atol(e) : -1; }
    if (at >= 0 && ++frame == at) RtlSaveLoad(kSaveLoad_Save, 0);
}

void GameRunOneFrame(void) {
    AnchorApuAtPowerOn();
    soe_frame_driver_run_frame();
    CmpFrame();
    SaveStateForTest();
}

void GameDrawPpuFrame(void) { snes_beam_frame_driver_draw_ppu_frame(); }

/* A new session (reset / return to title) must not inherit driver state from the last one. snesrecomp calls
 * session_reset only when the content variant changes; a console reset (the Reset hotkey, a script's `reset`) calls
 * hardware_reset after resetting the machine. Without it the driver resumed at the old PC on the reset hardware:
 * a black screen, the NMI counter stuck at 0 (found 2026-11-03, docs/bugs-and-todos.md "Soft reset"). */
void GameSessionReset(void) { soe_frame_driver_reset(); }

/* The console reset. snesrecomp's RtlReset resets the machine and the legacy core's registers
 * (SnesEnterNativeMode), not g_cpu, which the bridge and the compiled code run on: the game's M0X0 survived, and the
 * reset handler's first `lda.b #$8F` read a 16-bit operand and ran off into data. A 65816 reset: emulation mode,
 * M = X = I = 1, decimal off, D = DB = PB = 0, the stack in page 1 (as cpu_state_init at power-on, clocks and RAM
 * kept). */
static void GameHardwareReset(void) {
    g_cpu.emulation = 1;
    g_cpu.P = CPU_P_M | CPU_P_X | CPU_P_I;
    cpu_p_to_mirrors(&g_cpu);
    g_cpu.S = (uint16_t)(0x0100u | (g_cpu.S & 0xFFu));
    g_cpu.D = 0;
    g_cpu.DB = 0;
    g_cpu.PB = 0;
    g_cpu.host_return_valid = 0;
    soe_frame_driver_reset();
}

/*
 * Called by the desktop host after config.ini is parsed (hooked in src/main.c as the host
 * descriptor's after_config). snesrecomp's optimized PPU ("NewRenderer") draws BG modes 4-6 through
 * its mode-7 path; SoE's character-name screen is mode 4 (BG1 8bpp, written at $2105 = $04), so
 * it came out as noise. The reference renderer (ppu_legacy.c) handles every mode: use it until
 * the optimized one learns mode 4. Found 2026-10-12 (docs/game-rtl.md §4).
 */
/*
 * Extended frame timing: the APU's guest time follows the master clock across a host frame that holds more than
 * one video field (the reset code's 192 KB of DMA spans 5 fields in 4 host frames; scene loads do the same). By
 * default snesrecomp clamps it to one field per host frame, so the SPC700 lost the time it runs its boot ROM in:
 * the first sound-driver upload waited 46,700 master clocks for $BBAA that MesenCE does not wait, and every later
 * NMI came late (docs/imperfections/apu-time-in-long-frames.md).
 */
void SoeAfterConfig(void) {
    g_config.new_renderer = false;
    RtlEnableExtendedFrameTiming();
    /* snesrecomp PR #241 (framework/snesrecomp.patch): the SPC700's time is the master clock since power-on (MesenCE's
     * rate and rounding); the CPU reaches the ports 20 clocks into an absolute CMP/STA (docs/research/upstream-access-timing.md). */
    const char *mc = getenv("SNESRECOMP_APU_MASTER_CLOCK"); /* =0: the framework's old APU timeline, for A/B runs */
    RtlApuUseMasterClock(!(mc && mc[0] == '0'), 20);
    /* SOE_COMPARE_SAVE_ROOT: battery RAM from (and back to) this folder instead of saves/, so a compare run starts
     * from cleared SRAM as MesenCE's does (the harness gives an empty folder) and never touches a player's save:
     * the game's SRAM checksum leaves a stack byte that differs with the contents (NMI keys 320-321 of the attract
     * replay). Set here because RtlReadSram runs after after_config. */
    const char *save_root = getenv("SOE_COMPARE_SAVE_ROOT");
    if (save_root && *save_root) RtlSetSaveRoot(save_root);
}

/*
 * Save states. The framework's file snapshot holds the machine (WRAM, PPU, APU, the legacy
 * interpreter's Cpu) but not g_cpu -- the registers the recompiled code and the bridge run on --
 * nor the frame driver's resume PC (soe_frame_driver.c). A load therefore kept the running session's A/X/Y/S/P and PC
 * under the saved RAM: it only looked right when both moments sat in the same idle loop, and a
 * load before the first frame (resume PC still 0) cold-booted from the reset vector on top of the
 * loaded RAM, which is a black screen. Found 2026-10-12 (docs/bugs-and-todos.md).
 * This chunk follows the guest blob; saves made before it load as before.
 */
#define SOE_EXEC_MAGIC 0x43584553u /* 'SEXC' */
#define SOE_EXEC_VERSION 1u
#define SOE_CPU_REGS_SIZE offsetof(CpuState, ram) /* A .. open_bus: no host pointer, no clocks */

typedef struct SoeExecChunk {
    uint32_t magic, version, regs_size, resume_pc;
    uint8_t wai_halted, pad[3];
} SoeExecChunk;

#define SOE_LOAD_MIN_CLOCK (2u * 357368u) /* two fields of master clocks */

static SoeExecChunk s_loaded_exec;
static uint8_t s_loaded_regs[SOE_CPU_REGS_SIZE];
static bool s_loaded_exec_valid;

static void SoeStateSaveExtra(SaveLoadInfo *sli) {
    SoeExecChunk c = { SOE_EXEC_MAGIC, SOE_EXEC_VERSION, (uint32_t)SOE_CPU_REGS_SIZE,
                       soe_frame_driver_resume_pc(), soe_frame_driver_wai_halted(), { 0 } };
    sli->func(sli, &c, sizeof(c));
    sli->func(sli, &g_cpu, SOE_CPU_REGS_SIZE);
}

static void SoeStateLoadExtra(SaveLoadInfo *sli, uint32_t version) {
    (void)version;
    s_loaded_exec_valid = false;
    if (RtlStateBytesRemaining(sli) < sizeof(SoeExecChunk) + SOE_CPU_REGS_SIZE) return;
    sli->func(sli, &s_loaded_exec, sizeof(s_loaded_exec));
    if (s_loaded_exec.magic != SOE_EXEC_MAGIC || s_loaded_exec.version != SOE_EXEC_VERSION ||
        s_loaded_exec.regs_size != SOE_CPU_REGS_SIZE || !s_loaded_exec.resume_pc) return;
    sli->func(sli, s_loaded_regs, SOE_CPU_REGS_SIZE);
    s_loaded_exec_valid = true;
    /* A state loaded before the first frames: the clocks are not part of the state, and snesrecomp next sets the
     * beam's clock to the CPU's, still near 0, under the saved V/H. The frame driver's line and refresh arithmetic
     * then went below clock 0 and wrapped (a hang in refresh_points_upto). Start such a session two fields in, with
     * the APU's sync point alongside so it does not catch up on time that never passed. Found 2026-11-03
     * (tests/save_state_resume.sh). */
    if (g_cpu.master_cycles < SOE_LOAD_MIN_CLOCK) {
        g_cpu.master_cycles = SOE_LOAD_MIN_CLOCK;
        g_apu_last_sync_master = SOE_LOAD_MIN_CLOCK;
    }
}

static void SoeOnStateLoaded(uint32_t version) {
    (void)version;
    if (!s_loaded_exec_valid) {
        printf("[soe] save state has no CPU chunk (made before v0.201.18): CPU and PC are not restored\n");
        return;
    }
    s_loaded_exec_valid = false;
    memcpy(&g_cpu, s_loaded_regs, SOE_CPU_REGS_SIZE);
    interp_bridge_set_lle_resume_pc(s_loaded_exec.resume_pc);
    soe_frame_driver_set_resume(s_loaded_exec.resume_pc, s_loaded_exec.wai_halted != 0);
}

const RtlGameInfo kGameInfo = {
    .title = "secretofevermore",
    .initialize = NULL,
    .run_frame = &GameRunOneFrame,
    .draw_ppu_frame = &GameDrawPpuFrame,
    .save_name_prefix = "save",
    .state_save_extra = &SoeStateSaveExtra,
    .state_load_extra = &SoeStateLoadExtra,
    .on_state_loaded = &SoeOnStateLoaded,
    .session_reset = &GameSessionReset,
    .hardware_reset = &GameHardwareReset,
};
