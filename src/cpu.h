#ifndef CPU_H
#define CPU_H

#include "memory.h"

typedef struct {
    u8 a, f, b, c, d, e, h, l;
    u16 sp, pc;
    bool ime;
    bool ime_scheduled;
    bool halted;
    bool halt_bug;
    bool stopped;
} cpu_t;

static inline u16 cpu_af(cpu_t *c) { return (c->a << 8) | c->f; }
static inline u16 cpu_bc(cpu_t *c) { return (c->b << 8) | c->c; }
static inline u16 cpu_de(cpu_t *c) { return (c->d << 8) | c->e; }
static inline u16 cpu_hl(cpu_t *c) { return (c->h << 8) | c->l; }
static inline void cpu_set_af(cpu_t *c, u16 v) { c->a = v >> 8; c->f = v & 0xF0; }
static inline void cpu_set_bc(cpu_t *c, u16 v) { c->b = v >> 8; c->c = v & 0xFF; }
static inline void cpu_set_de(cpu_t *c, u16 v) { c->d = v >> 8; c->e = v & 0xFF; }
static inline void cpu_set_hl(cpu_t *c, u16 v) { c->h = v >> 8; c->l = v & 0xFF; }

#define FLAG_Z (1 << 7)
#define FLAG_N (1 << 6)
#define FLAG_H (1 << 5)
#define FLAG_C (1 << 4)

static inline void cpu_set_z(cpu_t *c, bool v) { if (v) c->f |= FLAG_Z; else c->f &= ~FLAG_Z; }
static inline void cpu_set_n(cpu_t *c, bool v) { if (v) c->f |= FLAG_N; else c->f &= ~FLAG_N; }
static inline void cpu_set_h(cpu_t *c, bool v) { if (v) c->f |= FLAG_H; else c->f &= ~FLAG_H; }
static inline void cpu_set_c(cpu_t *c, bool v) { if (v) c->f |= FLAG_C; else c->f &= ~FLAG_C; }
static inline bool cpu_get_z(cpu_t *c) { return (c->f & FLAG_Z) != 0; }
static inline bool cpu_get_n(cpu_t *c) { return (c->f & FLAG_N) != 0; }
static inline bool cpu_get_h(cpu_t *c) { return (c->f & FLAG_H) != 0; }
static inline bool cpu_get_c(cpu_t *c) { return (c->f & FLAG_C) != 0; }

void cpu_init(cpu_t *cpu);
int  cpu_step(cpu_t *cpu, mem_t *mem);
int  cpu_service_interrupt(cpu_t *cpu, mem_t *mem);

typedef void (*op_fn)(cpu_t *cpu, mem_t *mem);
extern op_fn  main_opcodes[256];
extern op_fn  cb_opcodes[256];
extern const u8 main_cycles[256];
extern const u8 cb_cycles[256];
void cpu_init_opcodes(void);

#endif
