// xroach for SymbOS — cockroaches that scatter under open windows
// Based on the classic Unix xroach by J.T. Anderson (1991)
// SymbOS C port by Salvatore Bognanni

#include <symbos.h>
#include <graphics.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define NUM_ROACHES     6
#define ROACH_W         16
#define ROACH_H         16
#define SCREEN_W        320
#define SCREEN_H        200
#define SPEED           3       // pixels per frame (normal)
#define SCATTER_SPEED   6       // pixels per frame (scattering)
#define SCATTER_TICKS   25      // frames of scatter movement
#define FRAME_SKIP      3       // system ticks between animation frames

// ---------------------------------------------------------------------------
// Direction table: 8 compass dirs, 0=E 1=SE 2=S 3=SW 4=W 5=NW 6=N 7=NE
// ---------------------------------------------------------------------------
static const signed char dir_dx[8] = { 3,  2,  0, -2, -3, -2,  0,  2 };
static const signed char dir_dy[8] = { 0,  2,  3,  2,  0, -2, -3, -2 };

// Map 8 movement directions to 4 sprite indices: 0=E 1=S 2=W 3=N
static const unsigned char dir_spr[8] = { 0, 0, 1, 2, 2, 2, 3, 0 };

// ---------------------------------------------------------------------------
// Sprite data: 4bpp, 16x16 pixels
// Format: bytew(=8), w(=16), h(=16), then 128 bytes of pixel data
// Nibble = one pixel: 0xD = grey (background), 0x1 = black (roach)
// Each byte = two pixels: high nibble = left pixel, low nibble = right pixel
// ---------------------------------------------------------------------------

// E sprite: roach facing right, head on right, cerci on left
static const char spr_E[3 + 128] = {
    0x08, 0x10, 0x10,
    // row 0
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    // row 1: antenna tip at col 14
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x1D,
    // row 2: antenna at col 13
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xD1,0xDD,
    // row 3: antenna at col 12
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x1D,0xDD,
    // row 4: leg at col 1, body cols 3-12
    0xD1,0xD1,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 5: body cols 3-13 (head wider)
    0xDD,0xD1,0x11,0x11,0x11,0x11,0x11,0xDD,
    // row 6: leg at col 1, body cols 3-12
    0xD1,0xD1,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 7: body cols 3-11
    0xDD,0xD1,0x11,0x11,0x11,0x1D,0xDD,0xDD,
    // row 8: leg at col 1, body cols 3-10
    0xD1,0xD1,0x11,0x11,0x11,0x1D,0xDD,0xDD,
    // row 9: body cols 3-9
    0xDD,0xD1,0x11,0x11,0x11,0xDD,0xDD,0xDD,
    // row 10: tail cols 4-8
    0xDD,0xDD,0x11,0x11,0x1D,0xDD,0xDD,0xDD,
    // row 11: cerci at cols 0,2
    0x1D,0x1D,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    // rows 12-15
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
};

// W sprite: horizontal mirror of E (head on left, cerci on right)
static const char spr_W[3 + 128] = {
    0x08, 0x10, 0x10,
    // row 0
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    // row 1: antenna tip at col 1
    0xD1,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    // row 2: antenna at col 2
    0xDD,0x1D,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    // row 3: antenna at col 3
    0xDD,0xD1,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    // row 4: body cols 3-12, leg at col 14
    0xDD,0xD1,0x11,0x11,0x11,0x11,0x1D,0x1D,
    // row 5: head wider cols 2-12
    0xDD,0x11,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 6: body cols 3-12, leg at col 14
    0xDD,0xD1,0x11,0x11,0x11,0x11,0x1D,0x1D,
    // row 7: body cols 4-12
    0xDD,0xDD,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 8: body cols 5-12, leg at col 14
    0xDD,0xDD,0xD1,0x11,0x11,0x11,0x1D,0x1D,
    // row 9: body cols 6-12
    0xDD,0xDD,0xDD,0xD1,0x11,0x11,0x11,0xDD,
    // row 10: tail cols 7-11
    0xDD,0xDD,0xDD,0xD1,0x11,0x11,0xDD,0xDD,
    // row 11: cerci at cols 13,15
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xD1,0xD1,
    // rows 12-15
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
};

