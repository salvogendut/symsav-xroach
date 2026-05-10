// xroach screensaver for SymbOS — fullscreen direct-render mode
// Based on the classic Unix xroach by J.T. Anderson (1991)
// SymbOS C port by Salvatore Bognanni
// Sprites by ChatGPT; fullscreen via Bank_Copy to video RAM

#include <symbos.h>
#include <symbos/msgid.h>
#include <symbos/keys.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Screensaver protocol message IDs
// ---------------------------------------------------------------------------

#define MSC_SAV_INIT    1
#define MSC_SAV_START   2
#define MSC_SAV_CONFIG  3
#define MSR_SAV_CONFIG  4

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
#define FRAME_SKIP      4

// ---------------------------------------------------------------------------
// Direction table
// 0=E 1=SE 2=S 3=SW 4=W 5=NW 6=N 7=NE
// ---------------------------------------------------------------------------

static const signed char dir_dx[8] = {
     3,  2,  0, -2,
    -3, -2,  0,  2
};

static const signed char dir_dy[8] = {
     0,  2,  3,  2,
     0, -2, -3, -2
};

// Map direction -> sprite index: 0=E 1=S 2=W 3=N

static const unsigned char dir_spr[8] = {
    0, 0, 1, 2, 2, 2, 3, 0
};

// ---------------------------------------------------------------------------
// Sprite ASCII art
// '.' = background (ink 0)
// '3' = roach body (ink 3)
// ---------------------------------------------------------------------------

// EAST

static const char *roach_E_art[16] = {
    "................",
    "..........3.....",
    ".........33.....",
    "......3333333...",
    "....33333333333.",
    "...333333333333.",
    "..33..333333..33",
    ".33.333333333..3",
    "3333333333333333",
    ".33.333333333..3",
    "..33..333333..33",
    "...333333333333.",
    "....3333333333..",
    "......333333....",
    ".........33.....",
    "...........3...."
};

// WEST

static const char *roach_W_art[16] = {
    "................",
    ".....3..........",
    ".....33.........",
    "...3333333......",
    ".33333333333....",
    ".333333333333...",
    "33..333333..33..",
    "3..333333333.33.",
    "3333333333333333",
    "3..333333333.33.",
    "33..333333..33..",
    ".333333333333...",
    "..3333333333....",
    "....333333......",
    ".....33.........",
    "....3..........."
};

// NORTH

static const char *roach_N_art[16] = {
    ".......33.......",
    "......3333......",
    ".....333333.....",
    "...3333333333...",
    "..333333333333..",
    "..33..3333..33..",
    ".33..333333..33.",
    ".33.33333333.33.",
    ".33333333333333.",
    "..333333333333..",
    "..33.333333.33..",
    "...3..3333..3...",
    "......3333......",
    ".....33..33.....",
    "....33....33....",
    "...3........3..."
};

// SOUTH

static const char *roach_S_art[16] = {
    "...3........3...",
    "....33....33....",
    ".....33..33.....",
    "......3333......",
    "...3..3333..3...",
    "..33.333333.33..",
    "..333333333333..",
    ".33333333333333.",
    ".33.33333333.33.",
    ".33..333333..33.",
    "..33..3333..33..",
    "..333333333333..",
    "...3333333333...",
    ".....333333.....",
    "......3333......",
    ".......33......."
};

// ---------------------------------------------------------------------------
// Mode 1 sprite buffers: 16 rows × 4 bytes = 64 bytes per direction
// 0=E 1=S 2=W 3=N
// CPC Mode 1 pixel encoding for 4 pixels p0..p3, each 2-bit ink:
//   byte = (p0_lo<<7)|(p1_lo<<6)|(p2_lo<<5)|(p3_lo<<4)
//         |(p0_hi<<3)|(p1_hi<<2)|(p2_hi<<1)|(p3_hi<<0)
// ink 0 (00) -> 0x00;  ink 3 (11) -> 0xFF
// ---------------------------------------------------------------------------

_data unsigned char m1_spr[4][64];

// Zero-filled plane buffer used for screen clear (one CPC character plane =
// 25 char-rows × 80 bytes = 2000 bytes).  BSS = guaranteed zero.

_data unsigned char zero_plane[2000];

// 4-byte zero row used for erasing one sprite scan-line

_data unsigned char zero_row[4];

// ---------------------------------------------------------------------------
// Build Mode 1 bytes from ASCII art
// ---------------------------------------------------------------------------

static void build_m1_sprite(unsigned char *dst, const char **art)
{
    int row, col, px;
    unsigned char b;

    for (row = 0; row < 16; row++) {
        for (col = 0; col < 4; col++) {
            px = col << 2;
            // Start with all 4 pixels = ink 1 (black background, 0xF0).
            // Flip the hi-bit of each body pixel to convert ink 1 → ink 3.
            b = 0xF0;
            if (art[row][px + 0] == '3') b |= 0x08;
            if (art[row][px + 1] == '3') b |= 0x04;
            if (art[row][px + 2] == '3') b |= 0x02;
            if (art[row][px + 3] == '3') b |= 0x01;
            dst[row * 4 + col] = b;
        }
    }
}

