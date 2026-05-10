// xroach screensaver for SymbOS — improved bug-like roaches
// Based on the classic Unix xroach by J.T. Anderson (1991)
// SymbOS C port by Salvatore Bognanni
// Improved sprites by ChatGPT

#include <symbos.h>
#include <graphics.h>
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

// Map movement direction -> sprite
// 0=E 1=S 2=W 3=N

static const unsigned char dir_spr[8] = {
    0,0,1,2,2,2,3,0
};

// ---------------------------------------------------------------------------
// ASCII ART SPRITES
// '.' = background
// '3' = roach body
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
// Runtime sprite buffers
// ---------------------------------------------------------------------------

char spr_E[3 + 128];
char spr_W[3 + 128];
char spr_N[3 + 128];
char spr_S[3 + 128];

// Sprite pointer table

const char *sprites[4] = {
    spr_E,
    spr_S,
    spr_W,
    spr_N
};

// ---------------------------------------------------------------------------
// Build SymbOS 4bpp sprite from ASCII art
// ---------------------------------------------------------------------------

static void build_sprite(char *dst, const char **art)
{
    int x, y;
    unsigned char p1, p2;

    dst[0] = 0x08;
    dst[1] = 0x10;
    dst[2] = 0x10;

    dst += 3;

    for (y = 0; y < 16; y++) {

        for (x = 0; x < 16; x += 2) {

            p1 = (art[y][x] == '3') ? 0x3 : 0x1;
            p2 = (art[y][x + 1] == '3') ? 0x3 : 0x1;

            *dst++ = (p1 << 4) | p2;
        }
    }
}

// ---------------------------------------------------------------------------
// Initialize sprites
// ---------------------------------------------------------------------------

