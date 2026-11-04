/*
 * A MesenCE save state as the recomp's starting point (docs/imperfections/mesen-state.md).
 *
 * SOE_COMPARE_MESEN_STATE=<file> holds the state's inflated field stream: a NUL-ended key, a u32 length, the bytes,
 * one after another (everscript-vscode src/platform/tas/mesen-movie.ts takes it out of the .mss). The state must sit
 * at an instruction boundary at a field's line 225, where MesenCE leaves its frame loop and this driver's host
 * frame starts. Clocks stay absolute: MesenCE's master clock is on this driver's field grid and its spc.cycle on the
 * APU's half-cycle timeline, so refresh points, the auto-joypad window and the SPC700 line up by themselves.
 *
 * PPU registers go through ppu_write, from MesenCE's decoded fields. The SPC700 runs an opcode whole at its first clock
 * here and in steps on MesenCE: an opcode in progress is run now, with its remaining cycles left to count down.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/apu.h"
#include "snes/dma.h"
#include "snes/dsp.h"
#include "snes/interp_bridge.h"
#include "snes/joypad.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/spc.h"
#include "soe_frame_driver.h"

extern uint8_t g_ram[0x20000];

static const uint8_t *s_data;
static size_t s_size;

static const uint8_t *field(const char *key, uint32_t *length) {
    for (size_t q = 0; q < s_size;) {
        const uint8_t *end = memchr(s_data + q, 0, s_size - q);
        if (!end) break;
        uint32_t n;
        memcpy(&n, end + 1, 4);
        if (!strcmp((const char *)s_data + q, key)) { if (length) *length = n; return end + 5; }
        q = (size_t)(end - s_data) + 5 + n;
    }
    fprintf(stderr, "soe_mesen_state: no field %s\n", key);
    abort();
}
static uint64_t num(const char *key) {
    uint32_t n;
    const uint8_t *p = field(key, &n);
    uint64_t v = 0;
    for (uint32_t i = 0; i < n && i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
static uint64_t numf(const char *format, int i) {
    char key[96];
    snprintf(key, sizeof key, format, i);
    return num(key);
}
static uint64_t num2(const char *format, int i, int j) {
    char key[96];
    snprintf(key, sizeof key, format, i, j);
    return num(key);
}
static void copy(const char *key, void *to, size_t size) {
    uint32_t n;
    const uint8_t *p = field(key, &n);
    memcpy(to, p, n < size ? n : size);
}

static void load_cpu(void) {
    g_cpu.A = (uint16_t)num("cpu.a");
    g_cpu.X = (uint16_t)num("cpu.x");
    g_cpu.Y = (uint16_t)num("cpu.y");
    g_cpu.S = (uint16_t)num("cpu.sp");
    g_cpu.D = (uint16_t)num("cpu.d");
    g_cpu.DB = (uint8_t)num("cpu.dbr");
    g_cpu.PB = (uint8_t)num("cpu.k");
    g_cpu.P = (uint8_t)num("cpu.ps");
    g_cpu.emulation = (uint8_t)num("cpu.emulationMode");
    cpu_p_to_mirrors(&g_cpu);
    g_cpu.host_return_valid = 0;
    g_cpu.cycles = num("cpu.cycleCount");
    g_cpu.master_cycles = num("memoryManager.masterClock");
    g_cpu.open_bus = (uint8_t)num("memoryManager.openBus");
}

static void load_ppu(void) {
    Ppu *ppu = g_ppu;
    copy("ppu.vram", ppu->vram, sizeof ppu->vram);
    copy("ppu.cgram", ppu->cgram, sizeof ppu->cgram);
    uint32_t n;
    const uint8_t *oam = field("ppu.oamRam", &n);
    memcpy(ppu->oam, oam, 0x200);
    memcpy(ppu->highOam, oam + 0x200, 0x20);

    ppu_write(ppu, 0x00, (uint8_t)(num("ppu.forcedBlank") << 7 | num("ppu.screenBrightness")));
    const uint64_t offset = (num("ppu.oamAddressOffset") >> 12) - 1;
    ppu_write(ppu, 0x01, (uint8_t)(num("ppu.oamMode") << 5 | offset << 3 | num("ppu.oamBaseAddress") >> 13));
    uint8_t mode = (uint8_t)(num("ppu.bgMode") | num("ppu.mode1Bg3Priority") << 3);
    for (int i = 0; i < 4; i++) mode |= (uint8_t)(numf("ppu.layers[%d].largeTiles", i) << (4 + i));
    ppu_write(ppu, 0x05, mode);
    ppu_write(ppu, 0x06, (uint8_t)(num("ppu.mosaicEnabled") | (num("ppu.mosaicSize") - 1) << 4));
    for (int i = 0; i < 4; i++) {
        ppu_write(ppu, (uint8_t)(0x07 + i), (uint8_t)(numf("ppu.layers[%d].tilemapAddress", i) >> 8 |
            numf("ppu.layers[%d].doubleWidth", i) | numf("ppu.layers[%d].doubleHeight", i) << 1));
        ppu->hScroll[i] = (uint16_t)numf("ppu.layers[%d].hscroll", i);
        ppu->vScroll[i] = (uint16_t)numf("ppu.layers[%d].vscroll", i);
    }
    ppu_write(ppu, 0x0B, (uint8_t)(num("ppu.layers[0].chrAddress") >> 12 | num("ppu.layers[1].chrAddress") >> 8));
    ppu_write(ppu, 0x0C, (uint8_t)(num("ppu.layers[2].chrAddress") >> 12 | num("ppu.layers[3].chrAddress") >> 8));
    ppu->scrollPrev = (uint8_t)num("ppu.hvScrollLatchValue");
    ppu->scrollPrev2 = (uint8_t)num("ppu.hScrollLatchValue");

    ppu_write(ppu, 0x1A, (uint8_t)(num("ppu.mode7.largeMap") << 7 | num("ppu.mode7.fillWithTile0") << 6 |
        num("ppu.mode7.verticalMirroring") << 1 | num("ppu.mode7.horizontalMirroring")));
    for (int i = 0; i < 4; i++) ppu->m7matrix[i] = (int16_t)numf("ppu.mode7.matrix[%d]", i);
    ppu->m7matrix[4] = (int16_t)num("ppu.mode7.centerX");
    ppu->m7matrix[5] = (int16_t)num("ppu.mode7.centerY");
    ppu->m7matrix[6] = (int16_t)num("ppu.mode7.hscroll");
    ppu->m7matrix[7] = (int16_t)num("ppu.mode7.vscroll");
    ppu->m7prev = (uint8_t)num("ppu.mode7.valueLatch");

    /* $2123-$2125: per layer, window 1's invert and enable, then window 2's. */
    for (int reg = 0; reg < 3; reg++) {
        uint8_t v = 0;
        for (int half = 0; half < 2; half++) {
            const int layer = reg * 2 + half;
            for (int w = 0; w < 2; w++) {
                const uint64_t bits = num2("ppu.window[%d].invertedLayers[%d]", w, layer) |
                                      num2("ppu.window[%d].activeLayers[%d]", w, layer) << 1;
                v |= (uint8_t)(bits << (half * 4 + w * 2));
            }
        }
        ppu_write(ppu, (uint8_t)(0x23 + reg), v);
    }
    ppu_write(ppu, 0x26, (uint8_t)num("ppu.window[0].left"));
    ppu_write(ppu, 0x27, (uint8_t)num("ppu.window[0].right"));
    ppu_write(ppu, 0x28, (uint8_t)num("ppu.window[1].left"));
    ppu_write(ppu, 0x29, (uint8_t)num("ppu.window[1].right"));
    uint8_t logic = 0, main_mask = 0, sub_mask = 0;
    for (int i = 0; i < 4; i++) logic |= (uint8_t)(numf("ppu.maskLogic[%d]", i) << (i * 2));
    ppu_write(ppu, 0x2A, logic);
    ppu_write(ppu, 0x2B, (uint8_t)(num("ppu.maskLogic[4]") | num("ppu.maskLogic[5]") << 2));
    ppu_write(ppu, 0x2C, (uint8_t)num("ppu.mainScreenLayers"));
    ppu_write(ppu, 0x2D, (uint8_t)num("ppu.subScreenLayers"));
    for (int i = 0; i < 5; i++) {
        main_mask |= (uint8_t)(numf("ppu.windowMaskMain[%d]", i) << i);
        sub_mask |= (uint8_t)(numf("ppu.windowMaskSub[%d]", i) << i);
    }
    ppu_write(ppu, 0x2E, main_mask);
    ppu_write(ppu, 0x2F, sub_mask);
    ppu_write(ppu, 0x30, (uint8_t)(num("ppu.directColorMode") | num("ppu.colorMathAddSubscreen") << 1 |
        num("ppu.colorMathPreventMode") << 4 | num("ppu.colorMathClipMode") << 6));
    ppu_write(ppu, 0x31, (uint8_t)(num("ppu.colorMathEnabled") | num("ppu.colorMathHalveResult") << 6 |
        num("ppu.colorMathSubtractMode") << 7));
    const uint64_t color = num("ppu.fixedColor");
    ppu_write(ppu, 0x32, (uint8_t)(0x20 | (color & 0x1F)));
    ppu_write(ppu, 0x32, (uint8_t)(0x40 | (color >> 5 & 0x1F)));
    ppu_write(ppu, 0x32, (uint8_t)(0x80 | (color >> 10 & 0x1F)));
    ppu_write(ppu, 0x33, (uint8_t)(num("ppu.screenInterlace") | num("ppu.objInterlace") << 1 | num("ppu.overscanMode") << 2 |
        num("ppu.hiResMode") << 3 | num("ppu.extBgEnabled") << 6));

    const uint64_t oam_address = num("ppu.oamRamAddress");
    ppu_write(ppu, 0x02, (uint8_t)oam_address);
    ppu_write(ppu, 0x03, (uint8_t)(oam_address >> 8 | num("ppu.enableOamPriority") << 7));
    ppu->vramPointer = (uint16_t)num("ppu.vramAddress");
    ppu->vramIncrement = (uint8_t)num("ppu.vramIncrementValue");
    ppu->vramRemapMode = (uint8_t)num("ppu.vramAddressRemapping");
    ppu->vramIncrementOnHigh = num("ppu.vramAddrIncrementOnSecondReg") != 0;
    ppu->vramReadBuffer = (uint16_t)num("ppu.vramReadBuffer");
    ppu->cgramPointer = (uint8_t)num("ppu.cgramAddress");
    ppu->cgramSecondWrite = num("ppu.cgramAddressLatch") != 0;
    ppu->cgramBuffer = (uint8_t)num("ppu.cgramWriteBuffer");
    ppu->oamBuffer = (uint8_t)num("ppu.oamWriteBuffer");
    ppu->timeOver = num("ppu.timeOver") != 0;
    ppu->rangeOver = num("ppu.rangeOver") != 0;
    ppu->evenFrame = !num("ppu.oddFrame");
    ppu->hCount = (uint16_t)num("ppu.horizontalLocation");
    ppu->vCount = (uint16_t)num("ppu.verticalLocation");
    ppu->hCountSecond = num("ppu.horizontalLocToggle") != 0;
    ppu->vCountSecond = num("ppu.verticalLocationToggle") != 0;
    ppu->countersLatched = num("ppu.locationLatched") != 0;
}

