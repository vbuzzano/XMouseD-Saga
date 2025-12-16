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

#define APP_NAME            "XMouseD"
#define APP_VERSION         "1.0-beta1"
#define APP_DATE            "10.12.2025"
#define APP_AUTHOR          "Vincent Buzzano (aka ReddoC)"
#define APP_EMAIL           "reddoc007@gmail.com"
#define APP_DESCRIPTION     "SAGA Daemon Driver"


//===========================================================================
// LOG String Constants                                                      
//===========================================================================

// TODO: Transform each log string to constants vvvv HERRE vvvv



//===========================================================================
// Newmouse button codes for extra buttons 4 & 5                             
// not defined in standard newmouse.h                                        
//===========================================================================

// Button 5
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

#define XMOUSE_PORT_NAME        "XMouseD_Port"

// Message commands for daemon control
#define XMSG_CMD_QUIT           0   // Stop daemon
#define XMSG_CMD_SET_CONFIG     1   // Set config byte
#define XMSG_CMD_SET_INTERVAL   2   // Set poll interval (microseconds)
#define XMSG_CMD_GET_STATUS     3   // Get current status


//===========================================================================
// Daemon Configuration Definitions
//===========================================================================

#define START_MODE_TOGGLE 0
#define START_MODE_START 1
#define START_MODE_STOP 2

// Configuration byte bits
#define CONFIG_WHEEL_ENABLED    0x01    // Bit 0: Wheel enabled (RawKey + NewMouse) (0b00000001)
#define CONFIG_BUTTONS_ENABLED  0x02    // Bit 1: Extra buttons 4 & 5 enabled (0b00000010)
// Bits 2-3: Reserved
#define CONFIG_INTERVAL_SHIFT   4       // Bits 4-5: Profile selection (00=COMFORT, 01=BALANCED, 10=REACTIVE)
#define CONFIG_INTERVAL_MASK    0x30    // Profile mask (0b00110000)
#define CONFIG_FIXED_MODE       0x40    // Bit 6: Fixed mode (0=dynamic, 1=fixed on burstUs) (0b01000000)
// Bit 7: Reserved (bit 7 use for dev debug mode )

#define CONFIG_STOP (CONFIG_WHEEL_ENABLED | CONFIG_BUTTONS_ENABLED)

#define CONFIG_DEBUG_MODE       0x80    // Bit 7: Debug mode (0b10000000)  

#define DEFAULT_CONFIG_BYTE     0x13    // Default: Wheel ON, Buttons ON, BALANCED mode (01), Debug OFF (0b00010011)


//===========================================================================
// Variables
//===========================================================================

struct ExecBase *SysBase;              // Exec base (absolute 4)
struct DosLibrary *DOSBase;            // DOS library base
//void *InputBase;                       // Input library base (for PeekQualifier inline pragma)
struct Device * InputBase;
static struct MsgPort *s_PublicPort;   // Singleton port
static struct MsgPort *s_InputPort;    // Input device port
static struct IOStdReq *s_InputReq;    // Input IO request
static struct MsgPort *s_TimerPort;    // Timer port
static struct timerequest *s_TimerReq; // Timer IO request
static BYTE s_lastCounter;             // Last wheel position
static UWORD s_lastButtons;            // Last button state
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

// Profile names
#define PROFILE_NAME_COMFORT    "COMFORT"
#define PROFILE_NAME_BALANCED   "BALANCED"
#define PROFILE_NAME_REACTIVE   "REACTIVE"
#define PROFILE_NAME_ECO        "ECO"

// Fixed mode names (when bit 6 = 1)
#define MODE_NAME_MODERATE      "MODERATE"   // COMFORT fixed (20ms)
#define MODE_NAME_ACTIVE        "ACTIVE"     // BALANCED fixed (10ms)
#define MODE_NAME_INTENSIVE     "INTENSIVE"  // REACTIVE fixed (5ms)
#define MODE_NAME_PASSIVE       "PASSIVE"    // ECO fixed (40ms)
// Adaptive mode configuration
typedef struct
{
    const char *dynamicName;  // Dynamic mode name (COMFORT, BALANCED, REACTIVE)
    const char *fixedName;    // Fixed mode name (MODERATE, ACTIVE, INTENSIVE)
    ULONG idleUs;             // Target interval when idle (microseconds)
    ULONG activeUs;           // Target interval when active (microseconds)
    ULONG burstUs;            // Target interval during burst usage (microseconds)
    ULONG stepDecUs;          // Microseconds to decrement per tick (ACTIVE → BURST)
    ULONG stepIncUs;          // Microseconds to increment per tick (TO_IDLE → IDLE)
    ULONG activeThreshold;    // Microseconds of inactivity before transitioning from ACTIVE to TO_IDLE (human grace period)
    ULONG idleThreshold;      // Microseconds of inactivity before transitioning from BURST to TO_IDLE
} AdaptiveMode;

