# []($PROGRAM_NAME)XMouseD[]()

Minimal, efficient mouse wheel and extra buttons driver for Vampire/Apollo 68080 SAGA chipset.

[]($PROGRAM_NAME)XMouseD[]() is a lightweight background daemon that monitors USB mouse wheel and button from the SAGA chipset hardware, then injects standard NewMouse-compatible scroll events into the Amiga input system. This allows Amiga programs to scroll using your mouse wheel without special drivers or modifications.

**Compatibility Note:** Also works with IControl preferences on AmigaOS 3.2, or use any commodity for wheel and extra mouse features like [FreeWheel](https://aminet.net/package/util/mouse/FreeWheel) from Aminet.

## Compatibility

### ✅ Works On (SAGA Chipset with Apollo 68080)

[]($PROGRAM_NAME)XMouseD[]() requires Apollo 68080 accelerators with SAGA chipset and USB mouse port. Confirmed working on:

- **Vampire V4 Standalone**
- **A6000 Unicorn** <- not tested

And accelerators cards (not tested):

- **Vampire V4 Icedrake**
- **Vampire V4 Manticore**
- **Vampire V4 Salamander**
- **Vampire V4 Phoenix**

Requires USB mouse with scroll wheel (and extra buttons 4 & 5) connected to the mouse USB port.


### ✅ Compatible & Recommended OS

~ XMouseD [PROGRAM_NAME]~ is tested and supported on these operating systems:

- **AmigaOS 3.x** (3.0, 3.1, 3.1.4, 3.2, 3.9 - all versions supported)
- **ApolloOS 9.x** (AROS-based, fully compatible)
- **AROS** (should work, ApolloOS is AROS-based)


### ❌ Does NOT Work On

[]($PROGRAM_NAME)XMouseD[]() will NOT function on the following platforms (missing SAGA USB hardware):

- **Vampire V2** (SAGA exists but no USB port for mouse hardware support)
- **Classic Amiga** (A500, A1200, A4000, etc.)
- **Emulators** (UAE, WinUAE, FS-UAE)
- **Other accelerators** (Blizzard, Apollo 1260, PiStorm)
- **AmigaOS 4.x, MorphOS, AROS x86**

> **SAGA Chipset Only**: []($PROGRAM_NAME)XMouseD[]() works exclusively on Apollo accelerators with SAGA chipset and 68080 processor. Not compatible with classic Amiga, other accelerators, or emulators. 


## Installation

[]($PROGRAM_NAME)XMouseD[]() can be installed using the included Amiga Installer script or manually.

### Method 1: Amiga Installer (Recommended)

1. Download the []($PROGRAM_NAME)XMouseD[]() release archive
2. Extract to RAM: or any temporary location
3. Double-click the **Install** icon
4. Follow the on-screen prompts
5. Reboot when installation completes

The Installer will copy `[]($PROGRAM_EXE_NAME)XMouseD[]()` to `C:` and add it to your `S:User-Startup` automatically.

### Method 2: Manual Installation

1. Copy `[]($PROGRAM_EXE_NAME)XMouseD[]()` to `C:` (or `SYS:C/`)
2. Add to `S:User-Startup`:
   ```
   C:[]($PROGRAM_EXE_NAME)XMouseD[]() >NIL:
   ```
4. Restart or run `[]($PROGRAM_EXE_NAME)XMouseD[]()` manually


## Usage & Configuration

[]($PROGRAM_NAME)XMouseD[]() runs as a background daemon and can be controlled via command line arguments. Configuration uses a simple hex byte format.

### Basic Commands

Start, stop, or toggle []($PROGRAM_EXE_NAME)XMouseD[]() with these commands:


```bash
[]($PROGRAM_EXE_NAME)XMouseD[]()              # Toggle (start if stopped, stop if running)
[]($PROGRAM_EXE_NAME)XMouseD[]() START        # Start with default config (wheel+buttons)
[]($PROGRAM_EXE_NAME)XMouseD[]() STOP         # Stop daemon gracefully
[]($PROGRAM_EXE_NAME)XMouseD[]() 0xBYTE       # Start with custom config byte 
```

### Configuration

[]($PROGRAM_NAME)XMouseD[]() supports two polling modes:

**Adaptive Mode** - Smart polling that adjusts to your usage:
- When idle (reading, thinking): polls slowly to save CPU
- When scrolling: instantly speeds up for smooth response
- *Example*: Reading a document = minimal CPU usage, scrolling through code = instant response

**Normal Mode** - Constant polling rate:
- Same speed all the time, predictable behavior
- Choose your preferred reactivity level
- *Example*: Always ready at the same speed, no variation

Adaptive Modes (recommended):

```bash
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x13         ; BALANCED (default, responsive for everyday use)
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x03         ; COMFORT (occasional use, reactive when needed)
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x23         ; REACTIVE (instant response, fast reactivity)
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x33         ; ECO (minimal CPU, slower reactivity)
```

Normal Modes (constant reactivity):

```bash
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x53         ; ACTIVE (medium reactivity)
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x43         ; MODERATE (low reactivity)
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x63         ; INTENSIVE (high reactivity)
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x73         ; PASSIVE (very low reactivity)
```

### Hot Config Update

If []($PROGRAM_EXE_NAME)XMouseD[]() []($PROGRAM_EXE_NAME)XMouseD[]() is already running, launch with a new config byte to update settings instantly:

```bash
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x13         ; Start daemon or update config
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x23         ; Switch to REACTIVE mode (no restart needed!)
[]($PROGRAM_EXE_NAME)XMouseD[]() 0x00         ; Stop daemon
```

### Command Arguments

| Argument | Effect |
|----------|--------|
| *(none)* | Toggle: start if stopped, stop if running |
| `START` | Start daemon with default config (0x13) |
| `STOP` | Stop daemon gracefully |
| `0xBYTE` | Start with custom config (hex format) |

### Config Byte Reference

Advanced users can customize behavior with a 2-digit hex byte:

```
Bit 0 (0x01)     - Wheel enabled (sends scroll events)
Bit 1 (0x02)     - Extra buttons 4 & 5 enabled
Bits 2-3         - Reserved
Bits 4-6         - Modes:
                   000 = COMFORT   (adaptive mode)
                   001 = BALANCED  (adaptive mode) [DEFAULT]
                   010 = REACTIVE  (adaptive mode)
                   011 = ECO       (adaptive mode)
                   100 = MODERATE  (normal mode)
                   101 = ACTIVE    (normal mode)
                   110 = INTENSIVE (normal mode)
                   111 = PASSIVE   (normal mode)
Bit 7            - Reserved (do not use)
```

**Examples:**
- `0x13` = Wheel ON, Buttons ON, BALANCED adaptive (default)
- `0x53` = Wheel ON, Buttons ON, ACTIVE normal mode
- `0x03` = Wheel ON, Buttons ON, COMFORT adaptive
- `0x43` = Wheel ON, Buttons ON, MODERATE normal mode
- `0x23` = Wheel ON, Buttons ON, REACTIVE adaptive


## How It Works (Simple)

```
1. []($PROGRAM_EXE_NAME)XMouseD[]() reads USB wheel counter from SAGA hardware
2. Calculates movement delta
3. Sends standard scroll commands to Amiga
4. Apps recognize wheel and scroll normally
```

No special software in apps needed - wheel "just works" everywhere.


## Developer Documentation

For developers and technical users:

- **[VISION.md](docs/VISION.md)** - Design philosophy and project goals
- **[TECHNICAL.md](docs/TECHNICAL.md)** - Architecture, hardware interface, polling system, and build instructions

## License

**MIT License** - Free and open source. Use, modify, and distribute freely.

See [LICENSE](LICENSE) for full legal text.

---

## Support & Feedback

Found a bug? Have a feature request? Open an issue on GitHub.