static void load_dma(void) {
    const uint64_t hdma = num("dmaController.hdmaChannels");
    for (int i = 0; i < 8; i++) {
        DmaChannel *c = &g_snes->dma->channel[i];
        c->bAdr = (uint8_t)numf("dmaController.channel[%d].destAddress", i);
        c->aAdr = (uint16_t)numf("dmaController.channel[%d].srcAddress", i);
        c->aBank = (uint8_t)numf("dmaController.channel[%d].srcBank", i);
        c->size = (uint16_t)numf("dmaController.channel[%d].transferSize", i);
        c->indBank = (uint8_t)numf("dmaController.channel[%d].hdmaBank", i);
        c->tableAdr = (uint16_t)numf("dmaController.channel[%d].hdmaTableAddress", i);
        c->repCount = (uint8_t)numf("dmaController.channel[%d].hdmaLineCounterAndRepeat", i);
        c->unusedByte = (uint8_t)numf("dmaController.channel[%d].unusedRegister", i);
        c->dmaActive = numf("dmaController.channel[%d].dmaActive", i) != 0;
        c->hdmaActive = (hdma >> i & 1) != 0;
        c->mode = (uint8_t)numf("dmaController.channel[%d].transferMode", i);
        c->fixed = numf("dmaController.channel[%d].fixedTransfer", i) != 0;
        c->decrement = numf("dmaController.channel[%d].decrement", i) != 0;
        c->indirect = numf("dmaController.channel[%d].hdmaIndirectAddressing", i) != 0;
        c->fromB = numf("dmaController.channel[%d].invertDirection", i) != 0;
        c->unusedBit = numf("dmaController.channel[%d].unusedControlFlag", i) != 0;
        c->doTransfer = numf("dmaController.channel[%d].doTransfer", i) != 0;
        c->terminated = numf("dmaController.channel[%d].hdmaFinished", i) != 0;
        c->offIndex = 0;
    }
}