// N sprite: roach facing up, head at top, legs left/right, cerci at bottom
static const char spr_N[3 + 128] = {
    0x08, 0x10, 0x10,
    // row 0: antennae tips at cols 4,11
    0xDD,0xDD,0x1D,0xDD,0xDD,0xD1,0xDD,0xDD,
    // row 1: antennae at cols 5,10
    0xDD,0xDD,0xD1,0xDD,0xDD,0x1D,0xDD,0xDD,
    // row 2: head antennae base cols 6-9
    0xDD,0xDD,0xDD,0x11,0x11,0xDD,0xDD,0xDD,
    // row 3: head cols 5-10
    0xDD,0xDD,0xD1,0x11,0x11,0x1D,0xDD,0xDD,
    // row 4: wider head cols 4-11
    0xDD,0xDD,0x11,0x11,0x11,0x11,0xDD,0xDD,
    // row 5: thorax cols 3-12
    0xDD,0xD1,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 6: legs at cols 1,14 + body cols 3-12
    0xD1,0xD1,0x11,0x11,0x11,0x11,0x1D,0x1D,
    // row 7: body cols 3-12
    0xDD,0xD1,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 8: legs at cols 1,14 + body
    0xD1,0xD1,0x11,0x11,0x11,0x11,0x1D,0x1D,
    // row 9: body
    0xDD,0xD1,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 10: legs
    0xD1,0xD1,0x11,0x11,0x11,0x11,0x1D,0x1D,
    // row 11: abdomen cols 4-11
    0xDD,0xDD,0x11,0x11,0x11,0x11,0xDD,0xDD,
    // row 12: tail cols 5-10
    0xDD,0xDD,0xD1,0x11,0x11,0x1D,0xDD,0xDD,
    // row 13: cerci at cols 6,9
    0xDD,0xDD,0xDD,0x1D,0xD1,0xDD,0xDD,0xDD,
    // rows 14-15
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
};

// S sprite: roach facing down = vertical mirror of N
static const char spr_S[3 + 128] = {
    0x08, 0x10, 0x10,
    // rows 0-1: empty
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,
    // row 2: cerci at cols 6,9 (N row 13 reversed)
    0xDD,0xDD,0xDD,0x1D,0xD1,0xDD,0xDD,0xDD,
    // row 3: tail cols 5-10 (N row 12)
    0xDD,0xDD,0xD1,0x11,0x11,0x1D,0xDD,0xDD,
    // row 4: abdomen cols 4-11 (N row 11)
    0xDD,0xDD,0x11,0x11,0x11,0x11,0xDD,0xDD,
    // row 5: legs (N row 10)
    0xD1,0xD1,0x11,0x11,0x11,0x11,0x1D,0x1D,
    // row 6: body (N row 9)
    0xDD,0xD1,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 7: legs (N row 8)
    0xD1,0xD1,0x11,0x11,0x11,0x11,0x1D,0x1D,
    // row 8: body (N row 7)
    0xDD,0xD1,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 9: legs (N row 6)
    0xD1,0xD1,0x11,0x11,0x11,0x11,0x1D,0x1D,
    // row 10: thorax (N row 5)
    0xDD,0xD1,0x11,0x11,0x11,0x11,0x1D,0xDD,
    // row 11: wider head (N row 4)
    0xDD,0xDD,0x11,0x11,0x11,0x11,0xDD,0xDD,
    // row 12: head (N row 3)
    0xDD,0xDD,0xD1,0x11,0x11,0x1D,0xDD,0xDD,
    // row 13: antennae base (N row 2)
    0xDD,0xDD,0xDD,0x11,0x11,0xDD,0xDD,0xDD,
    // row 14: antennae (N row 1)
    0xDD,0xDD,0xD1,0xDD,0xDD,0x1D,0xDD,0xDD,
    // row 15: antennae tips (N row 0)
    0xDD,0xDD,0x1D,0xDD,0xDD,0xD1,0xDD,0xDD,
};

// Sprite lookup by 4-sprite index
static const char * const sprites[4] = {
    spr_E, spr_S, spr_W, spr_N
};

// ---------------------------------------------------------------------------
// Roach state
// ---------------------------------------------------------------------------
typedef struct {
    int  x, y;             // current screen position
    int  ox, oy;           // previous position (for erase)
    unsigned char dir;     // direction 0-7
    unsigned char steps;   // steps until next random turn
    unsigned char scatter; // scatter frames remaining
} Roach;

Roach roaches[NUM_ROACHES];