static void init_sprites(void)
{
    build_m1_sprite(m1_spr[0], roach_E_art);
    build_m1_sprite(m1_spr[1], roach_S_art);
    build_m1_sprite(m1_spr[2], roach_W_art);
    build_m1_sprite(m1_spr[3], roach_N_art);
}

// Key_Status() reads a software buffer filled by the desktop — unusable while
// the desktop is stopped.  Key_Down() polls the kernel's hardware scan table
// which is always current.  Check all 80 CPC scan codes for any key pressed.

static unsigned char any_key_down(void)
{
    unsigned char sc;
    for (sc = 0; sc < 80; sc++) {
        if (Key_Down(sc)) return 1;
    }
    return 0;
}

// Fill background buffers with ink 1 (black in SymbOS Mode 1 palette).
// zero_plane and zero_row are BSS, so we must fill them at runtime.

static void init_screen_buffers(void)
{
    memset(zero_plane, 0xF0, sizeof(zero_plane));
    memset(zero_row,   0xF0, sizeof(zero_row));
}

// ---------------------------------------------------------------------------
// CPC Mode 1 screen address
// X must be a multiple of 4 (one byte = 4 pixels).
// addr = 0xC000 + (y/8)*80 + (y%8)*0x800 + x/4
// ---------------------------------------------------------------------------

static unsigned short scr_addr(int x, int y)
{
    return 0xC000u
         + (unsigned short)(y >> 3) * 80u
         + (unsigned short)(y & 7)  * 0x800u
         + (unsigned short)(x >> 2);
}

// ---------------------------------------------------------------------------
// Screen operations via Bank_Copy to video RAM (bank 0, 0xC000)
// ---------------------------------------------------------------------------

static void xr_clear_screen(void)
{
    unsigned char k;

    // CPC screen: 8 character planes, each at 0xC000 + k*0x800,
    // 25 rows × 80 bytes = 2000 bytes each.
    for (k = 0; k < 8; k++) {
        Bank_Copy(
            0,
            (char *)(0xC000u + (unsigned short)k * 0x0800u),
            _symbank,
            (char *)zero_plane,
            2000u
        );
    }
}

static void xr_erase(int sx, int sy)
{
    int row;

    for (row = 0; row < ROACH_H; row++) {
        Bank_Copy(
            0, (char *)scr_addr(sx, sy + row),
            _symbank, (char *)zero_row,
            4u
        );
    }
}

static void xr_draw(int sx, int sy, unsigned char *spr)
{
    int row;

    for (row = 0; row < ROACH_H; row++) {
        Bank_Copy(
            0, (char *)scr_addr(sx, sy + row),
            _symbank, (char *)(spr + row * 4),
            4u
        );
    }
}

// ---------------------------------------------------------------------------
// Roach state
// ---------------------------------------------------------------------------

typedef struct {
    int x, y;
    int ox, oy;
    unsigned char dir;
    unsigned char steps;
    unsigned char scatter;
} Roach;

Roach roaches[NUM_ROACHES];

// ---------------------------------------------------------------------------
// Animation tick — direct screen rendering
// ---------------------------------------------------------------------------

static void anim_tick(void)
{
    unsigned char i, spd, spr_idx;
    Roach *r;
    int nx, ny, turn, sx;

    for (i = 0; i < NUM_ROACHES; i++) {

        r = &roaches[i];

        spd = r->scatter ? SCATTER_SPEED : SPEED;

        if (r->scatter)
            r->scatter--;

        nx = r->x + (int)dir_dx[r->dir] * (int)spd;
        ny = r->y + (int)dir_dy[r->dir] * (int)spd;

        if (nx < 0 || nx + ROACH_W > SCREEN_W ||
            ny < 0 || ny + ROACH_H > SCREEN_H) {

            r->dir   = (r->dir + 4) & 7;
            r->steps = 3 + (rand() & 7);
            nx = r->x;
            ny = r->y;
        }

        if (r->steps == 0) {

            turn     = (rand() % 7) - 3;
            r->dir   = (r->dir + 8 + turn) & 7;
            r->steps = 10 + (rand() & 31);

        } else {

            r->steps--;
        }

        // Erase old position (X snapped to 4-pixel grid)
        sx = r->ox & ~3;
        xr_erase(sx, r->oy);

        // Draw at new position
        r->x  = nx;
        r->y  = ny;
        r->ox = nx;
        r->oy = ny;

        spr_idx = dir_spr[r->dir];
        sx = nx & ~3;
        xr_draw(sx, ny, m1_spr[spr_idx]);
    }
}