static void load_registers(void) {
    Snes *snes = g_snes;
    g_memsel = (uint8_t)num("internalRegisters.enableFastRom");
    snes->nmiEnabled = num("internalRegisters.enableNmi") != 0;
    snes->rdnmiPending = num("internalRegisters.nmiFlag") != 0;
    snes->hIrqEnabled = num("internalRegisters.enableHorizontalIrq") != 0;
    snes->vIrqEnabled = num("internalRegisters.enableVerticalIrq") != 0;
    snes->hTimer = (uint16_t)num("internalRegisters.horizontalTimer");
    snes->vTimer = (uint16_t)num("internalRegisters.verticalTimer");
    snes->autoJoyRead = num("internalRegisters.enableAutoJoypadRead") != 0;
    snes->autoJoyTimer = 0;
    snes->multiplyA = (uint8_t)num("internalRegisters.aluMulDiv.multOperand1");
    snes->multiplyResult = (uint16_t)num("internalRegisters.aluMulDiv.multOrRemainderResult");
    snes->divideA = (uint16_t)num("internalRegisters.aluMulDiv.dividend");
    snes->divideResult = (uint16_t)num("internalRegisters.aluMulDiv.divResult");
    snes->ramAdr = (uint32_t)num("memoryManager.registerHandlerB.wramPosition");
    snes->inVblank = true;
    snes->vPos = (uint16_t)num("ppu.scanline");
    snes->hPos = (uint16_t)num("memoryManager.hClock");
    snes->beamMasterLast = g_cpu.master_cycles;
}

