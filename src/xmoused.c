/*
 * XMouseD - Extended mouse driver daemon for Apollo 68080 SAGA chipset
 *
 * Light daemon for Vampire/Apollo SAGA chipset mouse wheel and
 * extra buttons (4/5) support. Polls hardware registers and injects
 * input events with adaptive or fixed polling intervals.
 *
 * (c) 2025 Vincent Buzzano
 * Licensed under MIT License
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/input.h>
#include <proto/timer.h>
#include <dos/dostags.h>
#include <devices/inputevent.h>
#include <devices/input.h>
#include <devices/timer.h>
#include <dos/dosextens.h>
#include <newmouse.h>

//===========================================================================
// Application Constants                                                     
//===========================================================================

// ---> BEGIN GENERATED PROGRAM_CONSTANTS
#define PROGRAM_NAME "XMouseD"
#define PROGRAM_VERSION "1.0"
#define PROGRAM_DATE "22.12.2025"
#define PROGRAM_AUTHOR "Vincent Buzzano"
#define PROGRAM_DESC_SHORT "SAGA eXtended Mouse Driver"
// <--- END GENERATED PROGRAM CONSTANTS

// Changing port name breaks compatibility with third-party tools.
#define DAEMON_PORT_NAME        "XMouseD_Port" // WARNING: Modify with caution!
#define DAEMON_DESC_SHORT       PROGRAM_NAME" - SAGA - Daemon"

// NORMAL Mode names
#define MODE_NAME_MODERATE      "MODERATE"
#define MODE_NAME_ACTIVE        "ACTIVE"
#define MODE_NAME_INTENSIVE     "INTENSIVE"
#define MODE_NAME_PASSIVE       "PASSIVE"

// ADAPTIVE Mode names
#define MODE_NAME_COMFORT       "COMFORT"
#define MODE_NAME_BALANCED      "BALANCED"
#define MODE_NAME_REACTIVE      "REACTIVE"
#define MODE_NAME_ECO           "ECO"

// $VERS: String
#define VERSION_STRING "$VER: " \
PROGRAM_NAME " " PROGRAM_VERSION " (" PROGRAM_DATE ") " PROGRAM_DESC_SHORT \ ", (c) " PROGRAM_AUTHOR

//===========================================================================
// User Messages
//===========================================================================

#define MSG_DAEMON_NOT_RUNNING      "daemon is not running"
#define MSG_DAEMON_RUNNING          "daemon running (config: 0x%02lx)"
#define MSG_DAEMON_STOPPED          "daemon stopped"
#define MSG_DAEMON_START_FAILED     "failed to start daemon"
#define MSG_CONFIG_UPDATED          "config updated to 0x%02lx"
#define MSG_UNKNOWN_ARGUMENT        "unknown argument: %s"

#define MSG_ERR_GET_STATUS_FAILED   "ERROR: Failed to get daemon status"
#define MSG_ERR_UPDATE_CONFIG       "ERROR: Failed to update daemon config"
#define MSG_ERR_STOP_DAEMON         "ERROR: Failed to stop daemon"
#define MSG_ERR_DAEMON_TIMEOUT      "ERROR: Daemon not responding (timeout)"

//===========================================================================
// Newmouse button codes for extra buttons 4 & 5                             
// not defined in standard newmouse.h                                        
//===========================================================================

#ifndef NM_BUTTON_FIFTH
#define NM_BUTTON_FIFTH     (0x7F)
#endif

//===========================================================================
// SAGA USB Mouse Registers                                                  
//===========================================================================

#define SAGA_MOUSE_BUTTONS      (*((volatile UWORD*)0xDFF212))
#define SAGA_WHEELCOUNTER       (*((volatile BYTE*)0xDFF212 + 1))

// Button bit masks in SAGA_MOUSE_BUTTONS (bits 8-9)
#define SAGA_BUTTON4_MASK       0x0100  // Bit 8
#define SAGA_BUTTON5_MASK       0x0200  // Bit 9


//===========================================================================
// XMouse Daemon Definitions
//===========================================================================

// Message commands for daemon control
#define XMSG_CMD_QUIT           0   // Stop daemon
#define XMSG_CMD_SET_CONFIG     1   // Set config byte
#define XMSG_CMD_GET_STATUS     2   // Get current status

// Daemon communication timeout
#define DAEMON_REPLY_TIMEOUT    2   // Seconds to wait for daemon reply


//===========================================================================
// Daemon Configuration Definitions
//===========================================================================

#define START_MODE_TOGGLE 0
#define START_MODE_START 1
#define START_MODE_STOP 2
#define START_MODE_CONFIG 3
#define START_MODE_STATUS 4

// Configuration byte bits
#define CONFIG_WHEEL_ENABLED    0x01    // Bit 0: Wheel enabled (RawKey + NewMouse) (0b00000001)
#define CONFIG_BUTTONS_ENABLED  0x02    // Bit 1: Extra buttons 4 & 5 enabled (0b00000010)
// Bits 2-3: Reserved
#define CONFIG_INTERVAL_SHIFT   4       // Bits 4-5: Mode selection (00=COMFORT, 01=BALANCED, 10=REACTIVE)
#define CONFIG_INTERVAL_MASK    0x30    // Mode mask (0b00110000)
#define CONFIG_FIXED_MODE       0x40    // Bit 6: Polling mode (0=adaptive, 1=normal/constant) (0b01000000)
// Bit 7: Reserved (bit 7 use for dev debug mode )

#define CONFIG_FEATURES_MASK (CONFIG_WHEEL_ENABLED | CONFIG_BUTTONS_ENABLED)

#define CONFIG_DEBUG_MODE       0x80    // Bit 7: Debug mode (0b10000000)  

#define DEFAULT_CONFIG_BYTE     0x13    // Default: Wheel ON, Buttons ON, BALANCED mode (01), Debug OFF (0b00010011)


//===========================================================================
// Variables
//===========================================================================

struct ExecBase *SysBase;              // Exec base (absolute 4)
struct DosLibrary *DOSBase;            // DOS library base
struct Device * InputBase;
static struct MsgPort *s_PublicPort;   // Singleton port
static struct MsgPort *s_InputPort;    // Input device port
static struct IOStdReq *s_InputReq;    // Input IO request
static struct MsgPort *s_TimerPort;    // Timer port
static struct timerequest *s_TimerReq; // Timer IO request

static BYTE s_lastWHCounter;           // Last wheel position
static int s_lastWHDelta;              // Last wheel delta
//static BYTE s_lastWHDir;               // Last wheel direction
static UWORD s_lastBTState;            // Last button state

static ULONG s_pollInterval;           // Timer interval (microseconds)
static UBYTE s_configByte;             // Configuration byte
static struct InputEvent s_eventBuf;   // Reusable event buffer

//===========================================================================
// Adaptive Polling System
//===========================================================================

// Polling states
#define POLL_STATE_IDLE      0  // At rest, interval = idleUs
#define POLL_STATE_ACTIVE    1  // Activity detected, interval descending toward burstUs
#define POLL_STATE_BURST     2  // Peak usage, interval = burstUs (floor)
#define POLL_STATE_TO_IDLE   3  // Returning to idle, interval ascending toward idleUs

// Adaptive mode configuration
typedef struct
{
    const char *adaptiveName;  // Adaptive mode name (COMFORT, BALANCED, REACTIVE, ECO)
    const char *normalName;    // Normal mode name (MODERATE, ACTIVE, INTENSIVE, PASSIVE)
    ULONG idleUs;             // Target interval when idle (microseconds)
    ULONG activeUs;           // Target interval when active (microseconds)
    ULONG burstUs;            // Target interval during burst usage (microseconds)
    ULONG stepDecUs;          // Microseconds to decrement per tick (ACTIVE → BURST)
    ULONG stepIncUs;          // Microseconds to increment per tick (TO_IDLE → IDLE)
    ULONG activeThreshold;    // Microseconds of inactivity before transitioning from ACTIVE to TO_IDLE (human grace period)
    ULONG idleThreshold;      // Microseconds of inactivity before transitioning from BURST to TO_IDLE
} AdaptiveMode;

// Mode table indexed by config bits 4-5
// 4 base modes x 2 variants (adaptive/normal via bit 6) = 8 total modes:
//   Adaptive (bit6=0): COMFORT, BALANCED, REACTIVE, ECO
//   Normal (bit6=1):   MODERATE (20ms), ACTIVE (10ms), INTENSIVE (5ms), PASSIVE (40ms)
static const AdaptiveMode s_adaptiveModes[] = 
{
    // COMFORT (00): Relaxed, tolerant
    { MODE_NAME_COMFORT, MODE_NAME_MODERATE, 150000, 60000, 20000, 1100, 15000, 500000, 500000 },
    
    // BALANCED (01): Balanced, universal - DEFAULT
    { MODE_NAME_BALANCED, MODE_NAME_ACTIVE, 100000, 30000, 10000, 600, 1200, 500000, 1500000 },
    
    // REACTIVE (10): Nervous, snappy
    { MODE_NAME_REACTIVE, MODE_NAME_INTENSIVE, 50000, 15000, 5000, 500, 250, 500000, 3000000 },

    // ECO (11): Low-power/quiet
    // Dynamic: 200→80→40ms | Fixed: 40ms (PASSIVE)
    // stepDecUs=2000, stepIncUs=4000, thresholds: 500ms/1.5s
    { MODE_NAME_ECO, MODE_NAME_PASSIVE, 200000, 80000, 40000, 2000, 4000, 500000, 1500000 }
};

// Adaptive state variables
static const AdaptiveMode *s_activeMode = NULL;
static UBYTE s_adaptiveState = POLL_STATE_IDLE;     // Current polling state
static ULONG s_adaptiveInterval = 0;                    // Current polling interval (microseconds)
static ULONG s_adaptiveInactive = 0;                   // Accumulated inactive time (microseconds)

// XMouse control message
struct XMouseMsg
{
    struct Message msg;
    UBYTE command;      // XMSG_CMD_* 
    ULONG value;        // Command parameter 
    ULONG result;       // Result/status 
};

#ifndef RELEASE
    static ULONG s_pollCount = 0;
    static BPTR s_debugCon = 0;
#endif

// Version string - uses APP_* macros
const char version[] = VERSION_STRING;


//===========================================================================
// Function Prototypes
//===========================================================================

static ULONG sendDaemonMessage(struct MsgPort *port, UBYTE cmd, ULONG value);
static inline int parseHexDigit(UBYTE c);
static inline BYTE parseArguments(void);
static inline const char* getModeName(UBYTE configByte);

static void daemon(void);
static inline void daemon_TimerStart(ULONG micros);
static inline void daemon_ProcessWheel(int delta);
static inline void daemon_ProcessButtons(UWORD state);
static inline ULONG daemon_GetAdaptiveInterval(BOOL hadActivity);
static BOOL daemon_Init(void);
static void daemon_Cleanup(void);


//===========================================================================
// Macros
//===========================================================================

// Simple print macros
#define Print(text) Printf(text "\n")
#define PrintF(fmtText,...) Printf(fmtText "\n", __VA_ARGS__)


//===========================================================================
// Debug Macros
//===========================================================================

#ifndef RELEASE
    // Log to debug console if debug mode enabled.
    #define DebugLog(fmt) \
        if (s_configByte & CONFIG_DEBUG_MODE) { \
            BPTR _old = SelectOutput(s_debugCon); \
            Printf(fmt "\n"); \
            Flush(s_debugCon); \
            SelectOutput(_old); \
        }

    #define DebugLogF(fmt, ...) \
        if (s_configByte & CONFIG_DEBUG_MODE) { \
            BPTR _old = SelectOutput(s_debugCon); \
            Printf(fmt "\n", __VA_ARGS__); \
            Flush(s_debugCon); \
            SelectOutput(_old); \
        }
#else
    #define DebugLog(fmt)         {} // No-op
    #define DebugLogF(fmt, ...)   {} // No-op
#endif


//===========================================================================
// Main thread functions
//===========================================================================

/**
 * Entry point.
 * Checks for existing instance and starts/stops daemon accordingly.
 * 
 * @return RETURN_OK on success, RETURN_FAIL on failure.
 */
