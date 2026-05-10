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
#define SPEED           3
#define SCATTER_SPEED   6
#define SCATTER_TICKS   25
#define FRAME_SKIP      4       // ticks between frames (~12 FPS at 50 Hz)

// ---------------------------------------------------------------------------
// Direction table: 8 compass dirs, 0=E 1=SE 2=S 3=SW 4=W 5=NW 6=N 7=NE
// ---------------------------------------------------------------------------
static const signed char dir_dx[8] = { 3,  2,  0, -2, -3, -2,  0,  2 };
static const signed char dir_dy[8] = { 0,  2,  3,  2,  0, -2, -3, -2 };

// Map 8 movement dirs to 4 sprite indices: 0=E 1=S 2=W 3=N
static const unsigned char dir_spr[8] = { 0, 0, 1, 2, 2, 2, 3, 0 };

// ---------------------------------------------------------------------------
// Sprite data: 4bpp 16x16, derived from original xroach XBM bitmaps (48x48)
// scaled to 16x16 via max-pooling. Nibble 0x1=black, 0x8=white(background).
// sprites[0]=E(0deg) sprites[1]=S(270deg) sprites[2]=W(180deg) sprites[3]=N(90deg)
// ---------------------------------------------------------------------------

// E — roach facing east/right (from roach000.xbm scaled to 16x16)
static const char spr_E[3 + 128] = {
    0x08, 0x10, 0x10,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x18,0x88,0x88,0x11,0x88,0x81,0x88,
    0x88,0x11,0x11,0x81,0x11,0x11,0x18,0x88,
    0x88,0x88,0x11,0x11,0x11,0x18,0x88,0x88,
    0x81,0x11,0x11,0x11,0x11,0x11,0x11,0x18,
    0x81,0x11,0x11,0x11,0x11,0x11,0x18,0x88,
    0x81,0x11,0x11,0x11,0x11,0x11,0x11,0x18,
    0x88,0x11,0x11,0x11,0x11,0x11,0x88,0x88,
    0x88,0x81,0x11,0x11,0x11,0x11,0x18,0x88,
    0x88,0x11,0x88,0x88,0x18,0x88,0x18,0x88,
    0x88,0x88,0x88,0x88,0x81,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
};

// N — roach facing north/up (from roach090.xbm)
static const char spr_N[3 + 128] = {
    0x08, 0x10, 0x10,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x81,0x88,0x88,0x88,
    0x88,0x88,0x88,0x81,0x88,0x88,0x88,0x88,
    0x88,0x88,0x11,0x11,0x18,0x81,0x88,0x88,
    0x88,0x88,0x11,0x81,0x18,0x81,0x88,0x88,
    0x88,0x88,0x11,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x81,0x11,0x11,0x11,0x18,0x88,
    0x88,0x81,0x18,0x11,0x11,0x11,0x18,0x88,
    0x88,0x88,0x11,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x81,0x11,0x11,0x18,0x88,0x88,
    0x88,0x88,0x81,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x11,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x11,0x11,0x11,0x81,0x88,0x88,
    0x88,0x88,0x18,0x11,0x11,0x81,0x88,0x88,
    0x88,0x88,0x88,0x11,0x11,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
};

// W — roach facing west/left (from roach180.xbm)
static const char spr_W[3 + 128] = {
    0x08, 0x10, 0x10,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x81,0x88,0x88,0x81,0x88,
    0x88,0x81,0x11,0x11,0x18,0x81,0x11,0x88,
    0x88,0x88,0x88,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x11,0x11,0x11,0x11,0x11,0x18,
    0x88,0x88,0x11,0x11,0x11,0x11,0x11,0x18,
    0x88,0x81,0x11,0x11,0x11,0x11,0x11,0x18,
    0x88,0x18,0x11,0x11,0x11,0x11,0x11,0x18,
    0x88,0x88,0x11,0x11,0x11,0x11,0x11,0x88,
    0x88,0x81,0x11,0x81,0x18,0x88,0x81,0x88,
    0x88,0x88,0x88,0x81,0x88,0x88,0x88,0x18,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
};