/* The pad of this field, and its automatic read: the beam starts just past the point where snesrecomp takes it. */
static void load_joypad(void) {
    static const uint8_t bit[12] = { 8, 0, 9, 1, 10, 11, 2, 3, 4, 5, 6, 7 };   /* MesenCE's ABXYLRSTUDLR to the host's word */
    const uint64_t state = num("controlManager.controlDevices[0].state");
    uint16_t input = 0;
    for (int i = 0; i < 12; i++) if (state >> i & 1) input |= (uint16_t)(1u << bit[i]);
    g_snes->input1_currentState = input;
    joypad_set_pad(0, input);
    if (g_snes->autoJoyRead) joypad_auto_read(g_snes);
}

static void load_dsp(Dsp *dsp) {
    DspClock *c = &dsp->clk;
    copy("spc.dsp.externalRegs", dsp->ram, sizeof dsp->ram);
    copy("spc.dsp.regs", c->regs, sizeof c->regs);
    c->step = (uint8_t)num("spc.dsp.step");
    c->counter = (uint16_t)num("spc.dsp.counter");
    c->noise = (int32_t)num("spc.dsp.noiseLfsr");
    c->outxBuf = (uint8_t)num("spc.dsp.outRegBuffer");
    c->envxBuf = (uint8_t)num("spc.dsp.envRegBuffer");
    c->endxBuf = (uint8_t)num("spc.dsp.voiceEndBuffer");
    c->voiceOut = (int32_t)num("spc.dsp.voiceOutput");
    copy("spc.dsp.outSamples", c->out, sizeof c->out);
    c->pitch = (int32_t)num("spc.dsp.pitch");
    c->dirAdr = (uint16_t)num("spc.dsp.sampleAddress");
    c->nextAdr = (uint16_t)num("spc.dsp.brrNextAddress");
    c->dir = (uint8_t)num("spc.dsp.dirSampleTableAddress");
    c->non = (uint8_t)num("spc.dsp.noiseOn");
    c->pmon = (uint8_t)num("spc.dsp.pitchModulationOn");
    c->eon = (uint8_t)num("spc.dsp.echoOn");
    c->kon = (uint8_t)num("spc.dsp.keyOn");
    c->konNew = (uint8_t)num("spc.dsp.newKeyOn");
    c->kof = (uint8_t)num("spc.dsp.keyOff");
    c->everyOther = (uint8_t)num("spc.dsp.everyOtherSample");
    c->srcn = (uint8_t)num("spc.dsp.sourceNumber");
    c->brrHeader = (uint8_t)num("spc.dsp.brrHeader");
    c->brrData = (uint8_t)num("spc.dsp.brrData");
    c->looped = (uint8_t)num("spc.dsp.looped");
    c->adsr1 = (uint8_t)num("spc.dsp.adsr1");
    copy("spc.dsp.echoIn", c->echoIn, sizeof c->echoIn);
    copy("spc.dsp.echoOut", c->echoOut, sizeof c->echoOut);
    copy("spc.dsp.echoHistory", c->echoHist, sizeof c->echoHist);
    c->echoPtr = (uint16_t)num("spc.dsp.echoPointer");
    c->echoLen = (uint16_t)num("spc.dsp.echoLength");
    c->echoOff = (uint16_t)num("spc.dsp.echoOffset");
    c->echoHistPos = (uint8_t)num("spc.dsp.echoHistoryPos");
    c->esa = (uint8_t)num("spc.dsp.echoRingBufferAddress");
    c->echoOn = num("spc.dsp.echoEnabled") != 0;
    for (int i = 0; i < 8; i++) {
        DspClockVoice *v = &c->v[i];
        v->env = (int32_t)numf("spc.dsp.voices[%d].envVolume", i);
        v->envPrev = (int32_t)numf("spc.dsp.voices[%d].prevCalculatedEnv", i);
        v->pos = (int32_t)numf("spc.dsp.voices[%d].interpolationPos", i);
        v->mode = (uint8_t)numf("spc.dsp.voices[%d].envMode", i);
        v->brrAdr = (uint16_t)numf("spc.dsp.voices[%d].brrAddress", i);
        v->brrOff = (uint8_t)numf("spc.dsp.voices[%d].brrOffset", i);
        v->konDelay = (uint8_t)numf("spc.dsp.voices[%d].keyOnDelay", i);
        v->envx = (uint8_t)numf("spc.dsp.voices[%d].envOut", i);
        v->bufPos = (uint8_t)numf("spc.dsp.voices[%d].bufferPos", i);
        char key[64];
        snprintf(key, sizeof key, "spc.dsp.voices[%d].sampleBuffer", i);
        copy(key, v->buf, sizeof v->buf);
    }
}