LONG _start(void)
{
    struct MsgPort *existingPort = NULL;
    struct Process *proc = NULL;
    struct CommandLineInterface *cli = NULL;
    LONG exitCode = RETURN_OK;
    
    SysBase = *(struct ExecBase **)4L;
    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 36);
    if (!DOSBase) { return RETURN_FAIL; }

    // check if should start or stop the daemon
    BYTE startMode = parseArguments();

    // Check if XMouse is already running
    Forbid();
    existingPort = FindPort(DAEMON_PORT_NAME);
    Permit();

    if (startMode == START_MODE_STOP && !existingPort)
    {
        // Not running, nothing to do
        Print(MSG_DAEMON_NOT_RUNNING);
        goto cleanup;
    }

    if (startMode == START_MODE_STATUS || (startMode == START_MODE_START && existingPort))
    {
        ULONG status;
        
        if (!existingPort)
        {
            Print(MSG_DAEMON_NOT_RUNNING);
            exitCode = RETURN_WARN;
            goto cleanup;
        }
        
        // Query daemon status - result is config byte
        status = sendDaemonMessage(existingPort, XMSG_CMD_GET_STATUS, 0);
        
        if (status != 0xFFFFFFFF)
        {
            PrintF(MSG_DAEMON_RUNNING, status);
        }
        else
        {
            Print(MSG_ERR_GET_STATUS_FAILED);
            exitCode = RETURN_FAIL;
        }
        
        goto cleanup;
    }

    if (startMode == START_MODE_CONFIG && existingPort)
    {
        ULONG result = sendDaemonMessage(existingPort, XMSG_CMD_SET_CONFIG, s_configByte);
        if (result == 0)
        {
            // Always log in dev builds
            PrintF(MSG_CONFIG_UPDATED, (ULONG)s_configByte);
        }
        else
        {
            Print(MSG_ERR_UPDATE_CONFIG);
            exitCode = RETURN_FAIL;
        }
        goto cleanup;
    }
    
    if ((startMode == START_MODE_STOP || startMode == START_MODE_TOGGLE) && existingPort)
    {
        ULONG result;
        
        // Send QUIT message to daemon
        result = sendDaemonMessage(existingPort, XMSG_CMD_QUIT, 0);
        
        if (result == 0)
        {
            Print(MSG_DAEMON_STOPPED);
        }
        else
        {
            Print(MSG_ERR_STOP_DAEMON);
            exitCode = RETURN_FAIL;
        }
        goto cleanup;
    }

    // Create background process using WBM pattern
    if (CreateNewProcTags(
        NP_Entry, (ULONG)daemon,
        NP_Name, (ULONG)DAEMON_DESC_SHORT,
        NP_Priority, 0,
        TAG_DONE))
    {
        // Detach from shell by clearing CLI module - WBM pattern
        proc = (struct Process *)FindTask(NULL);
        if (proc->pr_CLI)
        {
            cli = BADDR(proc->pr_CLI);
            cli->cli_Module = 0;
        }

        // Start the daemon
        PrintF(MSG_DAEMON_RUNNING, (ULONG)s_configByte);

        goto cleanup;
    }
    
    Print(MSG_DAEMON_START_FAILED);
    exitCode = RETURN_FAIL;

