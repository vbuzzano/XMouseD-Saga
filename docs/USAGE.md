# Usage & Configuration

[]($PROGRAM_NAME)XMouseD[]() runs as a background daemon and can be controlled via command line
argments. Configuration uses a simple hex byte format.

## Basic Commands

Start, stop, or toggle XMouseD with these commands:

```shell
XMouseD           # Toggle (start if stopped, stop if running)
XMouseD START     # Start with default config (wheel+buttons)
XMouseD STOP      # Stop daemon gracefully
XMouseD 0xBYTE    # Start with custom config byte 
```

## Configuration

[]($PROGRAM_NAME)XMouseD[]() supports two polling modes:

**Adaptive Mode** - Smart polling that adjusts to your usage:
- When idle (reading, thinking): polls slowly to save CPU
- When scrolling: instantly speeds up for smooth response
- *Example*: Reading a document = minimal CPU usage, scrolling through code =
  instant response

**Normal Mode** - Constant polling rate:
- Same speed all the time, predictable behavior
- Choose your preferred reactivity level
- *Example*: Always ready at the same speed, no variation

### Adaptive Modes (recommended):

```shell
XMouseD 0x13      # BALANCED (default, responsive for everyday use)
XMouseD 0x03      # COMFORT (occasional use, reactive when needed)
XMouseD 0x23      # REACTIVE (instant response, fast reactivity)
XMouseD 0x33      # ECO (minimal CPU, slower reactivity)
```

### Normal Modes (constant reactivity):

```shell
XMouseD 0x53      # ACTIVE (medium reactivity)
XMouseD 0x43      # MODERATE (low reactivity)
XMouseD 0x63      # INTENSIVE (high reactivity)
XMouseD 0x73      # PASSIVE (very low reactivity)
```

## Hot Config Update

If []($PROGRAM_NAME)XMouseD[]() is already running, launch with a new config byte to update settings
instantly:

```shell
XMouseD 0x13      # Start daemon or update config
XMouseD 0x23      # Switch to REACTIVE mode (no restart needed!)
XMouseD 0x00      # Stop daemon
```

## Command Arguments

| Argument | Effect |
|----------|--------|
| *(none)* | Toggle: start if stopped, stop if running |
| `START` | Start daemon with default config (0x13) |
| `STOP` | Stop daemon gracefully |
| `0xBYTE` | Start with custom config (hex format) |

## Config Byte Reference

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

### Examples:
- `0x13` = Wheel ON, Buttons ON, BALANCED adaptive (default)
- `0x53` = Wheel ON, Buttons ON, ACTIVE normal mode
- `0x03` = Wheel ON, Buttons ON, COMFORT adaptive
- `0x43` = Wheel ON, Buttons ON, MODERATE normal mode
- `0x23` = Wheel ON, Buttons ON, REACTIVE adaptive
