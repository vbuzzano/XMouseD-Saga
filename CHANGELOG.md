# Changelog

All notable changes to []($PROGRAM_NAME)XMouseD[]() will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added
- **Adaptive button-hold fix**: System stays reactive while button is pressed
  - Prevents polling slowdown when holding mouse button 4/5
  - Ensures quick release detection even after long press

### Fixed
- **C2: Timeout in sendDaemonMessage()**: Added 2-second timeout to prevent shell freeze if daemon crashes
  - Uses timer.device + Wait() on multiple signals (replySig | timerSig)
  - Returns 0xFFFFFFFF on timeout, prints error message
  - Cleanup centralized with goto pattern

### Added
- **Buttons 4 & 5 support**: Read SAGA register bits 8-9 at $DFF212
  - Press/release detection with state tracking
  - Event injection: NM_BUTTON_FOURTH (0x7E), NM_BUTTON_FIFTH (0x7F)
  - IECODE_UP_PREFIX (0x80) for release events
- **Hot debug mode toggle**: Enable/disable debug console at runtime
  - `XMouseD 0x93` opens CON: window
  - `XMouseD 0x13` closes it (without restart)
- Config byte system (0xBYTE) for startup configuration
  - Bit 0: Wheel enable/disable
  - Bit 1: Extra buttons enable/disable (buttons 4 & 5)
  - Bits 4-5: Poll interval (5ms, 10ms, 20ms, 40ms)
  - Bit 7: Debug mode with CON: window
- Command line arguments: START, STOP, 0xBYTE
- Toggle mode (no argument): start if stopped, stop if running
- Message port system for daemon control (XMSG_CMD_*)
- **Hot config update**: Send new config byte to running daemon without restart
- Debug mode with CON: window for troubleshooting
- Conditional wheel processing based on CONFIG_WHEEL_ENABLED
- Support for buttons-only mode (wheel disabled, buttons enabled)

### Changed
- Unified event injection: always send both RawKey and NewMouse events when wheel enabled
- Simplified config byte validation: accepts either wheel OR buttons enabled
- Poll interval configurable via config byte (4 values: 5/10/20/40ms)
- Comment style: use `//` for all active code, `/* */` only for docstrings and block disabling
- Default config: 0x13 (wheel ON, buttons ON, 10ms, debug OFF)

### Fixed
- Timer initialization with proper microsecond intervals
- Config byte parsing with hex notation (0xBYTE)
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