cleanup:
    if (DOSBase)
    {
        CloseLibrary((struct Library *)DOSBase);
    }
    return exitCode;
}

/**
 * Send a message to the daemon and wait for reply with timeout.
 * If daemon doesn't reply within 2 seconds, returns FALSE.
 * @param port Daemon's public port
 * @param cmd Command to send
 * @param value Command parameter
 * @return TRUE on success, FALSE on timeout/error
 */
static ULONG sendDaemonMessage(struct MsgPort *port, UBYTE cmd, ULONG value)
{
    struct MsgPort *replyPort = NULL;
    struct XMouseMsg *msg = NULL;
    struct MsgPort *timerPort = NULL;
    struct timerequest *timerReq = NULL;
    ULONG result = 0xFFFFFFFF;  // Error by default
    ULONG replySig, timerSig, signals;
    
    // Create reply port
    replyPort = CreateMsgPort();
    if (!replyPort)
    {
        goto cleanup;
    }
    
    // Create timer for timeout (2 seconds)
    timerPort = CreateMsgPort();
    if (!timerPort)
    {
        goto cleanup;
    }
    timerReq = (struct timerequest *)CreateIORequest(timerPort, sizeof(struct timerequest));
    if (!timerReq)
    {
        goto cleanup;
    }
    if (OpenDevice(TIMERNAME, UNIT_VBLANK, (struct IORequest *)timerReq, 0))
    {
        goto cleanup;
    }
    
    // Allocate message
    msg = (struct XMouseMsg *)AllocMem(sizeof(struct XMouseMsg), MEMF_PUBLIC | MEMF_CLEAR);
    if (!msg)
    {
        goto cleanup;
    }
    
    // Setup message
    msg->msg.mn_Node.ln_Type = NT_MESSAGE;
    msg->msg.mn_Length = sizeof(struct XMouseMsg);
    msg->msg.mn_ReplyPort = replyPort;
    msg->command = cmd;
    msg->value = value;
    
    // Send message to daemon
    PutMsg(port, (struct Message *)msg);
    
    // Setup timeout: 2 seconds
    timerReq->tr_node.io_Command = TR_ADDREQUEST;
    timerReq->tr_time.tv_secs = DAEMON_REPLY_TIMEOUT;
    timerReq->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)timerReq);
    
    // Wait for reply OR timeout
    replySig = 1L << replyPort->mp_SigBit;
    timerSig = 1L << timerPort->mp_SigBit;
    signals = Wait(replySig | timerSig);
    
    if (signals & replySig)
    {
        // Reply received before timeout
        GetMsg(replyPort);
        result = msg->result;  // Return daemon's result
        
        // Abort timer
        AbortIO((struct IORequest *)timerReq);
        WaitIO((struct IORequest *)timerReq);
    }
    else if (signals & timerSig)
    {
        // Timeout: daemon didn't respond
        GetMsg(timerPort);
        Print(MSG_ERR_DAEMON_TIMEOUT);
        result = 0xFFFFFFFF;  // Timeout error
        // Message is still pending in daemon - nothing we can do
    }

