# []($PROGRAM_NAME)XMouseD[]() Technical Documentation

[]($PROGRAM_DESCRIPTION)Extended mouse driver for Apollo 68080 SAGA chipset[]()

## Startup

**`_start()`** - Launches or controls the daemon:
1. Parse CLI arguments (START/STOP/TOGGLE or 0xBYTE)
2. `FindPort("XMouseD_Port")` to detect existing instance
3. If exists: send message (QUIT or SET_CONFIG)
4. Otherwise: `CreateNewProcTags(daemon)` + shell detachment (`cli_Module = 0`)

## Daemon Loop

**`daemon()`** - Background process:
```
daemon_Init()
  → Open devices (input, timer UNIT_VBLANK)
  → Create public port "XMouseD_Port"
  → Initialize hardware state (lastCounter, lastButtons)

Loop:
  Wait(CTRL+C | timer | port messages)
  
  If timer:
    → Read wheel/buttons SAGA
    → Inject events if changed
    → Update interval (adaptive or fixed)
    → Restart timer
  
  If message:
    → Process command (QUIT, SET_CONFIG, etc.)
    → Reply

daemon_Cleanup()
  → Close devices, remove port
```

---

## SAGA Hardware Reading

### Register $DFF212

```
Register $DFF212 (WORD)
BIT 15-10: unused
BIT 9:     Mouse Button 5 (1=pressed)
BIT 8:     Mouse Button 4 (1=pressed)
BIT 7-0:   Signed 8-bit wheel counter
```

### Wheel Reading

**Function:** `daemon_processWheel()`

```c
// Read high byte of register
BYTE current = SAGA_WHEELCOUNTER;  // $DFF213

// Calculate delta with signed 8-bit wrap-around handling
int delta = (int)(unsigned char)current - (int)(unsigned char)lastCounter;
if (delta > 127) delta -= 256;      // Wrap forward (255→0)
else if (delta < -128) delta += 256; // Wrap backward (0→255)

```

**Important:** Counter is persistent, driver must track delta between reads.

### Buttons 4/5 Reading

**Function:** `daemon_processButtons()`

```c
// Read bits 8-9
UWORD current = SAGA_MOUSE_BUTTONS & (SAGA_BUTTON4_MASK | SAGA_BUTTON5_MASK);

// Detect changes via XOR
UWORD changed = current ^ lastButtons;

```

---

## Event Injection

### Reusable Buffer

Single buffer `s_eventBuf` initialized once per timer tick:

```c
// In daemon() timer tick:
s_eventBuf.ie_Qualifier = PeekQualifier();  // Captured ONCE
s_eventBuf.ie_NextEvent = NULL;
s_eventBuf.ie_SubClass = 0;
// ... other fields set to 0

// In processWheel/processButtons: only change Code and Class
s_eventBuf.ie_Code = NM_WHEEL_UP;
s_eventBuf.ie_Class = IECLASS_NEWMOUSE;
injectEvent(&s_eventBuf);
```

### Double Injection

Each event is injected twice for maximum compatibility:
1. `IECLASS_RAWKEY` - Legacy apps (Miami, MultiView)
2. `IECLASS_NEWMOUSE` - Modern apps (IBrowse, browsers)

Codes used: `NM_WHEEL_UP/DOWN` (0x7A/0x7B), `NM_BUTTON_FOURTH/FIFTH` (0x7E/0x7F).

---

## Polling Modes

**Config byte bit 6:**
- `0` = **Adaptive** (adjusts polling interval based on activity)
- `1` = **Normal** (constant interval)

### Normal Mode

Simple: `s_pollInterval = burstUs` (constant based on chosen profile).

Available profiles (bits 4-5):
- MODERATE: 20ms (COMFORT profile)
- ACTIVE: 10ms (BALANCED profile, default)
- INTENSIVE: 5ms (REACTIVE profile)
- PASSIVE: 40ms (ECO profile)

### Adaptive Mode

```
State       | Interval    | Condition
------------|-------------|-----------------------------------
IDLE        | idleUs      | At rest (100-200ms depending on profile)
ACTIVE      | descending  | Activity detected, descending to burst
BURST       | burstUs     | Continuous activity (5-20ms)
TO_IDLE     | ascending   | Inactivity, ascending back to idle
```

**Parameters per profile:**
- `idleUs`: Interval at rest (CPU economy)
- `burstUs`: Maximum activity interval (reactivity)
- `stepDecUs`: Decrementation per tick (ACTIVE→BURST descent)
- `stepIncUs`: Incrementation per tick (TO_IDLE→IDLE ascent)
- `activeThreshold`: Inactivity threshold to leave ACTIVE
- `idleThreshold`: Inactivity threshold to leave BURST

