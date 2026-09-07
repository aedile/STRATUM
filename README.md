# STRATUM

**Namco's 1982 Dig Dug, emulated on an ESP32-C6 Fiesta medal.** Three Z80s, the
Namco WSG, the 06XX bus controller and its two custom I/O chips — running at
the cabinet's 60.6 Hz.

*Stratum* is a layer of rock or soil. Dig Dug is a game about going through
them.

A San Antonio Fiesta medal is a collectible pin. This one plays Dig Dug.

---

## 🎮 Quick Start Guide

### How to Play

You hold the medal upright, like a phone. Dig Dug's monitor was already
vertical, so the picture stands up the same way you do.

| Control | What it does |
|---|---|
| **Tilt any of four ways** | Dig in that direction |
| **Middle button** | The pump |
| **Power button, short press** | Insert a coin and start |
| **Power button, hold 1 second** | Power off |

Only the axis you have tilted further counts, so a diagonal never produces two
directions — that is what the four-way gate on the cabinet's stick did
mechanically.

Pump up Pooka and Fygar until they burst, or drop a rock on them. Fygar breathes
fire down the tunnel he is facing, so approach him from above or below.

Tilt is measured against however you are holding it right now. Coin up to
re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**It digs the wrong way.** Each axis is one sign in `main/input.cpp`
(`X_SIGN`, `Y_SIGN`).

**No sound during the attract sequence.** Correct at the factory settings —
demo sounds are a DIP switch, and it is off. `dd_set_dips()` in
`main/main.cpp` changes it.

---

## 🔨 Building Your Own

```sh
git clone https://github.com/aedile/STRATUM.git
cd STRATUM
python3 tools/convert_roms.py /path/to/digdug
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker build
cd build_docker && esptool --chip esp32c6 -p /dev/cu.usbmodemXXXX \
    -b 460800 write_flash @flash_args
```

Flashing has to run **from inside `build_docker`** and **from the host** —
Docker Desktop on macOS cannot reach USB.

### The ROMs

Not included. You need MAME's `digdug` set. The `51xx.bin` and `53xx.bin` MCU
ROMs in that set are **not used** — those chips are modelled at a high level
here, so the converter ignores them.

---

## 🔬 Technical Details

### The original hardware

Three Z80s at 3.072 MHz sharing one memory map, the Namco 3-voice WSG, and the
06XX bus controller fronting two custom microcontrollers: a **51XX** for the
joystick, coins and credits, and a **53XX** whose only job is reading the DIP
switches. That is Galaga's arrangement, minus the starfield generator and with
a 53XX where Galaga has the 54XX noise chip — so the machine model here is a
port of this project family's Galaga core.

### The background is a ROM, not a tilemap

The dirt you dig through is not something the CPU draws. It is a 4 KB ROM that
says which tile goes at each position — four screens' worth, selected by two
bits of a video latch — and each tile's own top nibble picks its colour. The
CPU only ever writes the *text* layer, and a tunnel appears because a character
cell over the dirt turns blank.

Colour goes through two lookup PROMs into a 32-entry palette. Sprites land in
the upper 16 entries and are transparent on colour 0x1F.

### Two bugs worth recording

**The 53XX handed back the wrong DIP bank, and one of the bits is "Freeze".**
The game booted, ran its self-test, started all three CPUs, took every
interrupt correctly — and then sat there with a completely static screen. It
looked like a crash. It was the game doing exactly what it was told: DSWB bit 5
is a freeze switch, and a model that returns DSWA for both banks will sooner or
later hand the game a bit it did not mean to set. The chip returns one whole
bank per read, not nibbles.

**Dig Dug wires the 51XX's ports differently from Galaga.** The joystick is on
port 0 here and the buttons and coins on ports 2 and 3; Galaga has it the other
way round. That matters in two different ways at once. In *switch mode* the
chip hands the ports back raw and the game parses them in hardware order. In
*credit mode* the chip does the coin arithmetic itself, and the model — which
came from MAME's historical high-level 51XX, written against Galaga — wants the
coin/start byte and the joystick nibble regardless of which pins they arrived
on. So the two modes read the same four ports through different lenses.

### Making it fit

Emulating three Z80s leaves little room. Two changes took the frame rate from
about 35 to between 50 and 60:

**The third CPU's idle loop.** It spends over 80% of its life at

```
00B0  LD SP,8B80
00B3  JR 00B0
```

waiting for the NMI that gives it work. Reloading the stack pointer to the same
value repeatedly is not observable, so those cycles are handed back. The
*second* CPU's loop is deliberately left alone: it looks similar but calls a
routine that polls a mailbox in shared RAM the main CPU writes, so skipping
ahead could step over a message.

**Blank character cells.** Most of the screen is dirt with nothing written over
it. A character with no set pixels skips the per-pixel overlay test entirely.

Both were checked by rendering a gameplay frame before and after and confirming
the output was byte-identical.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  digdug.c        three Z80s, the latches, 06XX/51XX/53XX, interrupt timing
  digdug_video.c  the ROM-driven background, the text layer, sprites
  digdug_sound.c  Namco WSG
  z80/            Marat Fayzullin's Z80 (non-commercial — see notices)
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop; frames to PPM, audio to WAV
tools/          ROM converter
```

## 💻 Running It on Your Computer

```sh
cd host && make
./harness /tmp/out 46 --every 4 --wav /tmp/dd.wav \
    --script "22.0:coin=1,23.2:coin=0,25.0:start=1,26.0:start=0,30.0:down=1,33.0:down=0"
```

The harness writes the **native** 288×224 frame, which is the picture lying on
its side — the cabinet's monitor is rotated and the medal's renderer does that
rotation. Turn the PPM 90° to look at it the right way up.

Hold the coin for at least a second; a shorter pulse can fall between the
51XX's credit-mode polls.

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches | `dd_set_dips()` in `main/main.cpp` — `0x99, 0x24` is the factory setting |
| Tilt direction | `X_SIGN`, `Y_SIGN` in `main/input.cpp` |
| Dig threshold | `DIG_DEG` in `main/input.cpp` |

## 📌 Status and Known Gaps

The game runs at full speed — 60.6 Hz, correct timing — with sound.

- **The panel refreshes at 50 to 60 of those frames** depending on how busy the
  scene is. The game logic and its timing are correct.
- Free heap is about 32 KB. The expanded sprite table is 64 KB of that; it
  could be packed two pixels to a byte if anything else ever needs the room.
- The high-score EAROM is RAM-backed, so scores do not survive a power cycle.
- Cocktail flip-screen is not applied.
- The 51XX and 53XX are behavioural models, not MCU emulation.

## 📄 Legal Notice

### ROM files

No ROMs here. Dig Dug is © 1982 Namco. This project ships a converter, not a
game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model, video
and WSG are written from MAME (BSD-3-Clause). **`core/z80/` is Marat
Fayzullin's Z80 emulator, which its author licenses for non-commercial use
only** — that term applies to this repository as a whole in any commercial
context.

### Disclaimer

Not affiliated with, endorsed by, or connected to Namco, Bandai Namco, their
successors, or the Fiesta San Antonio Commission.

## 🙏 Credits

The MAME team, whose `digdug.cpp` explains the ROM-driven background layer, and
whose historical `namco51.c` is the reason a high-level 51XX is possible at all.

## 📜 License

[0BSD](LICENSE) for the project's own code — but see the note above about the
Z80 core, which is non-commercial.