// ---------------------------------------------------------------------------
// Window/control structures (minimal: one C_AREA background)
// ---------------------------------------------------------------------------

_transfer Ctrl      ctrls[1];
_transfer Ctrl_Group ctrlgrp;
_transfer Window    winrec;
_transfer char      empty_str[1];

// ---------------------------------------------------------------------------
// Desktop stop / resume
// ---------------------------------------------------------------------------

static void desktop_stop(unsigned char wid)
{
    _symmsg[0] = MSC_DSK_DSKSRV;
    _symmsg[1] = DSK_SRV_DSKSTP;
    _symmsg[2] = 0xFF;
    _symmsg[3] = wid;

    while (Msg_Send(_sympid, 2, _symmsg) == 0);

    Msg_Wait(_sympid, 2, _symmsg, MSR_DSK_DSKSRV);
}

static void desktop_cont(void)
{
    _symmsg[0] = MSC_DSK_DSKSRV;
    _symmsg[1] = DSK_SRV_DSKCNT;

    while (Msg_Send(_sympid, 2, _symmsg) == 0);

    Idle();
}

// ---------------------------------------------------------------------------
// Animation loop
// ---------------------------------------------------------------------------

void start_animation(void)
{
    unsigned short resp;
    signed char    wid;
    unsigned char  i, tick, do_scatter;
    unsigned short mx0, my0;

    srand((unsigned int)Sys_Counter16());
    init_sprites();
    init_screen_buffers();

    for (i = 0; i < NUM_ROACHES; i++) {

        roaches[i].x = (20 + (rand() % (SCREEN_W - ROACH_W - 40))) & ~3;
        roaches[i].y =  20 + (rand() % (SCREEN_H - ROACH_H - 40));
        roaches[i].ox    = roaches[i].x;
        roaches[i].oy    = roaches[i].y;
        roaches[i].dir   = rand() & 7;
        roaches[i].steps = rand() & 31;
        roaches[i].scatter = 0;
    }

    // Open a minimal fullscreen window (provides a valid wid for desktop_stop)
    empty_str[0] = 0;

    ctrls[0].value  = 0;
    ctrls[0].type   = C_AREA;
    ctrls[0].bank   = -1;
    ctrls[0].param  = AREA_16COLOR | COLOR_BLACK;
    ctrls[0].x      = 0;
    ctrls[0].y      = 0;
    ctrls[0].w      = SCREEN_W;
    ctrls[0].h      = SCREEN_H;
    ctrls[0].unused = 0;

    memset(&ctrlgrp, 0, sizeof(ctrlgrp));
    ctrlgrp.controls = 1;
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

    if (wid < 0)
        return;

    // Stop desktop and take over the screen
    desktop_stop((unsigned char)wid);
    xr_clear_screen();

    mx0 = Mouse_X();
    my0 = Mouse_Y();
    tick       = 0;
    do_scatter = 0;

    while (1) {

        if (Mouse_X() != mx0 ||
            Mouse_Y() != my0 ||
            Mouse_Buttons()   ||
            any_key_down()) {

            desktop_cont();
            Idle();
            Win_Close((unsigned char)wid);
            Screen_Redraw();
            return;
        }

        resp = Msg_Receive(_sympid, -1, _symmsg);

        if (resp & 1) {

            switch (_symmsg[0]) {

            case 0:

                desktop_cont();
                Win_Close((unsigned char)wid);
                exit(0);

            case MSR_DSK_WFOCUS:

                if (_symmsg[2] == 1)
                    do_scatter = 1;

                break;
            }
        }

        if (++tick >= FRAME_SKIP) {

            tick = 0;

            if (do_scatter) {

                do_scatter = 0;

                for (i = 0; i < NUM_ROACHES; i++) {

                    roaches[i].scatter = SCATTER_TICKS;
                    roaches[i].dir     = rand() & 7;
                    roaches[i].steps   = 0;
                }
            }

            anim_tick();
        }

        Idle();
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    unsigned short resp;
    unsigned char  got_msg;
    unsigned char  b;

    got_msg = 0;

    for (b = 0; b < 10; b++) {

        Idle();

        resp = Msg_Receive(_sympid, -1, _symmsg);

        if (resp & 0x01) {

            got_msg = 1;
            break;
        }
    }

    if (!got_msg) {

        start_animation();
        exit(0);
    }

    while (1) {

        switch (_symmsg[0]) {

        case 0:

            exit(0);

        case MSC_SAV_INIT:

            break;

        case MSC_SAV_START:

            start_animation();
            break;

        case MSC_SAV_CONFIG:

            break;
        }

        do {

            resp = Msg_Sleep(_sympid, -1, _symmsg);

        } while (!(resp & 0x01));
    }
}