// Mode table indexed by config bits 4-5
// 4 base modes x 2 variants (dynamic/fixed via bit 6) = 8 total modes:
//   Dynamic (bit6=0): COMFORT, BALANCED, REACTIVE, ECO
//   Fixed (bit6=1):   MODERATE (20ms), ACTIVE (10ms), INTENSIVE (5ms), PASSIVE (40ms)
static const AdaptiveMode s_adaptiveModes[] = 
{
    // COMFORT (00): Relaxed, tolerant
    { PROFILE_NAME_COMFORT, MODE_NAME_MODERATE, 150000, 60000, 20000, 1100, 15000, 500000, 500000 },
    
    // BALANCED (01): Balanced, universal - DEFAULT
    { PROFILE_NAME_BALANCED, MODE_NAME_ACTIVE, 100000, 30000, 10000, 600, 1200, 500000, 1500000 },
    
    // REACTIVE (10): Nervous, snappy
    { PROFILE_NAME_REACTIVE, MODE_NAME_INTENSIVE, 50000, 15000, 5000, 500, 250, 500000, 3000000 },

    // ECO (11): Low-power/quiet
    // Dynamic: 200→80→40ms | Fixed: 40ms (PASSIVE)
    // stepDecUs=2000, stepIncUs=4000, thresholds: 500ms/1.5s
    { PROFILE_NAME_ECO, MODE_NAME_PASSIVE, 200000, 80000, 40000, 2000, 4000, 500000, 1500000 }
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
const char version[] = "$VER: " APP_NAME " " APP_VERSION " (" APP_DATE ") " APP_DESCRIPTION " (c) " APP_AUTHOR;


//===========================================================================
// Function Prototypes
//===========================================================================

static ULONG sendDaemonMessage(struct MsgPort *port, UBYTE cmd, ULONG value);
static inline int parseHexDigit(UBYTE c);
static inline BYTE parseArguments(void);
static inline const char* getModeName(UBYTE configByte);


static void daemon(void);
static BOOL daemon_Init(void);
static void daemon_Cleanup(void);
static inline void daemon_processWheel(void);
static inline void daemon_processButtons(void);
static inline ULONG getAdaptiveInterval(BOOL hadActivity);


//===========================================================================
// Macros
//===========================================================================

// Starts the timer with the specified timeout in milliseconds.
// millis Timeout in milliseconds.
#define TIMER_START(micros) \
        s_TimerReq->tr_node.io_Command = TR_ADDREQUEST;  \
        s_TimerReq->tr_time.tv_secs = micros / 1000000;  \
        s_TimerReq->tr_time.tv_micro = micros % 1000000; \
        SendIO((struct IORequest *)s_TimerReq);

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
    #define DebugLog(fmt) {}
    #define DebugLogF(fmt, ...) {}
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
    
    SysBase = *(struct ExecBase **)4L;
    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 36);
    if (!DOSBase)
    {
        return RETURN_FAIL;
    }
    // check if should start or stop the daemon
    BYTE startMode = parseArguments();

    // Check if XMouse is already running
    Forbid();
    existingPort = FindPort(XMOUSE_PORT_NAME);
    Permit();

    if (startMode == START_MODE_STOP && !existingPort)
    {
        // Not running, nothing to do
        Print("daemon is not running");
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_OK;
    }

    if (startMode == START_MODE_START && existingPort)
    {
        // Already running with config byte → update config instead of error
        if (s_configByte != DEFAULT_CONFIG_BYTE)
        {
            sendDaemonMessage(existingPort, XMSG_CMD_SET_CONFIG, s_configByte);
            if (s_configByte & CONFIG_DEBUG_MODE)
            {
                PrintF("config updated to 0x%02lx", (ULONG)s_configByte);
            }
        }
        else
        {
            Print("daemon already running");
        }
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_OK;
    }

    if ((startMode == START_MODE_STOP || startMode == START_MODE_TOGGLE) && existingPort)
    {
        // Send QUIT message to daemon
        sendDaemonMessage(existingPort, XMSG_CMD_QUIT, 0);
        CloseLibrary((struct Library *)DOSBase);
        Printf("stopping daemon...");
        // TODO: attendre réponse ?
        Print(" done.");
        
        return RETURN_OK;
    }

    // Start the daemon
    Printf("starting daemon...");

    // Create background process using WBM pattern
    if (CreateNewProcTags(
        NP_Entry, (ULONG)daemon,
        NP_Name, (ULONG)"XMouse - SAGA - Daemon",
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

        // TODO: could wait for confirmation of startup via message port ?
        Print(" done.");

        CloseLibrary((struct Library *)DOSBase);
        return RETURN_OK;
    }
    
    Print("failed to start daemon");
    CloseLibrary((struct Library *)DOSBase);
    return RETURN_FAIL;
}