cleanup:
    // Cleanup resources (safe even if NULL)
    if (msg)
    {
        FreeMem(msg, sizeof(struct XMouseMsg));
    }
    if (timerReq)
    {
        if (timerReq->tr_node.io_Device)
        {
            CloseDevice((struct IORequest *)timerReq);
        }
        DeleteIORequest((struct IORequest *)timerReq);
    }
    if (timerPort)
    {
        DeleteMsgPort(timerPort);
    }
    if (replyPort)
    {
        DeleteMsgPort(replyPort);
    }
    
    return result;
}

/**
 * Parse command line arguments and determine start mode.
 * Also parses optional config byte in hex format (0xBYTE).
 * @return START_MODE_START, START_MODE_STOP, or START_MODE_TOGGLE.
 */
static inline BYTE parseArguments(void)
{
    typedef STRPTR (*GetArgStrFunc)(void);
    GetArgStrFunc GetArgStr = (GetArgStrFunc)((UBYTE *)DOSBase + 0x114);
    STRPTR args = GetArgStr();
    UBYTE *p = (UBYTE *)args;
    int hi, lo;
    UBYTE configByte;
    
    // Skip leading spaces
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    
    // No argument = toggle mode
    if (*p == '\0' || *p == '\n')
    {
        s_configByte = DEFAULT_CONFIG_BYTE;
        return START_MODE_TOGGLE;
    }
    
    // Test STOP case-insensitive
    if ((p[0]|32)=='s' && (p[1]|32)=='t' && (p[2]|32)=='o' && (p[3]|32)=='p')
    {
        return START_MODE_STOP;
    }
    
    // Test START case-insensitive
    if ((p[0]|32)=='s' && (p[1]|32)=='t' && (p[2]|32)=='a' && (p[3]|32)=='r' && (p[4]|32)=='t')
    {
        // Check if START is followed by nothing, space, or newline
        if (p[5] == '\0' || p[5] == ' ' || p[5] == '\t' || p[5] == '\n')
        {
            // Store config byte for daemon to use
            s_configByte = DEFAULT_CONFIG_BYTE;
            return START_MODE_START;
        }
    }
    
    // Test STATUS case-insensitive (must come after START check)
    if ((p[0]|32)=='s' && (p[1]|32)=='t' && (p[2]|32)=='a' && (p[3]|32)=='t' && (p[4]|32)=='u' && (p[5]|32)=='s')
    {
        return START_MODE_STATUS;
    }
    
    // Test hex format: 0xBYTE
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        hi = parseHexDigit(p[2]);
        lo = parseHexDigit(p[3]);
        
        if (hi >= 0 && lo >= 0)
        {
            configByte = (UBYTE)((hi << 4) | lo);
            
#ifdef RELEASE
            // Force debug bit to 0 in release builds
            configByte &= ~CONFIG_DEBUG_MODE;
#endif
            
            // Check STOP conditions: neither wheel nor buttons enabled (bits 0-1)
            if ((configByte & CONFIG_FEATURES_MASK) == 0)
            {
                //PrintF("config 0x%02lx = STOP (wheel and buttons disabled)", (ULONG)configByte);
                return START_MODE_STOP;
            }
            
            // Store config byte for daemon to use
            s_configByte = configByte;

            return START_MODE_CONFIG;
        }
    }
    
    // Unknown argument
    PrintF(MSG_UNKNOWN_ARGUMENT, args);
    return START_MODE_TOGGLE;
}


//===========================================================================
// Daemon process functions
//===========================================================================

/**
 * Daemon main function.
 * This function runs in a separate process.
 */