// ---------------------------------------------------------------------------
// Canvas buffers: one 16x16 canvas per roach
// Size = (w * h / 2) + 24 bytes overhead (matches Eyes widget formula)
// ---------------------------------------------------------------------------
#define CANVAS_SIZE  ((ROACH_W * ROACH_H / 2) + 24)
_data char canvas[NUM_ROACHES][CANVAS_SIZE];

// ---------------------------------------------------------------------------
// SymbOS window/control structures (must be in transfer area)
// ---------------------------------------------------------------------------
// Controls: [0] background area fill, [1..NUM_ROACHES] roach image controls
_transfer Ctrl     ctrls[1 + NUM_ROACHES];
_transfer Ctrl_Group ctrlgrp;
_transfer Window   winrec;
_transfer char     empty_str[1];
_transfer char     timer_stack[512];
_transfer ProcHeader timer_hdr;

// ---------------------------------------------------------------------------
// Shared state between main and timer
// ---------------------------------------------------------------------------
volatile signed char  win_id    = -1;
volatile unsigned char do_scatter = 0;

// ---------------------------------------------------------------------------
// Timer process: animation loop
// ---------------------------------------------------------------------------
void timer_loop(void) {
    unsigned char i, spd, spr_idx;
    unsigned char tick = 0;
    Roach *r;
    int nx, ny;
    const char *spr;

    while (1) {
        // Only animate every FRAME_SKIP system ticks
        if (++tick < FRAME_SKIP) { Idle(); continue; }
        tick = 0;

        if (win_id < 0) { Idle(); continue; }

        // Pick up scatter trigger from main process
        if (do_scatter) {
            do_scatter = 0;
            for (i = 0; i < NUM_ROACHES; i++) {
                roaches[i].scatter = SCATTER_TICKS;
                roaches[i].dir     = (unsigned char)(rand() & 7);
                roaches[i].steps   = 0;
            }
        }

        for (i = 0; i < NUM_ROACHES; i++) {
            r = &roaches[i];

            spd = r->scatter ? SCATTER_SPEED : SPEED;
            if (r->scatter) r->scatter--;

            // Attempt move in current direction
            nx = r->x + dir_dx[r->dir] * spd;
            ny = r->y + dir_dy[r->dir] * spd;

            // Bounce at screen edges: reverse direction
            if (nx < 0 || nx + ROACH_W > SCREEN_W ||
                ny < 0 || ny + ROACH_H > SCREEN_H) {
                r->dir   = (r->dir + 4) & 7; // opposite
                r->steps = 3 + (unsigned char)(rand() & 7);
                nx = r->x;
                ny = r->y;
            }

            // Random turn
            if (r->steps == 0) {
                // Turn left or right by 1-3 directions
                int turn = (rand() % 7) - 3;  // -3 to +3
                r->dir   = (unsigned char)((r->dir + 8 + turn) & 7);
                r->steps = (unsigned char)(10 + (rand() & 31));
            } else {
                r->steps--;
            }

            // Update sprite canvas for new direction
            spr_idx = dir_spr[r->dir];
            spr = sprites[spr_idx];
            Gfx_Select(canvas[i]);
            Gfx_Clear(canvas[i], 0xD);
            Gfx_Put((char *)spr, 0, 0, PUT_SET);

            // Move control to new position BEFORE erasing old area.
            // When the desktop redraws old area, the control is at the new
            // position so only the background fills the old spot.
            r->x = nx;
            r->y = ny;
            ctrls[1 + i].x = (unsigned short)nx;
            ctrls[1 + i].y = (unsigned short)ny;

            // Erase old position (control is now at new pos, so bg fills here)
            if (r->ox >= 0) {
                Win_Redraw_Area((unsigned char)win_id, 255, 0,
                                (unsigned short)r->ox, (unsigned short)r->oy,
                                ROACH_W, ROACH_H);
            }

            // Draw roach at new position
            Win_Redraw_Area((unsigned char)win_id, 255, 0,
                            (unsigned short)nx, (unsigned short)ny,
                            ROACH_W, ROACH_H);

            r->ox = nx;
            r->oy = ny;
        }

        Idle();
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    unsigned short resp;
    unsigned char sender_pid;
    unsigned char i;
    signed char wid;

    // Seed RNG from system counter
    srand((unsigned int)Sys_Counter16());

    // Initialize roach canvases
    for (i = 0; i < NUM_ROACHES; i++) {
        Gfx_Init(canvas[i], ROACH_W, ROACH_H);
        Gfx_Select(canvas[i]);
        Gfx_Clear(canvas[i], 0xD);
        // Draw initial E sprite into each canvas
        Gfx_Put((char *)spr_E, 0, 0, PUT_SET);
    }

    // Scatter initial positions so roaches don't all start in same spot
    for (i = 0; i < NUM_ROACHES; i++) {
        roaches[i].x      = 20 + (int)(rand() % (SCREEN_W - ROACH_W  - 40));
        roaches[i].y      = 20 + (int)(rand() % (SCREEN_H - ROACH_H  - 40));
        roaches[i].ox     = -1;
        roaches[i].oy     = -1;
        roaches[i].dir    = (unsigned char)(rand() & 7);
        roaches[i].steps  = (unsigned char)(rand() & 31);
        roaches[i].scatter = 0;
    }

    // Build control array ---------------------------------------------------

    empty_str[0] = 0;

    // Control [0]: full-window background area, grey
    // C_AREA param: bit7=1 → 16-colour mode, bits 0-3 = colour index 13 (grey)
    ctrls[0].value = 0;
    ctrls[0].type  = C_AREA;
    ctrls[0].bank  = -1;
    ctrls[0].param = (unsigned short)(AREA_16COLOR | COLOR_GRAY);
    ctrls[0].x     = 0;
    ctrls[0].y     = 0;
    ctrls[0].w     = SCREEN_W;
    ctrls[0].h     = SCREEN_H;
    ctrls[0].unused = 0;

    // Controls [1..NUM_ROACHES]: roach image canvases
    for (i = 0; i < NUM_ROACHES; i++) {
        ctrls[1 + i].value  = (unsigned short)(1 + i);
        ctrls[1 + i].type   = C_IMAGE_EXT;
        ctrls[1 + i].bank   = -1;
        ctrls[1 + i].param  = (unsigned short)canvas[i];
        ctrls[1 + i].x      = (unsigned short)roaches[i].x;
        ctrls[1 + i].y      = (unsigned short)roaches[i].y;
        ctrls[1 + i].w      = ROACH_W;
        ctrls[1 + i].h      = ROACH_H;
        ctrls[1 + i].unused = 0;
    }

    // Control group --------------------------------------------------------
    memset(&ctrlgrp, 0, sizeof(ctrlgrp));
    ctrlgrp.controls = 1 + NUM_ROACHES;
    ctrlgrp.pid      = _sympid;
    ctrlgrp.first    = &ctrls[0];

    // Window record --------------------------------------------------------
    memset(&winrec, 0, sizeof(winrec));
    winrec.state    = WIN_NORMAL;          // open normally (not centered—place at 0,0)
    winrec.flags    = WIN_NOTTASKBAR | WIN_NOTMOVEABLE;
    winrec.pid      = _sympid;
    winrec.x        = 0;
    winrec.y        = 0;
    winrec.w        = SCREEN_W;
    winrec.h        = SCREEN_H;
    winrec.wfull    = SCREEN_W;
    winrec.hfull    = SCREEN_H;
    winrec.wmin     = 32;
    winrec.hmin     = 24;
    winrec.wmax     = SCREEN_W;
    winrec.hmax     = SCREEN_H;
    winrec.title    = empty_str;
    winrec.status   = empty_str;
    winrec.controls = &ctrlgrp;

    // Open window ----------------------------------------------------------
    wid = Win_Open(_symbank, &winrec);
    if (wid < 0) exit(1);
    win_id = wid;

    // Start animation timer ------------------------------------------------
    memset(&timer_hdr, 0, sizeof(timer_hdr));
    timer_hdr.startAddr = timer_loop;
    Timer_Add(_symbank, &timer_hdr);

    // Event loop -----------------------------------------------------------
    while (1) {
        resp = Msg_Sleep(_sympid, -1, _symmsg);
        if (!(resp & 1)) continue;

        sender_pid = (unsigned char)(resp >> 8);

        switch (_symmsg[0]) {
        case 0:
            // Close request from OS
            Win_Close((unsigned char)win_id);
            exit(0);
            break;

        case MSR_DSK_WCLICK:
            if (_symmsg[2] == DSK_ACT_CLOSE) {
                Win_Close((unsigned char)win_id);
                exit(0);
            }
            break;

        case MSR_DSK_WFOCUS:
            // Our window gained focus = another window was closed on top of us
            // Scatter the roaches!
            if (_symmsg[2] == 1) {
                do_scatter = 1;
            }
            break;
        }
    }
}