/**
 * Send a message to the daemon and wait for reply.
 * @param port Daemon's public port
 * @param cmd Command to send
 * @param value Command parameter
 * @return Result from daemon, or 0xFFFFFFFF on error
 */
static ULONG sendDaemonMessage(struct MsgPort *port, UBYTE cmd, ULONG value)
{
    struct MsgPort *replyPort;
    struct XMouseMsg *msg;
    ULONG result;
    
    replyPort = CreateMsgPort();
    if (!replyPort)
    {
        return 0xFFFFFFFF;
    }
    
    msg = (struct XMouseMsg *)AllocMem(sizeof(struct XMouseMsg), MEMF_PUBLIC | MEMF_CLEAR);
    if (!msg)
    {
        DeleteMsgPort(replyPort);
        return 0xFFFFFFFF;
    }
    
    msg->msg.mn_Node.ln_Type = NT_MESSAGE;
    msg->msg.mn_Length = sizeof(struct XMouseMsg);
    msg->msg.mn_ReplyPort = replyPort;
    msg->command = cmd;
    msg->value = value;
    
    PutMsg(port, (struct Message *)msg);
    WaitPort(replyPort);
    GetMsg(replyPort);
    
    result = msg->result;
    
    FreeMem(msg, sizeof(struct XMouseMsg));
    DeleteMsgPort(replyPort);
    
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
        return START_MODE_START;
    }
    
    // Test hex format: 0xBYTE
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        hi = parseHexDigit(p[2]);
        lo = parseHexDigit(p[3]);
        
        if (hi >= 0 && lo >= 0)
        {
            configByte = (UBYTE)((hi << 4) | lo);
            
            // Check STOP conditions: neither wheel nor buttons enabled (bits 0-1)
            if ((configByte & CONFIG_STOP) == 0)
            {
                //PrintF("config 0x%02lx = STOP (wheel and buttons disabled)", (ULONG)configByte);
                return START_MODE_STOP;
            }
            
            // Store config byte for daemon to use
            s_configByte = configByte;
            
            // Display config info
            {
#ifndef RELEASE
                PrintF("config: 0x%02lx", (ULONG)configByte);
                PrintF("wheel: %s", (configByte & CONFIG_WHEEL_ENABLED) ? "ON" : "OFF");
                PrintF("extra buttons: %s", (configByte & CONFIG_BUTTONS_ENABLED) ? "ON" : "OFF");
#endif
                if (configByte & CONFIG_DEBUG_MODE)
                {
                    PrintF("mode: %s", getModeName(configByte));
                }
            }

            return START_MODE_START;
        }
    }
    
    // Unknown argument
    PrintF("unknown argument: %s", args);
    return START_MODE_TOGGLE;
}