static void daemon(void)
{
    ULONG timerSig, portSig, signals;
    struct XMouseMsg *msg;
    BOOL quit = FALSE;
  
    if (daemon_Init()) 
    {
#ifndef RELEASE
        // Open debug console if debug mode enabled
        if (s_configByte & CONFIG_DEBUG_MODE)
        {
            s_debugCon = Open("CON:0/0/640/200/"PROGRAM_NAME" Debug/AUTO/CLOSE", MODE_NEWFILE);
            
            DebugLog("daemon started");
            DebugLogF("Mode: %s", getModeName(s_configByte));
            
            if (s_configByte & CONFIG_FIXED_MODE)
            {
                DebugLogF("Poll: %ldms (normal)", (LONG)(s_pollInterval / 1000));
            }
            else
            {
                DebugLogF("Poll: %ld->%ld->%ldms (adaptive)", 
                          (LONG)(s_activeMode->idleUs / 1000),
                          (LONG)(s_activeMode->activeUs / 1000),
                          (LONG)(s_activeMode->burstUs / 1000));
            }
            
            DebugLog("---");
        }
#endif        
        daemon_TimerStart(s_pollInterval);
        
        timerSig = 1L << s_TimerPort->mp_SigBit;
        portSig = 1L << s_PublicPort->mp_SigBit;
        
        for (;;)
        {
            // Wait for CTRL-C, timer signal, or messages
            signals = Wait(SIGBREAKF_CTRL_C | timerSig | portSig);

            if (signals & SIGBREAKF_CTRL_C)
            {
                break;
            }
            
            // Process messages from public port
            if (signals & portSig)
            {
                while ((msg = (struct XMouseMsg *)GetMsg(s_PublicPort)))
                {
                    switch (msg->command)
                    {
                        case XMSG_CMD_QUIT:
                            quit = TRUE;
                            msg->result = 0;  // Success
                            break;
                            
                        case XMSG_CMD_SET_CONFIG:
                            {
                                UBYTE oldConfig = s_configByte;
                                UBYTE newConfig = (UBYTE)msg->value;
                                UBYTE oldInterval = (oldConfig & CONFIG_INTERVAL_MASK) >> CONFIG_INTERVAL_SHIFT;
                                UBYTE newInterval = (newConfig & CONFIG_INTERVAL_MASK) >> CONFIG_INTERVAL_SHIFT;
                                
#ifdef RELEASE
                                // Force debug bit to 0 in release builds
                                newConfig &= ~CONFIG_DEBUG_MODE;
#endif
                                
                                s_configByte = newConfig;
                                msg->result = 0;  // Success
                                
                                DebugLogF("Config changed: 0x%02lx -> 0x%02lx", (ULONG)oldConfig, (ULONG)newConfig);
                                
                                // If mode changed, reinitialize adaptive system
                                if (oldInterval != newInterval || 
                                    ((oldConfig ^ newConfig) & CONFIG_FIXED_MODE))
                                {
                                    UBYTE modeIndex = (newConfig & CONFIG_INTERVAL_MASK) >> CONFIG_INTERVAL_SHIFT;
                                                                        
                                    s_activeMode = &s_adaptiveModes[modeIndex % 4];
                                    
                                    // Reinitialize based on new mode
                                    if (newConfig & CONFIG_FIXED_MODE)
                                    {
                                        // Normal mode: use burstUs
                                        s_adaptiveInterval = s_activeMode->burstUs;
                                        s_pollInterval = s_activeMode->burstUs;
                                        DebugLogF("Mode changed: %s (fixed %ldms)", s_activeMode->normalName, (LONG)(s_pollInterval / 1000));
                                    }
                                    else
                                    {
                                        // Adaptive mode: start from idle
                                        s_adaptiveState = POLL_STATE_IDLE;
                                        s_adaptiveInterval = s_activeMode->idleUs;
                                        s_pollInterval = s_activeMode->idleUs;
                                        DebugLogF("Mode changed: %s (adaptive)", s_activeMode->adaptiveName);
                                    }
                                    
                                    s_adaptiveInactive = 0;
                                    
                                    // Restart timer with new interval
                                    AbortIO((struct IORequest *)s_TimerReq);
                                    WaitIO((struct IORequest *)s_TimerReq);
                                    daemon_TimerStart(s_pollInterval);
                                }
                                
#ifndef RELEASE
                                // Handle debug mode change
                                if ((oldConfig & CONFIG_DEBUG_MODE) && !(newConfig & CONFIG_DEBUG_MODE))
                                {                                    // Debug mode disabled - close console
                                    if (s_debugCon)
                                    {
                                        Close(s_debugCon);
                                        s_debugCon = 0;
                                    }
                                }
                                else if (!(oldConfig & CONFIG_DEBUG_MODE) && (newConfig & CONFIG_DEBUG_MODE))
                                {
                                    // Debug mode enabled - open console
                                    if (!s_debugCon)
                                    {
                                        s_debugCon = Open("CON:0/0/640/200/"PROGRAM_NAME" Debug/AUTO/CLOSE", MODE_NEWFILE);
                                        DebugLog("Debug mode enabled");
                                    }
                                }
#endif
                            }
                            break;
                            
                        case XMSG_CMD_GET_STATUS:
                            // Return config byte only
                            DebugLogF("Status requested: config=0x%02lx", (ULONG)s_configByte);
                            msg->result = (ULONG)s_configByte;
                            break;
                            
                        default:
                            msg->result = 0xFFFFFFFF;  // Error
                            break;
                    }
                    
                    ReplyMsg((struct Message *)msg);
                }
                
                if (quit)
                {
                    break;
                }
            }
        
            // Timer signal: poll & inject events
            if (signals & timerSig)
            {
                BOOL hadActivity, hadWHActivity, hadBTActivity;
                UWORD currentBTState;
                //BYTE currentWHDir;
                BYTE currentWHCounter;
                int currentWHDelta;

                // Prepare wheel delta if WH enabled
                if (s_configByte & CONFIG_WHEEL_ENABLED)
                {
                    currentWHCounter = SAGA_WHEELCOUNTER;

                    if (s_lastWHCounter != currentWHCounter)
                    {
                        // Calculate delta with wrap-around handling
                        currentWHDelta = (int)(unsigned char)currentWHCounter - (int)(unsigned char)s_lastWHCounter;
                        if (currentWHDelta != 0)
                        {
                            if (currentWHDelta > 127)
                            {
                                currentWHDelta -= 256;
                            }
                            else if (currentWHDelta < -128) 
                            { 
                                currentWHDelta += 256;
                            }
                        }
                        hadWHActivity = TRUE;
                    }
                    else
                    {
                        hadWHActivity = s_lastWHDelta != 0;
                        currentWHDelta = 0;
                    }

                    // currentWHDir = (currentWHDelta == 0 ? 0 : (currentWHDelta > 0) ? 1 : -1);
                }

                // get currentButtons
                if (s_configByte & CONFIG_BUTTONS_ENABLED) {
                    currentBTState = SAGA_MOUSE_BUTTONS & (SAGA_BUTTON4_MASK | SAGA_BUTTON5_MASK);
                    
                    // button has activity when state change or any button is pressed
                    hadBTActivity = (currentBTState != s_lastBTState) || (currentBTState != 0);
                }

                // determine if ther is an activity
                hadActivity = hadWHActivity || hadBTActivity;

                if (hadActivity) 
                {
                    // Initialize event buffer (reused by both wheel and button processing)
                    s_eventBuf.ie_NextEvent = NULL;
                    s_eventBuf.ie_SubClass = 0;
                    s_eventBuf.ie_Qualifier = PeekQualifier();  // Capture current qualifier state
                    s_eventBuf.ie_X = 0;
                    s_eventBuf.ie_Y = 0;
                    s_eventBuf.ie_TimeStamp.tv_secs = 0;
                    s_eventBuf.ie_TimeStamp.tv_micro = 0;
                
                    // Check for wheel activity
                    if (hadWHActivity)
                    {
                        daemon_ProcessWheel(currentWHDelta);
                    }

                    // Check for button activity
                    if (hadBTActivity)
                    {
                        daemon_ProcessButtons(currentBTState);
                    }
                }

                // Update adaptive interval and restart timer
                if (s_configByte & CONFIG_FIXED_MODE)
                {
                    // Fixed mode: constant interval, direct restart
                    daemon_TimerStart(s_pollInterval);
                }
                else
                {
                    // Adaptive mode: update interval and restart
                    // No need for AbortIO/WaitIO here - timer already completed (we got the signal)
                    s_pollInterval = daemon_GetAdaptiveInterval(hadActivity);
                    daemon_TimerStart(s_pollInterval);
                }
                
#ifndef RELEASE
                if (s_configByte & CONFIG_DEBUG_MODE)
                {
                    s_pollCount++;
                    
                    // Log every 1000 timer polls (e.g., every 10 seconds at 10ms interval)
                    //if (s_pollCount % 1000 == 0)
                    //{
                    //    DebugLogF("Timer polls: %lu (interval: %ldms)", s_pollCount, (LONG)(s_pollInterval / 1000));
                    //}
                }
#endif

                // Update last values
                s_lastWHCounter = currentWHCounter;
                s_lastWHDelta   = currentWHDelta;
                //s_lastWHDir     = currentWHDir;
                s_lastBTState   = currentBTState;
            }
        }
    }

    daemon_Cleanup();
}

