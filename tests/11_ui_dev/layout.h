#pragma once
// Copy of firmware/controller/layout.h — the firmware copy is the one that counts.
// Display layout, one self-contained block per DISPLAY_VARIANT.
// Tune live with tests/11_ui_dev; its 'p' command prints a replacement block for the variant it runs on.
// Rows are LAYOUT_ROW_H apart from LAYOUT_TOP. LAYOUT_ROWS gives the row of each field, in this order:
// mode, heartbeat, T1, T2, Tset, Tout, dTpot, Room, DP, P2, V1, V2, message

#if DISPLAY_VARIANT == 1   // MSP1691 240x280
#define LAYOUT_W          240
#define LAYOUT_H          280
#define LAYOUT_TOP        4
#define LAYOUT_ROW_H      23
#define LAYOUT_LABEL_X    4
#define LAYOUT_LABEL_DY   5
#define LAYOUT_VALUE_X    40
#define LAYOUT_VALUE_SIZE 2
#define LAYOUT_VALUE_W    10
#define LAYOUT_ROWS       {0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}

#elif DISPLAY_VARIANT == 2 // MSP1901 170x320
#define LAYOUT_W          170
#define LAYOUT_H          320
#define LAYOUT_TOP        4
#define LAYOUT_ROW_H      26
#define LAYOUT_LABEL_X    4
#define LAYOUT_LABEL_DY   5
#define LAYOUT_VALUE_X    40
#define LAYOUT_VALUE_SIZE 2
#define LAYOUT_VALUE_W    10
#define LAYOUT_ROWS       {0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}

#else
#error "DISPLAY_VARIANT must be 1 or 2"
#endif
