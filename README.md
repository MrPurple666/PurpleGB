# PurpleGB

PurpleGB is a small Game Boy / Game Boy Color emulator written in C99 with SDL3. It is a learning project: compact, direct, and focused on making real ROMs boot far enough to expose CPU, memory, PPU, timer, interrupt, joypad, and mapper bugs.

## Status

Works well enough to boot and run DMG and CGB ROMs, including Tetris, Pokemon Green, and many others. It is not a cycle-perfect emulator and not a polished end-user application yet.

Implemented or partially implemented:

- SM83 CPU opcode dispatch, including CB-prefixed opcodes
- T-cycle accurate timing (cycle tables, interrupt handling, HALT/STOP)
- Memory bus with ROM, VRAM, WRAM, OAM, HRAM, I/O, and IE handling
- MBC1, MBC3, and MBC5 cartridge banking
- Embedded Bootix DMG boot ROM path
- Scanline PPU for BG, window, and sprites
- Variable Mode 3 timing (sprite count + SCX fine scroll)
- One-shot LCD off/on state handling
- CGB detection, VRAM banking, WRAM banking
- CGB BG/OBJ palette registers (BCPS/BCPD/OCPS/OCPD)
- CGB BG attribute map (palette, flip, VRAM bank, priority)
- CGB OBJ attributes (palette, VRAM bank)
- DIV/TIMA/TMA/TAC timer with falling-edge accuracy
- APU: 4 sound channels (square + sweep, square, wave, noise)
- Frame sequencer, length/envelope/sweep counters
- NR10-NR52 register read/write
- SDL3 audio output (44100 Hz stereo)
- Battery-backed SRAM persistence (.sav load/save)
- VBlank, STAT, timer, and joypad interrupt plumbing
- SDL3 window, rendering, keyboard input, drag-and-drop ROM loading, pause overlay, and FPS title

Known rough edges:

- Graphics are still approximate in places (sprite 0 hit not implemented)
- HDMA/GDMA registers stored but transfers not executed
- Double-speed mode registers stored but not applied
- Accuracy is good enough for debugging, not for compatibility claims

## Requirements

- C99 compiler, tested with `gcc`
- SDL3 development package
- `pkg-config`
- `make`

On Arch/CachyOS:

```sh
sudo pacman -S sdl3 pkgconf make gcc
```

## Build

```sh
make
```

This creates:

```sh
./purplegb
```

Clean build output:

```sh
make clean
```

## Run

Open a ROM from the file picker:

```sh
./purplegb
```

Run a ROM directly:

```sh
./purplegb path/to/game.gb
```

You can also drag and drop a `.gb` or `.gbc` file onto the window.

## Controls

| Game Boy | Keyboard |
|----------|----------|
| D-pad | Arrow keys |
| A | Z |
| B | X |
| Start | Enter |
| Select | Right Shift |
| Pause menu | Escape |

## Project Layout

```text
src/
  cpu.c/.h       SM83 CPU core and opcode tables
  memory.c/.h    cartridge loading, boot ROM, bus, MBC, I/O
  ppu.c/.h       scanline LCD renderer (DMG + CGB)
  apu.c/.h       4-channel APU (square, wave, noise)
  timer.c/.h     DIV/TIMA timer
  joypad.c/.h    P1 register and SDL keyboard input
  interrupt.h    interrupt bit definitions/helpers
  main.c         SDL3 app loop
references/      specs, Bootix source, docs, and local ROMs
Makefile         tiny build script
```

## Notes

This repository is intentionally simple. No build system beyond `make`, no frontend, no config layer, no emulator framework. The point is to keep the hardware model visible enough to debug.

Commercial ROMs are not included. Put local test ROMs under `references/roms/` or pass a ROM path on the command line.

## License

No license file is currently included. Treat the code as source-available only until a license is added.