static void init_roach_sprites(void)
{
    build_sprite(spr_E, roach_E_art);
    build_sprite(spr_W, roach_W_art);
    build_sprite(spr_N, roach_N_art);
    build_sprite(spr_S, roach_S_art);
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
// Canvas buffers
// ---------------------------------------------------------------------------

#define CANVAS_SIZE ((ROACH_W * ROACH_H / 2) + 24)

_data char canvas[NUM_ROACHES][CANVAS_SIZE];

// ---------------------------------------------------------------------------
// Window/control structures
// ---------------------------------------------------------------------------

_transfer Ctrl ctrls[1 + NUM_ROACHES];
_transfer Ctrl_Group ctrlgrp;
_transfer Window winrec;
_transfer char empty_str[1];

// ---------------------------------------------------------------------------
// Animation tick
// ---------------------------------------------------------------------------

static void anim_tick(signed char wid)
{
    unsigned char i, spd, spr_idx;
    Roach *r;
    int nx, ny, turn;
    const char *spr;
    int bx0, by0, bx1, by1;

    bx0 = SCREEN_W;
    by0 = SCREEN_H;
    bx1 = 0;
    by1 = 0;

    for (i = 0; i < NUM_ROACHES; i++) {

        r = &roaches[i];

        spd = r->scatter ? SCATTER_SPEED : SPEED;

        if (r->scatter)
            r->scatter--;

        nx = r->x + (int)dir_dx[r->dir] * (int)spd;
        ny = r->y + (int)dir_dy[r->dir] * (int)spd;

        if (nx < 0 || nx + ROACH_W > SCREEN_W ||
            ny < 0 || ny + ROACH_H > SCREEN_H) {

            r->dir = (r->dir + 4) & 7;
            r->steps = 3 + (rand() & 7);

            nx = r->x;
            ny = r->y;
        }

        if (r->steps == 0) {

            turn = (rand() % 7) - 3;

            r->dir = (r->dir + 8 + turn) & 7;
            r->steps = 10 + (rand() & 31);

        } else {

            r->steps--;
        }

        if (r->ox < bx0) bx0 = r->ox;
        if (r->oy < by0) by0 = r->oy;

        if (r->ox + ROACH_W > bx1)
            bx1 = r->ox + ROACH_W;

        if (r->oy + ROACH_H > by1)
            by1 = r->oy + ROACH_H;

        spr_idx = dir_spr[r->dir];
        spr = sprites[spr_idx];

        Gfx_Select(canvas[i]);
        Gfx_Clear(canvas[i], COLOR_BLACK);
        Gfx_Put((char *)spr, 0, 0, PUT_SET);

        r->x = nx;
        r->y = ny;

        ctrls[1 + i].x = (unsigned short)nx;
        ctrls[1 + i].y = (unsigned short)ny;

        if (nx < bx0) bx0 = nx;
        if (ny < by0) by0 = ny;

        if (nx + ROACH_W > bx1)
            bx1 = nx + ROACH_W;

        if (ny + ROACH_H > by1)
            by1 = ny + ROACH_H;

        r->ox = nx;
        r->oy = ny;
    }

    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;

    if (bx1 > SCREEN_W) bx1 = SCREEN_W;
    if (by1 > SCREEN_H) by1 = SCREEN_H;

    if (bx1 > bx0 && by1 > by0) {

        Win_Redraw_Area(
            (unsigned char)wid,
            255,
            0,
            (unsigned short)bx0,
            (unsigned short)by0,
            (unsigned short)(bx1 - bx0),
            (unsigned short)(by1 - by0)
        );
    }
}

// ---------------------------------------------------------------------------
// Start animation
// ---------------------------------------------------------------------------

void start_animation(void)
{
    unsigned short resp;
    signed char wid;

    unsigned char i;
    unsigned char tick;
    unsigned char do_scatter;

    unsigned short mx0, my0;

    srand((unsigned int)Sys_Counter16());

    init_roach_sprites();

    for (i = 0; i < NUM_ROACHES; i++) {

        Gfx_Init(canvas[i], ROACH_W, ROACH_H);

        Gfx_Select(canvas[i]);

        Gfx_Clear(canvas[i], COLOR_BLACK);

        Gfx_Put(spr_E, 0, 0, PUT_SET);
    }

    for (i = 0; i < NUM_ROACHES; i++) {

        roaches[i].x =
            20 + (rand() % (SCREEN_W - ROACH_W - 40));

        roaches[i].y =
            20 + (rand() % (SCREEN_H - ROACH_H - 40));

        roaches[i].ox = roaches[i].x;
        roaches[i].oy = roaches[i].y;

        roaches[i].dir = rand() & 7;
        roaches[i].steps = rand() & 31;
        roaches[i].scatter = 0;
    }

    empty_str[0] = 0;

    tick = 0;
    do_scatter = 0;

    ctrls[0].value  = 0;
    ctrls[0].type   = C_AREA;
    ctrls[0].bank   = -1;
    ctrls[0].param  = AREA_16COLOR | COLOR_BLACK;
    ctrls[0].x      = 0;
    ctrls[0].y      = 0;
    ctrls[0].w      = SCREEN_W;
    ctrls[0].h      = SCREEN_H;
    ctrls[0].unused = 0;

    for (i = 0; i < NUM_ROACHES; i++) {

        ctrls[1 + i].value  = 1 + i;
        ctrls[1 + i].type   = C_IMAGE_EXT;
        ctrls[1 + i].bank   = -1;
        ctrls[1 + i].param  = (unsigned short)canvas[i];
        ctrls[1 + i].x      = roaches[i].x;
        ctrls[1 + i].y      = roaches[i].y;
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

    if (wid < 0)
        return;

    mx0 = Mouse_X();
    my0 = Mouse_Y();

    while (1) {

        if (Mouse_X() != mx0 ||
            Mouse_Y() != my0 ||
            Mouse_Buttons()) {

            Win_Close((unsigned char)wid);
            return;
        }

        resp = Msg_Receive(_sympid, -1, _symmsg);

        if (resp & 1) {

            switch (_symmsg[0]) {

            case 0:

                Win_Close((unsigned char)wid);
                exit(0);

            case MSR_DSK_WCLICK:

                switch (_symmsg[2]) {

                case DSK_ACT_CLOSE:
                case DSK_ACT_CONTENT:
                case DSK_ACT_KEY:

                    Win_Close((unsigned char)wid);
                    return;
                }

                break;

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
                    roaches[i].dir = rand() & 7;
                    roaches[i].steps = 0;
                }
            }

            anim_tick(wid);
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
    unsigned char got_msg;
    unsigned char b;

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