//===========================================================================
// Daemon thread functions
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
            s_debugCon = Open("CON:0/0/640/200/XMouse Debug/AUTO/CLOSE/WAIT", MODE_NEWFILE);
            
            DebugLog("daemon started");
            DebugLogF("Mode: %s", getModeName(s_configByte));
            
            if (s_configByte & CONFIG_FIXED_MODE)
            {
                DebugLogF("Poll: %ldms (fixed)", (LONG)(s_pollInterval / 1000));
            }
            else
            {
                DebugLogF("Poll: %ld->%ld->%ldms (dynamic)", 
                          (LONG)(s_activeMode->idleUs / 1000),
                          (LONG)(s_activeMode->activeUs / 1000),
                          (LONG)(s_activeMode->burstUs / 1000));
            }
            
            DebugLog("---");
        }
#endif        
        TIMER_START(s_pollInterval);
        
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
                                
                                s_configByte = newConfig;
                                msg->result = s_configByte;
                                
                                // If mode changed, reinitialize adaptive system
                                if (oldInterval != newInterval || 
                                    ((oldConfig ^ newConfig) & CONFIG_FIXED_MODE))
                                {
                                    UBYTE modeIndex = (newConfig & CONFIG_INTERVAL_MASK) >> CONFIG_INTERVAL_SHIFT;
                                                                        
                                    s_activeMode = &s_adaptiveModes[modeIndex % 4];
                                    
                                    // Reinitialize based on new mode
                                    if (newConfig & CONFIG_FIXED_MODE)
                                    {
                                        // Fixed mode: use burstUs
                                        s_adaptiveInterval = s_activeMode->burstUs;
                                        s_pollInterval = s_activeMode->burstUs;
                                    }
                                    else
                                    {
                                        // Dynamic mode: start from idle
                                        s_adaptiveState = POLL_STATE_IDLE;
                                        s_adaptiveInterval = s_activeMode->idleUs;
                                        s_pollInterval = s_activeMode->idleUs;
                                    }
                                    
                                    s_adaptiveInactive = 0;
                                    
                                    // Restart timer with new interval
                                    AbortIO((struct IORequest *)s_TimerReq);
                                    WaitIO((struct IORequest *)s_TimerReq);
                                    TIMER_START(s_pollInterval);
                                }
                                
#ifndef RELEASE
                                // Handle debug mode change
                                if ((oldConfig & CONFIG_DEBUG_MODE) && !(newConfig & CONFIG_DEBUG_MODE))
                                {
                                    // Debug mode disabled - close console
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
                                        s_debugCon = Open("CON:0/0/640/200/XMouse Debug/AUTO/CLOSE/WAIT", MODE_NEWFILE);
                                        DebugLog("Debug mode enabled");
                                    }
                                }
#endif
                            }
                            break;
                            
                        case XMSG_CMD_SET_INTERVAL:
                            s_pollInterval = msg->value;
                            msg->result = s_pollInterval;
                            // Restart timer with new interval
                            AbortIO((struct IORequest *)s_TimerReq);
                            WaitIO((struct IORequest *)s_TimerReq);
                            TIMER_START(s_pollInterval);
                            break;
                            
                        case XMSG_CMD_GET_STATUS:
                            msg->result = (s_configByte << 16) | (s_pollInterval / 1000);
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
                BOOL hadActivity = FALSE;
                
                // Initialize event buffer (reused by both wheel and button processing)
                s_eventBuf.ie_NextEvent = NULL;
                s_eventBuf.ie_SubClass = 0;
                s_eventBuf.ie_Qualifier = PeekQualifier();  // Capture current qualifier state
                s_eventBuf.ie_X = 0;
                s_eventBuf.ie_Y = 0;
                s_eventBuf.ie_TimeStamp.tv_secs = 0;
                s_eventBuf.ie_TimeStamp.tv_micro = 0;
                
                // Check for wheel activity
                if (s_configByte & CONFIG_WHEEL_ENABLED)
                {
                    BYTE current = SAGA_WHEELCOUNTER;
                    if (current != s_lastCounter)
                    {
                        hadActivity = TRUE;
                        daemon_processWheel();
                    }
                }

                // Check for button activity
                if (s_configByte & CONFIG_BUTTONS_ENABLED)
                {
                    UWORD current = SAGA_MOUSE_BUTTONS & (SAGA_BUTTON4_MASK | SAGA_BUTTON5_MASK);
                    if (current != s_lastButtons)
                    {
                        hadActivity = TRUE;
                        daemon_processButtons();
                    }
                }

                // Update adaptive interval and restart timer
                // Fixed mode: no adaptation, just restart timer with burstUs
                if (s_configByte & CONFIG_FIXED_MODE)
                {
                    // Fixed mode: always use burstUs, no state machine
                    TIMER_START(s_pollInterval);
                }
                else
                {
                    // Dynamic mode: update adaptive interval
                    s_pollInterval = getAdaptiveInterval(hadActivity);
                    
                    // Always restart timer with updated interval
                    AbortIO((struct IORequest *)s_TimerReq);
                    WaitIO((struct IORequest *)s_TimerReq);
                    TIMER_START(s_pollInterval);
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
            }
        }
    }

    daemon_Cleanup();
}