/**
 * Start the timer with the specified timeout in microseconds.
 * @param micros Timeout in microseconds.
 */
static inline void daemon_TimerStart(ULONG micros)
{
    s_TimerReq->tr_node.io_Command = TR_ADDREQUEST;
    s_TimerReq->tr_time.tv_secs = micros / 1000000;
    s_TimerReq->tr_time.tv_micro = micros % 1000000;
    SendIO((struct IORequest *)s_TimerReq);
}

/**
 * Inject input event to input.device.
 * Reuses caller's InputEvent struct to avoid repeated allocations.
 * @param ev Pre-filled InputEvent struct
 */
static inline void injectEvent(struct InputEvent *ev)
{
    //DebugLogF("  injectEvent: class=0x%02lx code=0x%02lx qualifier=0x%04lx", 
    //          (ULONG)ev->ie_Class, (ULONG)ev->ie_Code, (ULONG)ev->ie_Qualifier);
    
    s_InputReq->io_Command = IND_WRITEEVENT;
    s_InputReq->io_Data = (APTR)ev;
    s_InputReq->io_Length = sizeof(struct InputEvent);
    
    DoIO((struct IORequest *)s_InputReq);
}

/**
 * Process wheel movement and inject events if needed.
 * Reuses s_eventBuf (only ie_Code and ie_Class are modified).
 * @param delta Current wheel delta
 */
static inline void daemon_ProcessWheel(int delta)
{
    if (delta == 0) return;
        
    // Determine direction and repeat count
    UWORD code = (delta > 0) ? NM_WHEEL_UP : NM_WHEEL_DOWN;
    int count = ((delta > 0) ? delta : -delta);
    
    DebugLogF("Wheel: %s delta=%ld", (delta > 0) ? "UP" : "DOWN", (LONG)delta);
    
    // Reuse s_eventBuf (only ie_Code and ie_Class change)
    s_eventBuf.ie_Code = code;
    
    // Repeat events based on delta
    for (int i = 0; i < count; i++)
    {
        // Inject both RAWKEY - Modern apps
        s_eventBuf.ie_Class = IECLASS_RAWKEY;
        injectEvent(&s_eventBuf);
        
        // and NEWMOUSE - Legacy apps
        s_eventBuf.ie_Class = IECLASS_NEWMOUSE;
        injectEvent(&s_eventBuf);
    }

    // Log wheel event
    //DebugLogF("Wheel: delta=%ld dir=%s count=%ld", 
    //         (LONG)delta, 
    //         (code == NM_WHEEL_UP) ? "UP" : "DOWN", 
    //         (LONG)count);
}

/**
 * Process buttons and inject events if needed.
 * Reuses s_eventBuf (only ie_Code and ie_Class are modified).
 * @param state Current button state (already read and masked from SAGA_MOUSE_BUTTONS)
 */