// S — roach facing south/down (from roach270.xbm)
static const char spr_S[3 + 128] = {
    0x08, 0x10, 0x10,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x81,0x11,0x88,0x88,0x88,
    0x88,0x88,0x11,0x11,0x11,0x81,0x18,0x88,
    0x88,0x88,0x81,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x81,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x81,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x88,0x11,0x11,0x18,0x88,0x88,
    0x88,0x88,0x11,0x11,0x11,0x11,0x18,0x88,
    0x88,0x88,0x11,0x11,0x11,0x18,0x18,0x88,
    0x88,0x88,0x88,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x81,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x81,0x11,0x11,0x11,0x88,0x88,
    0x88,0x88,0x11,0x81,0x11,0x81,0x88,0x88,
    0x88,0x88,0x18,0x88,0x81,0x81,0x18,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x81,0x88,0x88,0x88,0x88,
};

static const char * const sprites[4] = { spr_E, spr_S, spr_W, spr_N };

// ---------------------------------------------------------------------------
// Roach state
// ---------------------------------------------------------------------------
typedef struct {
    int  x, y;             // current position
    int  ox, oy;           // previous position (for dirty-region tracking)
    unsigned char dir;
    unsigned char steps;
    unsigned char scatter;
} Roach;

Roach roaches[NUM_ROACHES];

// ---------------------------------------------------------------------------
// Canvas buffers (one 16x16 4bpp canvas per roach)
// ---------------------------------------------------------------------------
#define CANVAS_SIZE  ((ROACH_W * ROACH_H / 2) + 24)
_data char canvas[NUM_ROACHES][CANVAS_SIZE];

// ---------------------------------------------------------------------------
// SymbOS window/control structures
// ---------------------------------------------------------------------------
_transfer Ctrl      ctrls[1 + NUM_ROACHES];
_transfer Ctrl_Group ctrlgrp;
_transfer Window    winrec;
_transfer char      empty_str[1];
_transfer char      timer_stack[512];
_transfer ProcHeader timer_hdr;

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
volatile signed char   win_id     = -1;
volatile unsigned char do_scatter = 0;

