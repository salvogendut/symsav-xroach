// xroach screensaver for SymbOS — fullscreen direct-render mode
// Based on the classic Unix xroach by J.T. Anderson (1991)
// SymbOS C port by Salvatore Bognanni
// Sprites by Salvatore Bognanni (Pixilate); fullscreen via Bank_Copy to video RAM

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

#define MAX_ROACHES     9
#define ROACH_W         16
#define ROACH_H         16
#define SCREEN_W        320
#define SCREEN_H        200

#define SCATTER_SPEED   6
#define SCATTER_TICKS   25
#define FRAME_SKIP      4
#define FLEE_DIST       64

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
    ".....3....3.....",
    "3....3....3.....",
    "33...3...33.....",
    ".33..33.33......",
    "..3.33333.....33",
    "..3333.33.3..33.",
    ".333...3333333..",
    "333..33333333...",
    "3333333333333...",
    ".3333333333333..",
    "..3333333.3..33.",
    "..3.33333.....33",
    ".33..33.33......",
    "33...3...33.....",
    "3....3....3.....",
    ".....3....3.....",
};

// WEST

static const char *roach_W_art[16] = {
    ".....3....3.....",
    ".....3....3....3",
    ".....33...3...33",
    "......33.33..33.",
    "33.....33333.3..",
    ".33..3.3333333..",
    "..3333333333333.",
    "...3333333333333",
    "...33333333..333",
    "..3333333...333.",
    ".33..3.33.3333..",
    "33.....33333.3..",
    "......33.33..33.",
    ".....33...3...33",
    ".....3....3....3",
    ".....3....3.....",
};

// NORTH

static const char *roach_N_art[16] = {
    "....3......3....",
    "....33....33....",
    ".....33..33.....",
    "......3333......",
    "......3333......",
    "333..333333..333",
    "..33..3333..33..",
    "...3333333333...",
    "....33333333....",
    "...33..333333...",
    "333333.333333333",
    "....33..3333....",
    ".....33.333.....",
    "...3333333333...",
    "..33..3333..33..",
    ".33....33....33.",
};

// SOUTH

