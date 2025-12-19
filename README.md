# []($PROGRAM_NAME)XMouseD[]()

[]($PROGRAM_DESCRIPTION)Extended mouse driver for Apollo 68080 SAGA chipset[]()


-----------------------------------------------------------------------------

## What is it ?

[]($PROGRAM_NAME)XMouseD[]() is a background daemon that reads USB mouse WHEEL and extra BUTTONS 
4 and 5 directly from SAGA hardware registers and injects standard NewMouse 
compatible scroll events into the Amiga input system. This enables mouse 
wheel scrolling in compatible Amiga applications.

For best experience on Workbench 3.1/3.9 use []($PROGRAM_NAME)XMouseD[]() with FreeWheel or 
similar commodities that improve integration of wheel and buttons mapping. 

On AmigaOS 3.2, enabling the "scroll wheel enabled" option in IControl 
improves how applications receive and handle wheel events. Without such 
tools the wheel works, but integration and extra features may be limited.


*** Note ***

avoid running []($PROGRAM_NAME)XMouseD[]() at the same time as ApolloWheel (or tools that also 
read SAGA mouse registers), since they can interfere with each other.


## Compatibility

### ✅ Works On (SAGA Chipset with Apollo 68080)

[]($PROGRAM_NAME)XMouseD[]() requires Apollo 68080 accelerators with SAGA chipset. 

Confirmed working on:

- Vampire V4 Standalone

Should work on *A6000 Unicorn* (not tested)

And V4 accelerators cards (not tested):

- Icedrake
- Manticore
- Salamander
- Phoenix


### ✅ Compatible & Recommended OS

[]($PROGRAM_NAME)XMouseD[]() is tested and supported on these operating systems:

- AmigaOS 3.x (3.0, 3.1, 3.1.4, 3.2, 3.9 - all versions supported)
- ApolloOS 9.x (AROS-based, fully compatible)
- AROS (should work, ApolloOS is AROS-based)


### ❌ Does NOT Work On

[]($PROGRAM_NAME)XMouseD[]() will NOT function on the following platforms:

- Vampire V2 (SAGA exists but no USB port for mouse hardware support)
- Classic Amiga (A500, A1200, A4000, etc.)
- Emulators (UAE, WinUAE, FS-UAE)
- Other accelerators (Blizzard, Apollo 1260, PiStorm)
- AmigaOS 4.x, MorphOS, AROS x86


## Installation

[]($PROGRAM_NAME)XMouseD[]() can be installed using the included Installer script or manually.

### Method 1: Amiga Installer (Recommended)

1. Download the []($PROGRAM_NAME)XMouseD[]() release archive
2. Extract to RAM: or any temporary location
3. Double-click the `Install XMouseD` icon
4. Follow the on-screen prompts
5. Reboot when installation completes

The Installer will copy `XMouseD` to `C:` and add it to your `S:User-Startup` automatically.

### Method 2: Manual Installation

1. Copy `XMouseD` to `C:` (or `SYS:C/`)
2. Add to `S:User-Startup`:
   ```
   # BEGIN XMouseD
   C:XMouseD >NIL:
   # END XMouseD
   ```
4. Restart or run `XMouseD` manually


## Usage & Configuration

See **[USAGE.md](docs/USAGE.md)** for complete command reference, configuration modes, and examples.


## Developer Documentation

For developers and technical users:

- **[VISION.md](docs/VISION.md)** - Design philosophy and project goals
- **[TECHNICAL.md](docs/TECHNICAL.md)** - Architecture, hardware interface, polling system, and build instructions


## Support & Feedback

Found a bug? Have a feature request? Open an issue on GitHub.

For bug reports, feature requests, or questions:
https://github.com/vbuzzano/XMouse-Saga

Special thanks to Apollo Team, Amiga community, and VBCC developers.

Author: []($PROGRAM_AUTHOR)Vincent Buzzano[]()

---

## License

**MIT License** - Free and open source. Use, modify, and distribute freely.

See [LICENSE](LICENSE) for full legal text.