static inline void daemon_ProcessButtons(UWORD state)
{
    UWORD changed;
    UWORD code;
    
    // Use provided current value (already read and masked in main loop)
    changed = state ^ s_lastBTState;
    
    if (changed)
    {
        if (changed & SAGA_BUTTON4_MASK)
        {
            code = NM_BUTTON_FOURTH | ((state & SAGA_BUTTON4_MASK) ? 0 : IECODE_UP_PREFIX);
        
            DebugLogF("Button 4: %s", (state & SAGA_BUTTON4_MASK) ? "PRESS" : "RELEASE");

            s_eventBuf.ie_Code = code;
            
            s_eventBuf.ie_Class = IECLASS_RAWKEY;
            injectEvent(&s_eventBuf);
            
            s_eventBuf.ie_Class = IECLASS_NEWMOUSE;
            injectEvent(&s_eventBuf);
        }
        
        if (changed & SAGA_BUTTON5_MASK)
        {
            code = NM_BUTTON_FIFTH | ((state & SAGA_BUTTON5_MASK) ? 0 : IECODE_UP_PREFIX);

            DebugLogF("Button 5: %s", (state & SAGA_BUTTON5_MASK) ? "PRESS" : "RELEASE");

            s_eventBuf.ie_Code = code;
            
            s_eventBuf.ie_Class = IECLASS_RAWKEY;
            injectEvent(&s_eventBuf);
            
            s_eventBuf.ie_Class = IECLASS_NEWMOUSE;
            injectEvent(&s_eventBuf);
        }
    }
}

/**
 * Update adaptive polling interval based on activity.
 * State machine: IDLE → ACTIVE → BURST → TO_IDLE → IDLE
 * Only called in adaptive mode (bit 6 = 0). Normal mode bypasses this function.
 * @param hadActivity TRUE if wheel/button activity detected this tick
 */