/* The opcode MesenCE is inside of: it starts `length` bytes before PC, the length found from the opcode byte. */
static void finish_spc_opcode(Apu *apu) {
    const uint64_t step = num("spc.opStep");
    if (!step) return;
    const uint8_t opcode = (uint8_t)num("spc.opCode");
    int length = 1;
    while (length <= 3 && apu->ram[(uint16_t)(apu->spc->pc - length)] != opcode) length++;
    if (length > 3) { fprintf(stderr, "soe_mesen_state: SPC opcode %02X not found before PC\n", opcode); return; }
    apu->spc->pc = (uint16_t)(apu->spc->pc - length);
    /* spc.opStep counts from 0: measured on the one state so far (step 3 of $F7), each other value parts by key 108. */
    const int used = spc_runOpcode(apu->spc), ran = (int)step + 1;
    apu->cpuCyclesLeft = (uint8_t)(used > ran ? used - ran : 0);
}

static void load_apu(void) {
    Apu *apu = g_snes->apu;
    Spc *spc = apu->spc;
    copy("spc.ram", apu->ram, sizeof apu->ram);
    spc->a = (uint8_t)num("spc.a");
    spc->x = (uint8_t)num("spc.x");
    spc->y = (uint8_t)num("spc.y");
    spc->sp = (uint8_t)num("spc.sp");
    spc->pc = (uint16_t)num("spc.pc");
    const uint8_t ps = (uint8_t)num("spc.ps");
    spc->n = ps & 0x80; spc->v = ps & 0x40; spc->p = ps & 0x20; spc->b = ps & 0x10;
    spc->h = ps & 0x08; spc->i = ps & 0x04; spc->z = ps & 0x02; spc->c = ps & 0x01;
    spc->stopped = false;
    apu->romReadable = num("spc.romEnabled") != 0;
    apu->dspAdr = (uint8_t)num("spc.dspReg");
    for (int i = 0; i < 4; i++) {
        apu->inPorts[i] = (uint8_t)numf("spc.cpuRegs[%d]", i);
        apu->outPorts[i] = (uint8_t)numf("spc.outputReg[%d]", i);
    }
    apu->inPorts[4] = (uint8_t)num("spc.ramReg[0]");
    apu->inPorts[5] = (uint8_t)num("spc.ramReg[1]");
    /* Prescaler position: two units per SPC cycle, a period of two half periods. */
    for (int i = 0; i < 3; i++) {
        const int half = i == 2 ? 16 : 128, period = half;
        const int position = (int)numf("spc.timer%d.stage1", i) * half + (int)numf("spc.timer%d.stage0", i);
        Timer *t = &apu->timer[i];
        t->cycles = (uint8_t)((period - 1 - position / 2 + period) % period);
        t->divider = (uint8_t)numf("spc.timer%d.stage2", i);
        t->target = (uint8_t)numf("spc.timer%d.target", i);
        t->counter = (uint8_t)numf("spc.timer%d.output", i);
        t->enabled = numf("spc.timer%d.enabled", i) != 0;
        apu->timerAhead[i] = 0;
    }
    load_dsp(apu->dsp);
    apu->dspAhead = 0;
    apu->cpuCyclesLeft = 0;
    apu->held = false;
    apu->holdActive = false;
    apu->portQHead = apu->portQTail = 0;
    apu->portClock = num("spc.cycle") / 2;
    apu->cycles = (uint32_t)apu->portClock;
    finish_spc_opcode(apu);
}

/* The state's PC, 0 without SOE_COMPARE_MESEN_STATE. */
uint32_t soe_mesen_state_load(void) {
    static bool done;
    const char *path = getenv("SOE_COMPARE_MESEN_STATE");
    FILE *f = !done && path ? fopen(path, "rb") : NULL;
    done = true;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    s_size = (size_t)ftell(f);
    rewind(f);
    uint8_t *data = malloc(s_size);
    if (fread(data, 1, s_size, f) != s_size) s_size = 0;
    fclose(f);
    s_data = data;

    load_cpu();
    copy("memoryManager.workRam", g_ram, 0x20000);
    copy("cart.saveRam", g_sram, (size_t)g_sram_size);
    load_ppu();
    load_dma();
    load_registers();
    load_joypad();
    load_apu();
    const uint32_t pc = (uint32_t)g_cpu.PB << 16 | (uint32_t)num("cpu.pc");
    free(data);
    s_data = NULL;
    return pc;
}
