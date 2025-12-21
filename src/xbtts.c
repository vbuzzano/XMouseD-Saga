/*
 * XBttS - Simple test tool for buttons 4/5 emulation
 * Maps Ctrl→Button4 and Shift→Button5 via shared memory
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/inputevent.h>
#include <devices/input.h>
#include <clib/input_protos.h>
#include <stdio.h>

struct Library *InputBase;

#define XBTTS_SHARED_ADDR   0x1FFFFFFC
#define FAKE_BUTTON4_MASK   0x0100
#define FAKE_BUTTON5_MASK   0x0200

#define IEQUALIFIER_CONTROL 0x0008
#define IEQUALIFIER_LSHIFT  0x0001
#define IEQUALIFIER_RSHIFT  0x0002

int main(void)
{
    struct MsgPort *inputPort;
    struct IOStdReq *inputReq;
    volatile UWORD *fakeButtons = (volatile UWORD*)XBTTS_SHARED_ADDR;
    UWORD lastQual = 0;
    
    printf("XBttS - Ctrl=Button4, Shift=Button5\n");
    
    // Open input.device for PeekQualifier()
    inputPort = CreateMsgPort();
    if (!inputPort)
    {
        printf("ERROR: Failed to create port\n");
        return 1;
    }
    
    inputReq = (struct IOStdReq *)CreateIORequest(inputPort, sizeof(struct IOStdReq));
    if (!inputReq)
    {
        printf("ERROR: Failed to create IO request\n");
        DeleteMsgPort(inputPort);
        return 1;
    }
    
    if (OpenDevice("input.device", 0, (struct IORequest *)inputReq, 0) != 0)
    {
        printf("ERROR: Failed to open input.device\n");
        DeleteIORequest((struct IORequest *)inputReq);
        DeleteMsgPort(inputPort);
        return 1;
    }
    
    InputBase = (struct Library *)inputReq->io_Device;
    
    printf("Shared memory at 0x%08x\n", (unsigned int)XBTTS_SHARED_ADDR);
    printf("Press Ctrl+C to exit.\n\n");
    
    *fakeButtons = 0;
    
    while (!(SetSignal(0, 0) & SIGBREAKF_CTRL_C))
    {
        UWORD qual = PeekQualifier();
        
        if (qual != lastQual)
        {
            UWORD buttons = 0;
            
            // Ctrl pressed → Button 4
            if (qual & IEQUALIFIER_CONTROL)
                buttons |= FAKE_BUTTON4_MASK;
            
            // Shift pressed → Button 5
            if (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
                buttons |= FAKE_BUTTON5_MASK;
            
            *fakeButtons = buttons;
            lastQual = qual;
        }
        
        Delay(1);  // 20ms polling
    }
    
    *fakeButtons = 0;
    
    CloseDevice((struct IORequest *)inputReq);
    DeleteIORequest((struct IORequest *)inputReq);
    DeleteMsgPort(inputPort);
    
    printf("\nDone.\n");
    return 0;
}