static inline ULONG daemon_GetAdaptiveInterval(BOOL hadActivity)
{
    const AdaptiveMode *mode = s_activeMode;
    ULONG oldUs = s_adaptiveInterval;
    UBYTE oldState = s_adaptiveState;
    
    // Accumulate inactive time by adding current poll interval
    if (hadActivity)
    {
        s_adaptiveInactive = 0;  // Reset accumulated inactive time
    }
    else
    {
        s_adaptiveInactive += s_adaptiveInterval;  // Add current interval to inactive time
    }
    
    // State machine
    switch (s_adaptiveState)
    {
        case POLL_STATE_IDLE:
            if (hadActivity)
            {
                // Jump to ACTIVE
                s_adaptiveState = POLL_STATE_ACTIVE;
                s_adaptiveInterval = mode->activeUs;
#ifdef DEBUG_ADAPTIVE
                DebugLogF("[IDLE->ACTIVE] %ldus | InactiveUs=%ld", 
                          (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
#endif
            }
            break;
            
        case POLL_STATE_ACTIVE:
            if (hadActivity)
            {
                // Descend toward BURST every tick with activity
                if (s_adaptiveInterval > mode->burstUs)
                {
                    s_adaptiveInterval = (s_adaptiveInterval > mode->stepDecUs) ? (s_adaptiveInterval - mode->stepDecUs) : mode->burstUs;
                }
                
                // Reached BURST floor?
                if (s_adaptiveInterval <= mode->burstUs)
                {
                    s_adaptiveState = POLL_STATE_BURST;
                    s_adaptiveInterval = mode->burstUs;
#ifdef DEBUG_ADAPTIVE
                    DebugLogF("[ACTIVE->BURST] %ldus | InactiveUs=%ld", 
                              (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
#endif
                }
            }
            else
            {
                // Check accumulated inactivity in microseconds
                if (s_adaptiveInactive >= mode->activeThreshold)
                {
                    s_adaptiveState = POLL_STATE_TO_IDLE;
#ifdef DEBUG_ADAPTIVE
                    DebugLogF("[ACTIVE->TO_IDLE] %ldus | InactiveUs=%ld", 
                              (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
#endif
                }
            }
            break;
            
        case POLL_STATE_BURST:
            if (!hadActivity)
            {
                // Check accumulated inactivity in microseconds
                if (s_adaptiveInactive >= mode->idleThreshold)
                {
                    // Transition to TO_IDLE
                    s_adaptiveState = POLL_STATE_TO_IDLE;
#ifdef DEBUG_ADAPTIVE
                    DebugLogF("[BURST->TO_IDLE] %ldus | InactiveUs=%ld", 
                              (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
#endif
                }
            }
            break;
            
        case POLL_STATE_TO_IDLE:
            if (hadActivity)
            {
                // Return to ACTIVE
                if (s_adaptiveInterval > mode->activeUs)
                {
                    s_adaptiveInterval = mode->activeUs;
                }
                s_adaptiveState = POLL_STATE_ACTIVE;
#ifdef DEBUG_ADAPTIVE
                DebugLogF("[TO_IDLE->ACTIVE] %ldus | InactiveUs=%ld", 
                          (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
#endif
            }
            else
            {
                // Ascend toward IDLE every tick without activity
                if (s_adaptiveInterval < mode->idleUs)
                {
                    s_adaptiveInterval += mode->stepIncUs;
                    
                    // Clamp to idleUs ceiling
                    if (s_adaptiveInterval > mode->idleUs)
                    {
                        s_adaptiveInterval = mode->idleUs;
                    }
                }
                
                // Reached IDLE ceiling?
                if (s_adaptiveInterval >= mode->idleUs)
                {
                    s_adaptiveState = POLL_STATE_IDLE;
                    s_adaptiveInterval = mode->idleUs;
#ifdef DEBUG_ADAPTIVE
                    DebugLogF("[TO_IDLE->IDLE] %ldus | InactiveUs=%ld", 
                              (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
#endif
                }
            }
            break;
    }
    

#ifndef RELEASE
    // Log state changes (even without interval change)
    if (s_configByte & CONFIG_DEBUG_MODE)
    {
        const char *stateNames[] = {"IDLE", "ACTIVE", "BURST", "TO_IDLE"};
        
        // State changed?
        if (oldState != s_adaptiveState)
        {
            DebugLogF("Adaptive: [%s->%s] interval=%ldus", 
                      stateNames[oldState], stateNames[s_adaptiveState], 
                      (LONG)s_adaptiveInterval);
        }
    }
#endif

    return s_adaptiveInterval;
}

/**
 * Initialize daemon resources.
 * @return TRUE on success, FALSE on failure.
 */
static inline BOOL daemon_Init(void)
{
    SysBase = *(struct ExecBase **)4L;
    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 36);
    if (!DOSBase)
    {
        return FALSE;
    }

    // Create our public port
    s_PublicPort = CreateMsgPort();
    if (!s_PublicPort)
    {
        return FALSE;
    }
    s_PublicPort->mp_Node.ln_Name = DAEMON_PORT_NAME;
    s_PublicPort->mp_Node.ln_Pri = 0;
    AddPort(s_PublicPort);

    // Create input device for event injection    
    s_InputPort = CreateMsgPort();
    if (!s_InputPort)
    {
        return FALSE;
    }
    s_InputReq = (struct IOStdReq *)CreateIORequest(s_InputPort, sizeof(struct IOStdReq));
    if (!s_InputReq)
    {
        DeleteMsgPort(s_InputPort);
        s_InputPort = NULL;
        return FALSE;
    }
    if (OpenDevice("input.device", 0, (struct IORequest *)s_InputReq, 0))
    {
        DeleteIORequest((struct IORequest *)s_InputReq);
        DeleteMsgPort(s_InputPort);
        s_InputPort = NULL;
        s_InputReq = NULL;
        return FALSE;
    }
    
    // Get InputBase from the opened device for PeekQualifier inline pragma
    InputBase = s_InputReq->io_Device;

    // Create Timer for polling
    s_TimerPort = CreateMsgPort();
    if (!s_TimerPort)
    {
        return FALSE;
    }
    s_TimerReq = (struct timerequest *)CreateIORequest(s_TimerPort, sizeof(struct timerequest));
    if (!s_TimerReq)
    {
        DeleteMsgPort(s_TimerPort);
        s_TimerPort = NULL;
        return FALSE;
    }
    if (OpenDevice(TIMERNAME, UNIT_VBLANK, (struct IORequest *)s_TimerReq, 0))
    {
        DeleteIORequest((struct IORequest *)s_TimerReq);
        DeleteMsgPort(s_TimerPort);
        s_TimerPort = NULL;
        s_TimerReq = NULL;
        return FALSE;
    }

    // Initialize hardware state to avoid false initial events
    s_lastBTState = SAGA_MOUSE_BUTTONS & (SAGA_BUTTON4_MASK | SAGA_BUTTON5_MASK);
    s_lastWHCounter = SAGA_WHEELCOUNTER;
    s_lastWHDelta = 0;
    //s_lastWHDir = 0;
    
    // Ensure config byte and poll interval are set
    if (s_configByte == 0)
    {
        s_configByte = DEFAULT_CONFIG_BYTE;
    }
    
    // Initialize adaptive polling system
{
        UBYTE modeIndex = ((s_configByte & CONFIG_INTERVAL_MASK) >> CONFIG_INTERVAL_SHIFT) % 4;
                
        s_activeMode = &s_adaptiveModes[modeIndex];

        // Check if normal mode (bit 6)
        if (s_configByte & CONFIG_FIXED_MODE)
        {
            // Normal mode: use burstUs constantly (no state machine)
            s_adaptiveState = POLL_STATE_IDLE;  // State unused in normal mode
            s_adaptiveInterval = s_activeMode->burstUs;
            s_pollInterval = s_activeMode->burstUs;
        }
        else
        {
            // Adaptive mode: start in IDLE
            s_adaptiveState = POLL_STATE_IDLE;
            s_adaptiveInterval = s_activeMode->idleUs;
            s_pollInterval = s_activeMode->idleUs;
        }
        
        s_adaptiveInactive = 0;
    }

    return TRUE;
}

/**
 * Cleanup daemon resources
 */
static inline void daemon_Cleanup(void)
{

#ifndef RELEASE
    // Close debug console
    if (s_debugCon)
    {
        Close(s_debugCon);
        s_debugCon = 0;
    }
#endif

    // Cleanup timer: abort pending request, close device, delete resources
    if (s_TimerReq)
    {
        if (s_TimerReq->tr_node.io_Device)
        {
            if (CheckIO((struct IORequest *)s_TimerReq) == NULL)
            {
                AbortIO((struct IORequest *)s_TimerReq);
                WaitIO((struct IORequest *)s_TimerReq);
            }
            CloseDevice((struct IORequest *)s_TimerReq);
        }
        DeleteIORequest((struct IORequest *)s_TimerReq);
    }
    if (s_TimerPort)
    {
        DeleteMsgPort(s_TimerPort);
    }

    // cleanup input device
    if (s_InputReq)
    {
        if (s_InputReq->io_Device)
        {
            CloseDevice((struct IORequest *)s_InputReq);
        }
        DeleteIORequest((struct IORequest *)s_InputReq);
    }
    if (s_InputPort)
    {
        DeleteMsgPort(s_InputPort);
    }

    // cleanup public port
    if (s_PublicPort)
    {
        RemPort(s_PublicPort);
        DeleteMsgPort(s_PublicPort);
    }

    // cleanup DOS library
    if (DOSBase)
    {
        CloseLibrary((struct Library *)DOSBase);
    }
}

/**
 * Get mode name from config byte.
 * @param configByte Configuration byte
 * @return Mode name string (adaptive or normal variant)
 */
static inline const char* getModeName(UBYTE configByte)
{
    UBYTE modeIndex = ((configByte & CONFIG_INTERVAL_MASK) >> CONFIG_INTERVAL_SHIFT) % 4;
    const AdaptiveMode *mode = &s_adaptiveModes[modeIndex];
    
    return (configByte & CONFIG_FIXED_MODE) ? mode->normalName : mode->adaptiveName;
}

/**
 * Parse hex digit to value (0-15).
 * @param c Character to parse ('0'-'9', 'A'-'F', 'a'-'f')
 * @return Value 0-15, or -1 if invalid
 */
static inline int parseHexDigit(UBYTE c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}