// ---------------------------------------------------------------------------
// Timer process
// ONE Win_Redraw_Area call per frame, covering only the union of dirty areas.
// Full-window repaints (~32 KB) on a Z80 freeze the mouse; small dirty
// regions (~few hundred pixels) leave plenty of CPU for the desktop.
// ---------------------------------------------------------------------------
void timer_loop(void) {
    unsigned char i, spd, spr_idx;
    unsigned char tick = 0;
    Roach *r;
    int nx, ny;
    const char *spr;
    int turn;
    int bx0, by0, bx1, by1;

    while (1) {
        if (++tick < FRAME_SKIP) { Idle(); continue; }
        tick = 0;

        if (win_id < 0) { Idle(); continue; }

        if (do_scatter) {
            do_scatter = 0;
            for (i = 0; i < NUM_ROACHES; i++) {
                roaches[i].scatter = SCATTER_TICKS;
                roaches[i].dir     = (unsigned char)(rand() & 7);
                roaches[i].steps   = 0;
            }
        }

        // Start with inverted bbox
        bx0 = SCREEN_W; by0 = SCREEN_H; bx1 = 0; by1 = 0;

        for (i = 0; i < NUM_ROACHES; i++) {
            r = &roaches[i];

            spd = r->scatter ? SCATTER_SPEED : SPEED;
            if (r->scatter) r->scatter--;

            nx = r->x + (int)dir_dx[r->dir] * (int)spd;
            ny = r->y + (int)dir_dy[r->dir] * (int)spd;

            // Bounce at screen edges
            if (nx < 0 || nx + ROACH_W > SCREEN_W ||
                ny < 0 || ny + ROACH_H > SCREEN_H) {
                r->dir   = (r->dir + 4) & 7;
                r->steps = 3 + (unsigned char)(rand() & 7);
                nx = r->x;
                ny = r->y;
            }

            // Random turn
            if (r->steps == 0) {
                turn     = (rand() % 7) - 3;
                r->dir   = (unsigned char)((r->dir + 8 + turn) & 7);
                r->steps = (unsigned char)(10 + (rand() & 31));
            } else {
                r->steps--;
            }

            // Include old position in dirty bbox (for erase)
            if (r->ox < bx0) bx0 = r->ox;
            if (r->oy < by0) by0 = r->oy;
            if (r->ox + ROACH_W > bx1) bx1 = r->ox + ROACH_W;
            if (r->oy + ROACH_H > by1) by1 = r->oy + ROACH_H;

            // Update canvas with current direction sprite
            spr_idx = dir_spr[r->dir];
            spr     = sprites[spr_idx];
            Gfx_Select(canvas[i]);
            Gfx_Clear(canvas[i], COLOR_WHITE);
            Gfx_Put((char *)spr, 0, 0, PUT_SET);

            // Move control to new position
            r->x = nx;
            r->y = ny;
            ctrls[1 + i].x = (unsigned short)nx;
            ctrls[1 + i].y = (unsigned short)ny;

            // Include new position in dirty bbox (for draw)
            if (nx     < bx0) bx0 = nx;
            if (ny     < by0) by0 = ny;
            if (nx + ROACH_W > bx1) bx1 = nx + ROACH_W;
            if (ny + ROACH_H > by1) by1 = ny + ROACH_H;

            r->ox = nx;
            r->oy = ny;
        }

        // Clamp to screen bounds
        if (bx0 < 0)       bx0 = 0;
        if (by0 < 0)       by0 = 0;
        if (bx1 > SCREEN_W) bx1 = SCREEN_W;
        if (by1 > SCREEN_H) by1 = SCREEN_H;

        // ONE repaint covering only dirty area — C_AREA bg erases old positions,
        // C_IMAGE_EXT controls draw roaches at new positions.
        if (bx1 > bx0 && by1 > by0) {
            Win_Redraw_Area((unsigned char)win_id, 255, 0,
                            (unsigned short)bx0, (unsigned short)by0,
                            (unsigned short)(bx1 - bx0),
                            (unsigned short)(by1 - by0));
        }

        Idle();
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    unsigned short resp;
    unsigned char i;
    signed char wid;

    srand((unsigned int)Sys_Counter16());

    // Init canvases
    for (i = 0; i < NUM_ROACHES; i++) {
        Gfx_Init(canvas[i], ROACH_W, ROACH_H);
        Gfx_Select(canvas[i]);
        Gfx_Clear(canvas[i], COLOR_WHITE);
        Gfx_Put((char *)spr_E, 0, 0, PUT_SET);
    }

    // Initial positions — set ox/oy = initial pos so first frame erases correctly
    for (i = 0; i < NUM_ROACHES; i++) {
        roaches[i].x       = 20 + (int)(rand() % (SCREEN_W - ROACH_W - 40));
        roaches[i].y       = 20 + (int)(rand() % (SCREEN_H - ROACH_H - 40));
        roaches[i].ox      = roaches[i].x;
        roaches[i].oy      = roaches[i].y;
        roaches[i].dir     = (unsigned char)(rand() & 7);
        roaches[i].steps   = (unsigned char)(rand() & 31);
        roaches[i].scatter = 0;
    }

    empty_str[0] = 0;

    // Background control — white fill
    ctrls[0].value  = 0;
    ctrls[0].type   = C_AREA;
    ctrls[0].bank   = -1;
    ctrls[0].param  = (unsigned short)(AREA_16COLOR | COLOR_WHITE);
    ctrls[0].x      = 0;
    ctrls[0].y      = 0;
    ctrls[0].w      = SCREEN_W;
    ctrls[0].h      = SCREEN_H;
    ctrls[0].unused = 0;

    // Roach image controls
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

    memset(&ctrlgrp, 0, sizeof(ctrlgrp));
    ctrlgrp.controls = 1 + NUM_ROACHES;
    ctrlgrp.pid      = _sympid;
    ctrlgrp.first    = &ctrls[0];

    memset(&winrec, 0, sizeof(winrec));
    winrec.state    = WIN_NORMAL;
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

    wid = Win_Open(_symbank, &winrec);
    if (wid < 0) exit(1);
    win_id = wid;

    memset(&timer_hdr, 0, sizeof(timer_hdr));
    timer_hdr.startAddr = timer_loop;
    Timer_Add(_symbank, &timer_hdr);

    // Event loop — any click or key press closes the app
    while (1) {
        resp = Msg_Sleep(_sympid, -1, _symmsg);
        if (!(resp & 1)) continue;

        switch (_symmsg[0]) {
        case 0:
            Win_Close((unsigned char)win_id);
            exit(0);
            break;

        case MSR_DSK_WCLICK:
            switch (_symmsg[2]) {
            case DSK_ACT_CLOSE:
            case DSK_ACT_CONTENT:  // click anywhere on window → quit
            case DSK_ACT_KEY:      // any key press → quit
                Win_Close((unsigned char)win_id);
                exit(0);
                break;
            }
            break;

        case MSR_DSK_WFOCUS:
            if (_symmsg[2] == 1)
                do_scatter = 1;
            break;
        }
    }
}