/**
 * Inject input event to input.device.
 * Reuses caller's InputEvent struct to avoid repeated allocations.
 * @param ev Pre-filled InputEvent struct
 */
static inline void injectEvent(struct InputEvent *ev)
{
#ifndef RELEASE
    //DebugLogF("  injectEvent: class=0x%02lx code=0x%02lx qualifier=0x%04lx", 
    //          (ULONG)ev->ie_Class, (ULONG)ev->ie_Code, (ULONG)ev->ie_Qualifier);
#endif
    
    s_InputReq->io_Command = IND_WRITEEVENT;
    s_InputReq->io_Data = (APTR)ev;
    s_InputReq->io_Length = sizeof(struct InputEvent);
    
    DoIO((struct IORequest *)s_InputReq);
}

/**
 * Process wheel movement and inject events if needed.
 * Reuses s_eventBuf (only ie_Code and ie_Class are modified).
 */
static inline void daemon_processWheel(void)
{
    BYTE current;
    int delta, count, i;
    UWORD code;
    
    // Read current wheel counter
    current = SAGA_WHEELCOUNTER;
    if (current != s_lastCounter)
    {
        // Calculate delta with wrap-around handling
        delta = (int)(unsigned char)current - (int)(unsigned char)s_lastCounter;
        if (delta > 127)
        {
            delta -= 256;
        }
        else if (delta < -128) 
        { 
            delta += 256;
        }
    
        // Update last counter
        s_lastCounter = current;
    
        if (delta != 0)
        {
            // Determine direction and repeat count
            code = (delta > 0) ? NM_WHEEL_UP : NM_WHEEL_DOWN;
            count = (delta > 0) ? delta : -delta;  // abs(delta)

#ifndef RELEASE
            // Log wheel event
            //DebugLogF("Wheel: delta=%ld dir=%s count=%ld", 
            //         (LONG)delta, 
            //         (code == NM_WHEEL_UP) ? "UP" : "DOWN", 
            //         (LONG)count);
#endif
            
            // Reuse s_eventBuf (only ie_Code and ie_Class change)
            s_eventBuf.ie_Code = code;
            
            // Repeat events based on delta magnitude
            for (i = 0; i < count; i++)
            {
                // Always send both RawKey and NewMouse events
                s_eventBuf.ie_Class = IECLASS_RAWKEY;
                injectEvent(&s_eventBuf);
                
                s_eventBuf.ie_Class = IECLASS_NEWMOUSE;
                injectEvent(&s_eventBuf);
            }
        }
    }
}

/**
 * Process buttons and inject events if needed.
 * Reuses s_eventBuf (only ie_Code and ie_Class are modified).
 */
static inline void daemon_processButtons(void)
{
    UWORD current, changed;
    UWORD code;
    
    current = SAGA_MOUSE_BUTTONS & (SAGA_BUTTON4_MASK | SAGA_BUTTON5_MASK);
    changed = current ^ s_lastButtons;
    
    if (changed)
    {
        if (changed & SAGA_BUTTON4_MASK)
        {
            code = NM_BUTTON_FOURTH | ((current & SAGA_BUTTON4_MASK) ? 0 : IECODE_UP_PREFIX);
#ifndef RELEASE
            //DebugLogF("Button 4 %s", (current & SAGA_BUTTON4_MASK) ? "pressed" : "released");
#endif
            s_eventBuf.ie_Code = code;
            
            s_eventBuf.ie_Class = IECLASS_RAWKEY;
            injectEvent(&s_eventBuf);
            
            s_eventBuf.ie_Class = IECLASS_NEWMOUSE;
            injectEvent(&s_eventBuf);
        }
        
        if (changed & SAGA_BUTTON5_MASK)
        {
            code = NM_BUTTON_FIFTH | ((current & SAGA_BUTTON5_MASK) ? 0 : IECODE_UP_PREFIX);
#ifndef RELEASE
            //DebugLogF("Button 5 %s", (current & SAGA_BUTTON5_MASK) ? "pressed" : "released");
#endif
            s_eventBuf.ie_Code = code;
            
            s_eventBuf.ie_Class = IECLASS_RAWKEY;
            injectEvent(&s_eventBuf);
            
            s_eventBuf.ie_Class = IECLASS_NEWMOUSE;
            injectEvent(&s_eventBuf);
        }
        
        s_lastButtons = current;
    }
}