static const char *roach_S_art[16] = {
    ".33....33....33.",
    "..33..3333..33..",
    "...3333333333...",
    ".....333.33.....",
    "....3333..33....",
    "333333333.333333",
    "...333333..33...",
    "....33333333....",
    "...3333333333...",
    "..33..3333..33..",
    "333..333333..333",
    "......3333......",
    "......3333......",
    ".....33..33.....",
    "....33....33....",
    "....3......3....",
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
_data unsigned char m1_spr_white[4][64];  // ink 2 (0x0F per all-body byte)

// Zero-filled plane buffer used for screen clear (one CPC character plane =
// 25 char-rows × 80 bytes = 2000 bytes).  BSS = guaranteed zero.

_data unsigned char zero_plane[2000];

// 4-byte zero row used for erasing one sprite scan-line

_data unsigned char zero_row[4];

// ---------------------------------------------------------------------------
// Build Mode 1 bytes from ASCII art
// ---------------------------------------------------------------------------

// ink2=0: body pixels → ink 3 (set lo-nibble bit on top of ink-1 background)
// ink2=1: body pixels → ink 2 (swap: clear hi-nibble bit, set lo-nibble bit)

static void build_sprite(unsigned char *dst, const char **art,
                         unsigned char ink2)
{
    int row, col, px;
    unsigned char b;

    for (row = 0; row < 16; row++) {
        for (col = 0; col < 4; col++) {
            px = col << 2;
            b = 0xF0;
            if (ink2) {
                if (art[row][px+0]=='3'){b&=~0x80;b|=0x08;}
                if (art[row][px+1]=='3'){b&=~0x40;b|=0x04;}
                if (art[row][px+2]=='3'){b&=~0x20;b|=0x02;}
                if (art[row][px+3]=='3'){b&=~0x10;b|=0x01;}
            } else {
                if (art[row][px+0]=='3') b|=0x08;
                if (art[row][px+1]=='3') b|=0x04;
                if (art[row][px+2]=='3') b|=0x02;
                if (art[row][px+3]=='3') b|=0x01;
            }
            dst[row*4+col] = b;
        }
    }
}

static void init_sprites(void)
{
    build_sprite(m1_spr[0], roach_E_art, 0);
    build_sprite(m1_spr[1], roach_S_art, 0);
    build_sprite(m1_spr[2], roach_W_art, 0);
    build_sprite(m1_spr[3], roach_N_art, 0);
    build_sprite(m1_spr_white[0], roach_E_art, 1);
    build_sprite(m1_spr_white[1], roach_S_art, 1);
    build_sprite(m1_spr_white[2], roach_W_art, 1);
    build_sprite(m1_spr_white[3], roach_N_art, 1);
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

Roach roaches[MAX_ROACHES];
Roach white_roach;

// ---------------------------------------------------------------------------
// Direction helper: returns the direction index most aligned with (dx, dy)
// ---------------------------------------------------------------------------

static unsigned char best_dir(int dx, int dy)
{
    unsigned char best, i;
    int best_dot, dot;
    best     = 0;
    best_dot = -32000;
    for (i = 0; i < 8; i++) {
        dot = (int)dir_dx[i] * dx + (int)dir_dy[i] * dy;
        if (dot > best_dot) { best_dot = dot; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Animation tick — direct screen rendering
// behavior: 0 = flee from white roach, 1 = chase white roach
// ---------------------------------------------------------------------------

static void anim_tick(unsigned char num_roaches, unsigned char speed,
                      unsigned char behavior)
{
    unsigned char i, spd, spr_idx;
    Roach *r;
    int nx, ny, sx;
    int wdx, wdy, wdist;

    // --- Move white roach (wanders, ignores other roaches) ---
    {
        Roach *w = &white_roach;

        nx = w->x + (int)dir_dx[w->dir] * SCATTER_SPEED;
        ny = w->y + (int)dir_dy[w->dir] * SCATTER_SPEED;

        if (nx < 0 || nx + ROACH_W > SCREEN_W ||
            ny < 0 || ny + ROACH_H > SCREEN_H) {
            w->dir   = (w->dir + 4) & 7;
            w->steps = 3 + (rand() & 7);
            nx = w->x;
            ny = w->y;
        }

        if (w->steps == 0) {
            w->dir   = (w->dir + 8 + (rand() % 7) - 3) & 7;
            w->steps = 10 + (rand() & 31);
        } else {
            w->steps--;
        }

        sx = w->ox & ~3;
        xr_erase(sx, w->oy);
        w->x = nx; w->y = ny; w->ox = nx; w->oy = ny;
        spr_idx = dir_spr[w->dir];
        xr_draw(nx & ~3, ny, m1_spr_white[spr_idx]);
    }

    // --- Move regular roaches ---
    for (i = 0; i < num_roaches; i++) {

        r = &roaches[i];

        // React to white roach proximity
        wdx   = r->x - white_roach.x;
        wdy   = r->y - white_roach.y;
        wdist = (wdx < 0 ? -wdx : wdx) + (wdy < 0 ? -wdy : wdy);
        if (wdist < FLEE_DIST) {
            // flee: away from white (aligned with wdx,wdy)
            // chase: toward white (aligned with -wdx,-wdy)
            r->dir    = behavior ? best_dir(-wdx, -wdy) : best_dir(wdx, wdy);
            r->scatter = SCATTER_TICKS;
            r->steps   = 8;
        }

        spd = r->scatter ? SCATTER_SPEED : speed;

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

            r->dir   = (r->dir + 8 + (rand() % 7) - 3) & 7;
            r->steps = 10 + (rand() & 31);

        } else {

            r->steps--;
        }

        sx = r->ox & ~3;
        xr_erase(sx, r->oy);

        r->x  = nx;
        r->y  = ny;
        r->ox = nx;
        r->oy = ny;

        spr_idx = dir_spr[r->dir];
        xr_draw(nx & ~3, ny, m1_spr[spr_idx]);
    }
}

// ---------------------------------------------------------------------------
// Config data and GUI structures
// ---------------------------------------------------------------------------

// cfgdat[0..3] = "XRCH" magic; [4] = roach count; [5] = speed; [6] = behavior
_transfer char cfgdat[7]      = { 'X', 'R', 'C', 'H', 6, 3, 0 };

// Working copies edited while the config dialog is open
_transfer char tmp_roaches    = 6;
_transfer char tmp_speed      = 3;
_transfer char tmp_behavior   = 0;  // 0=flee, 1=chase

// Saved PID of the screensaver manager that requested config
_transfer char cfg_prz        = 0;

// ID of the open config window (-1 = closed)
_transfer signed char cfgwin_id = -1;

// Radio group coordinate buffers — must be {-1,-1,-1,-1} before first open
_transfer char rg_roaches[4]   = { -1, -1, -1, -1 };
_transfer char rg_speed[4]     = { -1, -1, -1, -1 };
_transfer char rg_behavior[4]  = { -1, -1, -1, -1 };

// Text/frame descriptors
_transfer Ctrl_TFrame cfg_tf      = { "Settings", (COLOR_BLACK<<2)|COLOR_ORANGE, 0 };
_transfer Ctrl_Text   cfg_lbl_r   = { "Roaches:", (COLOR_BLACK<<2)|COLOR_ORANGE, 0 };
_transfer Ctrl_Text   cfg_lbl_s   = { "Speed:",   (COLOR_BLACK<<2)|COLOR_ORANGE, 0 };
_transfer Ctrl_Text   cfg_lbl_b   = { "Behavior:",(COLOR_BLACK<<2)|COLOR_ORANGE, 0 };

// Radio descriptors — Ctrl_Radio.value is what gets stored in the status var
_transfer Ctrl_Radio cfg_rad_r3 = { &tmp_roaches,  "3",      (COLOR_BLACK<<2)|COLOR_ORANGE, 3, rg_roaches  };
_transfer Ctrl_Radio cfg_rad_r6 = { &tmp_roaches,  "6",      (COLOR_BLACK<<2)|COLOR_ORANGE, 6, rg_roaches  };
_transfer Ctrl_Radio cfg_rad_r9 = { &tmp_roaches,  "9",      (COLOR_BLACK<<2)|COLOR_ORANGE, 9, rg_roaches  };
_transfer Ctrl_Radio cfg_rad_sl = { &tmp_speed,    "Slow",   (COLOR_BLACK<<2)|COLOR_ORANGE, 1, rg_speed    };
_transfer Ctrl_Radio cfg_rad_no = { &tmp_speed,    "Normal", (COLOR_BLACK<<2)|COLOR_ORANGE, 3, rg_speed    };
_transfer Ctrl_Radio cfg_rad_fa = { &tmp_speed,    "Fast",   (COLOR_BLACK<<2)|COLOR_ORANGE, 6, rg_speed    };
_transfer Ctrl_Radio cfg_rad_bf = { &tmp_behavior, "Flee",   (COLOR_BLACK<<2)|COLOR_ORANGE, 0, rg_behavior };
_transfer Ctrl_Radio cfg_rad_bc = { &tmp_behavior, "Chase",  (COLOR_BLACK<<2)|COLOR_ORANGE, 1, rg_behavior };

// Config dialog controls — must be CONSECUTIVE in transfer segment
// Layout: 200×74 content area
//   Frame "Settings" covers rows 0..50
//   Row 1 (Roaches): y=10  Row 2 (Speed): y=22  Row 3 (Behavior): y=34
//   Buttons: y=58
_transfer Ctrl ccc0  = { 0,  C_AREA,   -1, COLOR_ORANGE,                      0,  0, 200, 74, 0 };
_transfer Ctrl ccc1  = { 0,  C_TFRAME, -1, (unsigned short)&cfg_tf,           2,  1, 196, 50, 0 };
_transfer Ctrl ccc2  = { 0,  C_TEXT,   -1, (unsigned short)&cfg_lbl_r,        8, 10,  52,  8, 0 };
_transfer Ctrl ccc3  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rad_r3,      64, 10,  18,  8, 0 };
_transfer Ctrl ccc4  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rad_r6,      84, 10,  18,  8, 0 };
_transfer Ctrl ccc5  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rad_r9,     104, 10,  18,  8, 0 };
_transfer Ctrl ccc6  = { 0,  C_TEXT,   -1, (unsigned short)&cfg_lbl_s,        8, 22,  52,  8, 0 };
_transfer Ctrl ccc7  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rad_sl,      64, 22,  30,  8, 0 };
_transfer Ctrl ccc8  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rad_no,      96, 22,  42,  8, 0 };
_transfer Ctrl ccc9  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rad_fa,     140, 22,  30,  8, 0 };
_transfer Ctrl ccc10 = { 0,  C_TEXT,   -1, (unsigned short)&cfg_lbl_b,        8, 34,  56,  8, 0 };
_transfer Ctrl ccc11 = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rad_bf,      68, 34,  30,  8, 0 };
_transfer Ctrl ccc12 = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rad_bc,     100, 34,  42,  8, 0 };
_transfer Ctrl ccc13 = { 10, C_BUTTON, -1, (unsigned short)"OK",             50, 58,  32, 12, 0 };
_transfer Ctrl ccc14 = { 11, C_BUTTON, -1, (unsigned short)"Cancel",         90, 58,  52, 12, 0 };

