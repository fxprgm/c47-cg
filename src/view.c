// SPDX-License-Identifier: GPL-3.0-only
/*
 * C47's 400x240 one-bit screen -> the CG50's 396x224 RGB565 screen.
 *
 * Columns: C47's x = 4..399. Numbers are right-aligned to x = 399, so the four columns go
 * on the left, where the first softkey loses its left edge.
 * Rows: until C47's layout is patched for 224 rows (stack lines 36 -> 32 px), four rows are
 * left out at the bottom of each of the four stack lines (C47 rows 56-59, 92-95, 128-131,
 * 164-167), which keeps the status bar and the three softkey rows whole. The digits of a stack
 * line are 26-28 px high and sit at its top, so this mostly drops blank space.
 *
 * Shared by the calculator and the PC test build (plat_present shows the frame).
 */
#include <stdint.h>
#include <string.h>
#include "plat.h"
#include "view.h"

#define SRC_W 400
#define SRC_H 240

static int8_t changed[SRC_H];
static int anyChanged = 0;
static int16_t dstRow[SRC_H];       // CG row of a C47 row, -1 = not shown

uint16_t view_paper = 0xE73B;       // light grey-green, #E0E4D8
uint16_t view_ink   = 0x1082;       // near black, #101010


static int skipped(int row) {
  return (row >= 56 && row < 60) || (row >= 92 && row < 96) || (row >= 128 && row < 132) || (row >= 164 && row < 168);
}


void view_init(void) {
  int d = 0;
  for(int r = 0; r < SRC_H; r++) {
    dstRow[r] = skipped(r) ? -1 : d++;
  }
  // d == VIEW_H
  memset(changed, 1, sizeof(changed));
  anyChanged = 1;
}


void plat_row_changed(int row) {
  if(row >= 0 && row < SRC_H) {
    changed[row] = 1;
    anyChanged = 1;
  }
}


void view_all_changed(void) {
  memset(changed, 1, sizeof(changed));
  anyChanged = 1;
}


void plat_flush(void) {
  if(!anyChanged) {
    return;
  }
  uint16_t *fb = plat_framebuffer();
  const uint16_t paper = view_paper, ink = view_ink;
  int top = VIEW_H, bottom = -1;
  for(int r = 0; r < SRC_H; r++) {
    if(!changed[r]) {
      continue;
    }
    changed[r] = 0;
    if(dstRow[r] < 0) {
      continue;
    }
    if(dstRow[r] < top) {
      top = dstRow[r];
    }
    if(dstRow[r] > bottom) {
      bottom = dstRow[r];
    }
    const uint8_t *line = dmcp_shown_line(r);
    uint16_t *out = fb + dstRow[r] * VIEW_W;
    // C47 x = 4..399; pixel x is bit (399-x)&7 of byte (399-x)>>3, 0 = black
    for(int x = 4; x < 400; x++) {
      int b = 399 - x;
      *out++ = (line[b >> 3] >> (b & 7) & 1) ? paper : ink;
    }
  }
  anyChanged = 0;
  if(bottom >= top) {
    plat_present(top, bottom - top + 1);
  }
}