/**
 * Update adaptive polling interval based on activity.
 * State machine: IDLE → ACTIVE → BURST → TO_IDLE → IDLE
 * Only called in dynamic mode (bit 6 = 0). Fixed mode bypasses this function.
 * @param hadActivity TRUE if wheel/button activity detected this tick
 */
static inline ULONG getAdaptiveInterval(BOOL hadActivity)
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

                DebugLogF("[IDLE->ACTIVE] %ldus | InactiveUs=%ld", 
                          (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
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
                    DebugLogF("[ACTIVE->BURST] %ldus | InactiveUs=%ld", 
                              (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
                }
            }
            else
            {
                // Check accumulated inactivity in microseconds
                if (s_adaptiveInactive >= mode->activeThreshold)
                {
                    s_adaptiveState = POLL_STATE_TO_IDLE;
                    DebugLogF("[ACTIVE->TO_IDLE] %ldus | InactiveUs=%ld", 
                              (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
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
                    DebugLogF("[BURST->TO_IDLE] %ldus | InactiveUs=%ld", 
                              (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
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
                DebugLogF("[TO_IDLE->ACTIVE] %ldus | InactiveUs=%ld", 
                          (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
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
                    DebugLogF("[TO_IDLE->IDLE] %ldus | InactiveUs=%ld", 
                              (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
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
            DebugLogF("[%s->%s] %ldus | InactiveUs=%ld", 
                      stateNames[oldState], stateNames[s_adaptiveState], 
                      (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
        }
        // Interval changed but same state (progressive descent/ascent)
        else if (s_adaptiveInterval != oldUs)
        {
            DebugLogF("[%s] %ldus | InactiveUs=%ld", 
                      stateNames[s_adaptiveState], (LONG)s_adaptiveInterval, (LONG)s_adaptiveInactive);
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
    s_PublicPort->mp_Node.ln_Name = XMOUSE_PORT_NAME;
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
    s_lastCounter = SAGA_WHEELCOUNTER;
    s_lastButtons = SAGA_MOUSE_BUTTONS & (SAGA_BUTTON4_MASK | SAGA_BUTTON5_MASK);
    
    // Ensure config byte and poll interval are set
    if (s_configByte == 0)
    {
        s_configByte = DEFAULT_CONFIG_BYTE;
    }
    
    // Initialize adaptive polling system
{
        UBYTE modeIndex = ((s_configByte & CONFIG_INTERVAL_MASK) >> CONFIG_INTERVAL_SHIFT) % 4;
                
        s_activeMode = &s_adaptiveModes[modeIndex];

        // Check if fixed mode (bit 6)
        if (s_configByte & CONFIG_FIXED_MODE)
        {
            // Fixed mode: use burstUs constantly (no state machine)
            s_adaptiveState = POLL_STATE_IDLE;  // State unused in fixed mode
            s_adaptiveInterval = s_activeMode->burstUs;
            s_pollInterval = s_activeMode->burstUs;
        }
        else
        {
            // Dynamic adaptive mode: start in IDLE
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

    // cleanup timer
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
 * @return Mode name string (dynamic or fixed variant)
 */
static inline const char* getModeName(UBYTE configByte)
{
    UBYTE modeIndex = ((configByte & CONFIG_INTERVAL_MASK) >> CONFIG_INTERVAL_SHIFT) % 4;
    const AdaptiveMode *mode = &s_adaptiveModes[modeIndex];
    
    return (configByte & CONFIG_FIXED_MODE) ? mode->fixedName : mode->dynamicName;
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
