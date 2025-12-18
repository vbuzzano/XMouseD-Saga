# Changelog

All notable changes to []($PROGRAM_NAME)XMouseD[]() will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [1.0.0] - 2025-12-18

Initial release of XMouseD - Extended mouse driver for Apollo 68080 SAGA chipset.

### Features
- **Mouse wheel scrolling** - Scroll up/down support with NewMouse compatibility
- **Extra buttons 4 & 5** - Full support for additional mouse buttons
- **Adaptive polling system** - 8 modes (4 adaptive + 4 normal) for optimal CPU/responsiveness balance
  - Adaptive modes: COMFORT, BALANCED, REACTIVE, ECO (smart polling)
  - Normal modes: MODERATE, ACTIVE, INTENSIVE, PASSIVE (constant rate)
- **Hot configuration** - Change settings without restarting daemon
- **Debug mode** - Toggle debug console at runtime for troubleshooting
- **Lightweight** - ~6KB executable with minimal CPU footprint

### Usage
- `XMouseD` - Toggle daemon (start/stop)
- `XMouseD START` - Start with default config
- `XMouseD STOP` - Stop daemon
- `XMouseD 0xBYTE` - Start/update with custom config byte

### Technical
- Singleton daemon with public message port
- Timer-based polling (adaptive or fixed intervals)
- Direct SAGA hardware register access ($DFF212)
- Input.device event injection (IECLASS_RAWKEY + IECLASS_NEWMOUSE)
- 2-second message timeout for reliable communication
- Proper resource cleanup on exit

### Compatibility
- Vampire V4 Standalone, A6000 (Apollo 68080 SAGA chipset)
- AmigaOS 3.x (3.0+), ApolloOS 9.x
- USB mouse with wheel and extra buttons

### Documentation
- AmigaGuide manual (XMouseD.guide)
- Aminet-ready readme (XMouseD.readme)
- Technical documentation (docs/TECHNICAL.md)
- Installer script included
- Wrap-around handling for 8-bit wheel counter delta calculation
- Resource cleanup on daemon exit (timer, input.device, ports)

### Technical
- SAGA wheel register: 0xDFF212 (bits 7-0 = counter, bits 8-9 = buttons 4 & 5)
- Event injection: IECLASS_RAWKEY + IECLASS_NEWMOUSE with codes 0x7A/0x7B
- Singleton detection via public port "XMouse_Port"
- Background daemon using WBM pattern (CLI module detachment)
- 68080 optimized: Instruction scheduling for SAGA dual-pipe architecture

## [0.1.0] - 2025-12-08

### Added
- Initial wheel support (UP/DOWN)
- Timer-based polling infrastructure
- Input.device event injection
- NewMouse protocol implementation (IECLASS_NEWMOUSE + IECLASS_RAWKEY)
- Singleton daemon with toggle start/stop
- Background process detachment
- 8-bit counter delta calculation with wrap-around handling

### Known Limitations
- Extra buttons 4 & 5 not yet implemented (hardware ready, code pending)
- Config byte partially implemented (wheel/buttons bits work, message system ready)
- No runtime configuration change (must restart daemon)
- No Installer script
- No AmigaGuide documentation
- No Preferences GUI
- No WBStartup icon