// Ctrl_Group and Window for the config dialog
_transfer Ctrl_Group cfgcg;
_transfer Window     cfgwin;
_transfer char       cfg_title[7] = { 'X', 'R', 'o', 'a', 'c', 'h', 0 };

// ---------------------------------------------------------------------------
// Window/control structures for the animation window (minimal)
// ---------------------------------------------------------------------------

_transfer Ctrl      ctrls[1];
_transfer Ctrl_Group ctrlgrp;
_transfer Window    winrec;
_transfer char      empty_str[1];

// ---------------------------------------------------------------------------
// Config dialog helpers
// ---------------------------------------------------------------------------

static void cfg_open(void)
{
    if (cfgwin_id >= 0)
        return;

    // Copy current config into working vars
    tmp_roaches  = cfgdat[4];
    tmp_speed    = cfgdat[5];
    tmp_behavior = cfgdat[6];

    // Reset radio group coordinate buffers so SymbOS starts fresh
    rg_roaches[0]  = rg_roaches[1]  = rg_roaches[2]  = rg_roaches[3]  = -1;
    rg_speed[0]    = rg_speed[1]    = rg_speed[2]     = rg_speed[3]    = -1;
    rg_behavior[0] = rg_behavior[1] = rg_behavior[2]  = rg_behavior[3] = -1;

    memset(&cfgcg, 0, sizeof(cfgcg));
    cfgcg.controls = 15;
    cfgcg.pid      = _sympid;
    cfgcg.first    = &ccc0;

    memset(&cfgwin, 0, sizeof(cfgwin));
    cfgwin.state    = WIN_NORMAL;
    cfgwin.flags    = WIN_TITLE | WIN_CENTERED | WIN_NOTTASKBAR;
    cfgwin.pid      = _sympid;
    cfgwin.w        = 200;
    cfgwin.h        = 74;
    cfgwin.wfull    = 200;
    cfgwin.hfull    = 74;
    cfgwin.wmin     = 200;
    cfgwin.hmin     = 74;
    cfgwin.wmax     = 200;
    cfgwin.hmax     = 74;
    cfgwin.title    = cfg_title;
    cfgwin.controls = &cfgcg;

    cfgwin_id = Win_Open(_symbank, &cfgwin);
}

