#pragma once
// Copy of firmware/controller/layout.h — the firmware copy is the one that counts.
// Display layout (display software rev 1: landscape), one self-contained block per DISPLAY_VARIANT.
// Tune live with tests/11_ui_dev; its 'p' command prints a replacement block for the variant it runs on.
//
// LAYOUT_INIT_W/H are the panel's native portrait size for init(); DISP_ROTATION (config.h, 1 or 3)
// turns it to LAYOUT_W x LAYOUT_H. Row r sits at y = LAYOUT_TOP + r * LAYOUT_ROW_H.
// LAYOUT_FIELDS: one {row, labelX, valueX, widthChars} per field, in this order:
//   mode, heartbeat, T1, T2, Tset, Tout, dTpot, Room, DP, P2, V1, V2, message
// The heartbeat is always right-aligned on its row (its x values are ignored).

#if DISPLAY_VARIANT == 1   // MSP1691 1.69", landscape 280x240
#define LAYOUT_INIT_W     240
#define LAYOUT_INIT_H     280
#define LAYOUT_W          280
#define LAYOUT_H          240
#define LAYOUT_TOP        6
#define LAYOUT_ROW_H      32
#define LAYOUT_LABEL_DY   5
#define LAYOUT_VALUE_SIZE 2
#define LAYOUT_FIELDS { \
  {0,   4,   4, 12}, {0, 0, 0, 8},                                                   \
  {1,   4,  44,  7}, {2,   4,  44,  7}, {3,   4,  44,  7}, {4,   4,  44,  7}, {5,   4,  44,  7}, \
  {1, 144, 180,  8}, {2, 144, 180,  8}, {3, 144, 180,  8}, {4, 144, 180,  8}, {5, 144, 180,  8}, \
  {6,   4,   4, 22} }

#elif DISPLAY_VARIANT == 2 // MSP1901 1.9", landscape 320x170
#define LAYOUT_INIT_W     170
#define LAYOUT_INIT_H     320
#define LAYOUT_W          320
#define LAYOUT_H          170
#define LAYOUT_TOP        4
#define LAYOUT_ROW_H      23
#define LAYOUT_LABEL_DY   5
#define LAYOUT_VALUE_SIZE 2
#define LAYOUT_FIELDS { \
  {0,   4,   4, 12}, {0, 0, 0, 8},                                                   \
  {1,   4,  44,  8}, {2,   4,  44,  8}, {3,   4,  44,  8}, {4,   4,  44,  8}, {5,   4,  44,  8}, \
  {1, 164, 204,  9}, {2, 164, 204,  9}, {3, 164, 204,  9}, {4, 164, 204,  9}, {5, 164, 204,  9}, \
  {6,   4,   4, 26} }

#else
#error "DISPLAY_VARIANT must be 1 or 2"
#endif
