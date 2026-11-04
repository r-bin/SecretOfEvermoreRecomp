#ifndef SOE_COST_H
#define SOE_COST_H
#include <stdbool.h>
#include <stdint.h>

#define SOE_COST_DEPTH 8

/* A path through a routine so far: see soe_cost.c. */
typedef struct {
    uint64_t cycles, fetches, data;
    uint32_t pc, ret[SOE_COST_DEPTH];
    uint16_t d, s, want;
    uint8_t db;
    int depth;
    bool m16, x16, started, done;
} SoeCostTrace;

void soe_cost_begin(uint32_t pc24, uint16_t d, uint8_t db, bool m16, bool x16, uint16_t s);
void soe_cost_take(SoeCostTrace *out);
void soe_at(uint16_t pc);
void soe_cost_end(SoeCostTrace *out);
#endif