static void cfg_close(void)
{
    if (cfgwin_id < 0)
        return;
    Win_Close((unsigned char)cfgwin_id);
    cfgwin_id = -1;
}

static void cfg_ok(void)
{
    cfgdat[4] = tmp_roaches;
    cfgdat[5] = tmp_speed;
    cfgdat[6] = tmp_behavior;
    cfg_close();

    if (cfg_prz) {
        _symmsg[0] = MSR_SAV_CONFIG;
        _symmsg[1] = _symbank;
        _symmsg[2] = (char)((unsigned short)cfgdat & 0xFF);
        _symmsg[3] = (char)((unsigned short)cfgdat >> 8);
        while (!Msg_Send(cfg_prz, _sympid, _symmsg));
        cfg_prz = 0;
    }
}

static void cfg_cancel(void)
{
    cfg_close();
    cfg_prz = 0;
}

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
    unsigned char  num_roaches, speed, behavior;

    num_roaches = (unsigned char)cfgdat[4];
    speed       = (unsigned char)cfgdat[5];
    behavior    = (unsigned char)cfgdat[6];

    if (num_roaches < 1 || num_roaches > MAX_ROACHES) num_roaches = 6;
    if (speed < 1 || speed > 9)                       speed = 3;
    if (behavior > 1)                                  behavior = 0;

    srand((unsigned int)Sys_Counter16());
    init_sprites();
    init_screen_buffers();

    for (i = 0; i < num_roaches; i++) {

        roaches[i].x = (20 + (rand() % (SCREEN_W - ROACH_W - 40))) & ~3;
        roaches[i].y =  20 + (rand() % (SCREEN_H - ROACH_H - 40));
        roaches[i].ox    = roaches[i].x;
        roaches[i].oy    = roaches[i].y;
        roaches[i].dir   = rand() & 7;
        roaches[i].steps = rand() & 31;
        roaches[i].scatter = 0;
    }

    white_roach.x = (20 + (rand() % (SCREEN_W - ROACH_W - 40))) & ~3;
    white_roach.y =  20 + (rand() % (SCREEN_H - ROACH_H - 40));
    white_roach.ox    = white_roach.x;
    white_roach.oy    = white_roach.y;
    white_roach.dir   = rand() & 7;
    white_roach.steps = rand() & 31;
    white_roach.scatter = 0;

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

                for (i = 0; i < num_roaches; i++) {

                    roaches[i].scatter = SCATTER_TICKS;
                    roaches[i].dir     = rand() & 7;
                    roaches[i].steps   = 0;
                }
            }

            anim_tick(num_roaches, speed, behavior);
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
    unsigned char  sender;
    char           init_tmp[7];

    got_msg = 0;
    sender  = 0;

    for (b = 0; b < 10; b++) {

        Idle();

        resp = Msg_Receive(_sympid, -1, _symmsg);

        if (resp & 0x01) {

            got_msg = 1;
            sender  = (unsigned char)(resp >> 8);
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

            // Screensaver manager passes saved config: bank in [1], addr in [2/3]
            Bank_Copy(
                _symbank, init_tmp,
                (unsigned char)_symmsg[1],
                (char *)((unsigned short)((unsigned char)_symmsg[3] << 8)
                         | (unsigned char)_symmsg[2]),
                7
            );
            if (init_tmp[0] == 'X' && init_tmp[1] == 'R' &&
                init_tmp[2] == 'C' && init_tmp[3] == 'H') {
                cfgdat[4] = init_tmp[4];
                cfgdat[5] = init_tmp[5];
                cfgdat[6] = init_tmp[6];
            }
            break;

        case MSC_SAV_START:

            start_animation();
            break;

        case MSC_SAV_CONFIG:

            cfg_prz = sender;
            cfg_open();
            break;

        default:

            // Config window click messages
            if ((unsigned char)_symmsg[0] == MSR_DSK_WCLICK &&
                cfgwin_id >= 0 &&
                (unsigned char)_symmsg[1] == (unsigned char)cfgwin_id) {

                if ((unsigned char)_symmsg[2] == DSK_ACT_CLOSE) {

                    cfg_cancel();

                } else if ((unsigned char)_symmsg[2] == DSK_ACT_CONTENT) {

                    if ((unsigned char)_symmsg[8] == 10)
                        cfg_ok();
                    else if ((unsigned char)_symmsg[8] == 11)
                        cfg_cancel();
                }
            }
            break;
        }

        do {

            resp = Msg_Sleep(_sympid, -1, _symmsg);

        } while (!(resp & 0x01));

        sender = (unsigned char)(resp >> 8);
    }
}
