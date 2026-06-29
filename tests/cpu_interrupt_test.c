#include "../src/cpu.h"
#include "../src/interrupt.h"
#include "../src/memory.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void setup(mem_t *mem, cpu_t *cpu)
{
    cpu_init_opcodes();
    mem_init(mem);
    cpu_init(cpu);
    mem->rom = calloc(0x8000, 1);
    assert(mem->rom);
    mem->rom_banks = 2;
    mem->boot_on = false;
    cpu->pc = 0x0100;
    cpu->sp = 0xFFFE;
    mem->ie = 0;
    mem->io[0x0F] = 0;
}

static void cleanup(mem_t *mem)
{
    free(mem->rom);
}

static void test_ei_is_delayed_until_following_instruction_boundary(void)
{
    mem_t mem;
    cpu_t cpu;
    setup(&mem, &cpu);

    mem.rom[0x0100] = 0xFB; /* EI */
    mem.rom[0x0101] = 0x00; /* NOP */
    mem.rom[0x0102] = 0x00; /* NOP after interrupt service */

    cpu.ime = false;
    cpu.ime_scheduled = false;
    mem.ie = (1 << INT_TIMER);
    mem.io[0x0F] = (1 << INT_TIMER);

    cpu_step(&cpu, &mem);
    assert(cpu.pc == 0x0101);
    assert(cpu.sp == 0xFFFE);

    cpu_step(&cpu, &mem);
    assert(cpu.pc == 0x0102);
    assert(cpu.sp == 0xFFFE);

    int cycles = cpu_step(&cpu, &mem);
    assert(cycles == 20);
    assert(cpu.pc == 0x50);
    assert(cpu.sp == 0xFFFC);
    assert((mem.io[0x0F] & (1 << INT_TIMER)) == 0);

    cleanup(&mem);
}

static void test_interrupt_service_pushes_pc_and_clears_if_bit(void)
{
    mem_t mem;
    cpu_t cpu;
    setup(&mem, &cpu);

    cpu.ime = true;
    cpu.pc = 0x2345;
    mem.ie = (1 << INT_TIMER);
    mem.io[0x0F] = (1 << INT_TIMER);

    int cycles = cpu_step(&cpu, &mem);

    assert(cycles == 20);
    assert(cpu.ime == false);
    assert(cpu.pc == 0x50);
    assert(cpu.sp == 0xFFFC);
    assert(mem_read(&mem, 0xFFFC) == 0x45);
    assert(mem_read(&mem, 0xFFFD) == 0x23);
    assert((mem.io[0x0F] & (1 << INT_TIMER)) == 0);

    cleanup(&mem);
}

static void test_halt_with_ime_and_pending_interrupt_services_next_step(void)
{
    mem_t mem;
    cpu_t cpu;
    setup(&mem, &cpu);

    mem.rom[0x0100] = 0x76; /* HALT */
    cpu.ime = true;
    mem.ie = (1 << INT_VBLANK);

    cpu_step(&cpu, &mem);
    assert(cpu.halted == true);
    mem.io[0x0F] = (1 << INT_VBLANK);
    int cycles = cpu_step(&cpu, &mem);

    assert(cycles == 20);
    assert(cpu.halted == false);
    assert(cpu.pc == 0x40);
    assert(cpu.sp == 0xFFFC);
    assert(mem_read(&mem, 0xFFFC) == 0x01);
    assert(mem_read(&mem, 0xFFFD) == 0x01);
    assert((mem.io[0x0F] & (1 << INT_VBLANK)) == 0);

    cleanup(&mem);
}

static void test_halt_bug_duplicates_opcode_fetch_for_multi_byte_instruction(void)
{
    mem_t mem;
    cpu_t cpu;
    setup(&mem, &cpu);

    mem.rom[0x0100] = 0x76; /* HALT */
    mem.rom[0x0101] = 0x06; /* LD B,d8 */
    mem.rom[0x0102] = 0x04; /* duplicated as opcode after halt bug */
    mem.rom[0x0103] = 0x0C; /* INC C */

    cpu.b = 0;
    cpu.c = 0;
    cpu.ime = false;
    mem.ie = (1 << INT_TIMER);
    mem.io[0x0F] = (1 << INT_TIMER);

    cpu_step(&cpu, &mem);

    cpu_step(&cpu, &mem);
    assert(cpu.b == 0x06);

    cpu_step(&cpu, &mem);
    assert(cpu.b == 0x07);

    cpu_step(&cpu, &mem);
    assert(cpu.b == 0x07);
    assert(cpu.c == 1);

    cleanup(&mem);
}

int main(void)
{
    test_ei_is_delayed_until_following_instruction_boundary();
    test_interrupt_service_pushes_pc_and_clears_if_bit();
    test_halt_with_ime_and_pending_interrupt_services_next_step();
    test_halt_bug_duplicates_opcode_fetch_for_multi_byte_instruction();
    return 0;
}