**BALANCED example:**
```c
{ "BALANCED", "ACTIVE", 100000, 30000, 10000, 600, 1200, 500000, 1500000 }
//                       idle    active  burst  dec  inc   grace   idle-th
```

**Inactivity counter:**
```c
if (hadActivity)
    s_adaptiveInactive = 0;  // Reset
else
    s_adaptiveInactive += s_adaptiveInterval;  // Accumulate
```

---

## Public Port

**Name:** `"XMouseD_Port"`

**Used for:**
1. Instance detection (FindPort)
2. Runtime command sending

### Commands

| Command | Value | Response |
|---------|-------|----------|
| `XMSG_CMD_QUIT` (0) | - | 0 |
| `XMSG_CMD_SET_CONFIG` (1) | 0xBYTE | applied config |
| `XMSG_CMD_GET_STATUS` (2) | - | (config << 16) \| ms |


**Message Structure**

```c
struct XMouseMsg {
    struct Message msg;
    UBYTE command;    // XMSG_CMD_*
    ULONG value;      // Parameter
    ULONG result;     // Response
};
```


**Hot config update:**
```bash
xmoused 0x23  # Change config without restarting daemon
```

Daemon detects mode change (adaptive↔normal) or profile change, reinitializes adaptive system and restarts timer.

---

## VBCC Inline Pragmas

### Principle

VBCC generates inline library calls via offsets in proto headers:

```c
CreateMsgPort();  // → move.l _SysBase,a6; jsr -654(a6)
```

### Requirements

**Global bases:**
```c
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Device *InputBase;  // For PeekQualifier()
```

**Headers:**
```c
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/input.h>
```

**Build:** `-nostdlib` required, no `-lamiga`.

---

## Timer Implementation

### Setup

```c
OpenDevice(TIMERNAME, UNIT_VBLANK, ...)  // Frame-sync
```

### Restart Logic

**Normal mode:**
```c
TIMER_START(s_pollInterval);  // Direct, constant interval
```

**Adaptive mode:**
```c
s_pollInterval = getAdaptiveInterval(hadActivity);
AbortIO(s_TimerReq);   // Abort pending
WaitIO(s_TimerReq);    // Wait completion
TIMER_START(s_pollInterval);  // Restart with new interval
```

---

## Debug Mode

**Activation:** Bit 7 (0x80) of config byte.

```bash
xmoused 0x93  # Debug ON
xmoused 0x13  # Debug OFF
```

**Console:** `CON:0/0/640/200/XMouseD Debug/AUTO/CLOSE/WAIT`

**Logs:** Wheel/buttons events, adaptive transitions, config changes.

**Build:** Compile-time controlled via `#ifndef RELEASE`.

---

## Building From Source

### Prerequisites

**Required tools:**
- PowerShell 5.1+

**First-time setup:**
```powershell
> .\setup.ps1
```

This downloads and configures:
- VBCC toolchain (compiler, assembler, linker)
- Amiga NDK 3.9 headers and libraries
- Environment variables in `.env` file

### Build Commands

**Standard development build:**
```powershell
> make
> make build
> make rebuild
```

Output: `dist/xmoused` with debug symbols, optimizations enabled (-O3 -speed)


**Release build:**
```powershell
> make release
> make build MODE=release
> make rebuild MODE=release
```

**Clean build files:**
```powershell
make clean
```

**Compiler flags:**
- `-O3`: All optimizations (single-file program, no cross-module needed)
- `-speed`: Prefer speed over size (critical for polling loop responsiveness)
- `-sc`: Small code model (16-bit PC-relative calls)
- `-schedule`: Instruction scheduling for 68080 dual-pipeline
- `-cpu=68080`: Use 68080-specific instructions (Vampire/Apollo exclusive)

**Target:** `aos68k` (AmigaOS m68k, requires exec.library v39+)

### Troubleshooting

**Error: "vc not found"**
- Run `.\setup.ps1` to install VBCC
- Verify `.env` file exists with correct `VBCC` path

**Error: "Cannot find include file"**
- Check `VBCC` and `NDK39` paths in `.env`
- Verify `vendor/NDK39/Include/include_h/` exists

**Linker errors about undefined symbols:**
- Ensure global bases are declared: `SysBase`, `DOSBase`
- Check inline pragma headers are included
- Verify `-nostdlib` flag is present (no standard library linking)

**Wrong CPU instructions generated:**
- Confirm `CPU=68080` in Makefile (line 48)
- Release build uses same CPU target as dev build

### Upload to Vampire

**Prerequisites:**
- Vampire V4 running and accessible on network
- ApolloExplorer Connection Protocol (ACP) configured
- `APOLLO_V4_HOST` variable in `.env`

**Upload command:**
```powershell
make upload
```

This performs:
1. Build the executable (if needed)
2. Upload `dist/xmoused` to Vampire via ACP



