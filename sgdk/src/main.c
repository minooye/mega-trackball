/*
 * Mega Drive Trackball - SGDK demonstration ROM
 *
 * - Bypasses SGDK classification (via JOY_SUPPORT_OFF)
 * - Manually reads the 9-nibble Mega Mouse protocol on port 2
 * - Sprite cursor controlled by the trackball, constrained to lower screen half
 * - Anti-glitch filter on corrupted reads
 *
 * Target: SGDK 1.80+
 */

#include <genesis.h>
#include "resources.h"

// ====================================================================
// Mega Drive port 2 I/O addresses
// For port 1: use 0xA10003 and 0xA10009
// ====================================================================

#define IO_DATA2 ((volatile u8*)0xA10005)
#define IO_CTRL2 ((volatile u8*)0xA1000B)

// ====================================================================
// Cursor bounds in pixels (lower screen area)
// ====================================================================

#define CURSOR_X_MIN  0
#define CURSOR_X_MAX  311    // 319 - 8 (sprite width)
#define CURSOR_Y_MIN  112    // line 14 * 8 (below text area)
#define CURSOR_Y_MAX  215    // 223 - 8 (sprite height)

// Anti-glitch: discard deltas that are too large (corrupted reads)
#define MAX_DELTA  30

// ====================================================================
// Globals
// ====================================================================

static u8 mouseBuffer[9];
static u32 successCount = 0;
static u32 failCount = 0;
static u32 glitchCount = 0;

// ====================================================================
// Manually read a mouse packet via direct I/O register access
// Returns TRUE if the packet is valid (signature 0xB FF read correctly)
// ====================================================================

static bool readMousePacket(void)
{
    volatile u8 *data = IO_DATA2;
    volatile u8 *ctrl = IO_CTRL2;
    bool result = FALSE;
    
    // Critical section: interrupts disabled
    SYS_disableInts();
    
    // Configure TH (bit 6) and TR (bit 5) as outputs; others as inputs
    *ctrl = 0x60;
    
    // Idle state: TH=1, TR=1
    *data = 0x60;
    
    // Delay to let the Arduino settle
    for (volatile u16 j = 0; j < 200; j++);
    
    // Sequence of 9 commands (alternates $20 / $00)
    // Note: each nibble is read after a fixed delay rather than waiting for TL.
    // More tolerant to timing variations of the Arduino emulation.
    static const u8 cmds[9] = {
        0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20
    };
    
    for (u8 i = 0; i < 9; i++) {
        *data = cmds[i];
        for (volatile u16 j = 0; j < 200; j++);
        mouseBuffer[i] = *data & 0x0F;
    }
    
    // Validation: expected signature = 0xB 0xF 0xF
    if (mouseBuffer[0] == 0xB &&
        mouseBuffer[1] == 0xF &&
        mouseBuffer[2] == 0xF) {
        result = TRUE;
    }
    
    // Back to idle
    *data = 0x60;
    SYS_enableInts();
    return result;
}

// ====================================================================
// main
// ====================================================================

int main(bool hard)
{
    char str[40];
    s32 totalX = 0, totalY = 0;
    
    // Initial cursor position (center of lower area)
    s16 cursorPx = 160;
    s16 cursorPy = 168;
    
    Sprite *cursorSprite;
    
    JOY_init();
    // Disable SGDK polling for port 2: we do manual reads
    JOY_setSupport(PORT_2, JOY_SUPPORT_OFF);
    
    // Initialize sprite engine
    SPR_init();
    
    // Load cursor palette into PAL1
    PAL_setPalette(PAL1, cursor_image.palette->data, DMA);
    
    // Create cursor sprite
    cursorSprite = SPR_addSprite(&cursor_image, cursorPx, cursorPy,
                                  TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    
    // === Static display (upper half) ===
    VDP_drawText("=== Mega Trackball ===", 8, 1);
    VDP_drawText("Status:", 0, 3);
    VDP_drawText("Stats: ", 0, 4);
    VDP_drawText("Delta: ", 0, 6);
    VDP_drawText("Total: ", 0, 7);
    VDP_drawText("Btns:  ", 0, 9);
    VDP_drawText("Signs: ", 0, 10);
    VDP_drawText("Cursor:", 0, 12);
    VDP_drawText("----------------------------------------", 0, 13);
    
    // === Main loop ===
    while (TRUE)
    {
        bool ok = readMousePacket();
        
        if (ok) successCount++;
        else    failCount++;
        
        // Status
        VDP_clearText(8, 3, 24);
        VDP_drawText(ok ? "MOUSE OK" : "FAIL", 8, 3);
        
        // Stats
        VDP_clearText(8, 4, 24);
        sprintf(str, "OK=%lu FAIL=%lu", successCount, failCount);
        VDP_drawText(str, 8, 4);
        
        s16 dX = 0, dY = 0;
        
        if (ok) {
            u8 signs   = mouseBuffer[3];
            u8 buttons = mouseBuffer[4];
            u16 absX   = (mouseBuffer[5] << 4) | mouseBuffer[6];
            u16 absY   = (mouseBuffer[7] << 4) | mouseBuffer[8];
            
            dX = (signs & 0x01) ? -(s16)absX : (s16)absX;
            dY = (signs & 0x02) ? -(s16)absY : (s16)absY;
            
            // Anti-glitch: discard deltas that are too large
            if (dX > MAX_DELTA || dX < -MAX_DELTA ||
                dY > MAX_DELTA || dY < -MAX_DELTA) {
                glitchCount++;
                dX = 0;
                dY = 0;
            } else {
                totalX += dX;
                totalY += dY;
            }
            
            // Current delta
            VDP_clearText(8, 6, 24);
            sprintf(str, "dX=%4d dY=%4d", dX, dY);
            VDP_drawText(str, 8, 6);
            
            // Total accumulated
            VDP_clearText(8, 7, 24);
            sprintf(str, "X=%6ld Y=%6ld", totalX, totalY);
            VDP_drawText(str, 8, 7);
            
            // Buttons
            VDP_clearText(8, 9, 24);
            char btnStr[16] = "";
            if (buttons & 0x01) strcat(btnStr, "L ");
            if (buttons & 0x02) strcat(btnStr, "R ");
            if (buttons & 0x04) strcat(btnStr, "M ");
            if (buttons & 0x08) strcat(btnStr, "S ");
            if (btnStr[0] == 0) strcpy(btnStr, "(none)");
            VDP_drawText(btnStr, 8, 9);
            
            // Signs + glitch counter
            VDP_clearText(8, 10, 24);
            sprintf(str, "0x%01X glitch=%lu", signs, glitchCount);
            VDP_drawText(str, 8, 10);
        }
        
        // Update cursor position
        cursorPx += dX;
        cursorPy -= dY;   // Y inverted
        
        // Bounds (lower zone only)
        if (cursorPx < CURSOR_X_MIN) cursorPx = CURSOR_X_MIN;
        if (cursorPx > CURSOR_X_MAX) cursorPx = CURSOR_X_MAX;
        if (cursorPy < CURSOR_Y_MIN) cursorPy = CURSOR_Y_MIN;
        if (cursorPy > CURSOR_Y_MAX) cursorPy = CURSOR_Y_MAX;
        
        SPR_setPosition(cursorSprite, cursorPx, cursorPy);
        
        // Cursor position display
        VDP_clearText(8, 12, 24);
        sprintf(str, "(%3d,%3d)", cursorPx, cursorPy);
        VDP_drawText(str, 8, 12);
        
        SPR_update();
        SYS_doVBlankProcess();
    }
    
    return 0;
}
