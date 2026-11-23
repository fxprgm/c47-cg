// SPDX-License-Identifier: GPL-3.0-only
/*
 * C47's 400x240 one-bit screen -> the CG50's 396x224 RGB565 screen.
 *
 * C47 is patched to lay out for 396x224 (patches/0001-layout-396x224.patch: SCREEN_WIDTH 396,
 * SCREEN_HEIGHT 224, stack lines 32 px instead of 36) while its lcd buffer stays DMCP's
 * 400x240, so this is a 1:1 copy of the buffer's x 0..395, rows 0..223; columns 396..399 and
 * rows 224..239 are never drawn on.
 *
 * Shared by the calculator and the PC test build (plat_present shows the frame).
 */
#include <stdint.h>
#include <string.h>
#include "plat.h"
#include "view.h"

#define SRC_H VIEW_H        // rows 0..223 of the 240; the rest are never drawn on

static int8_t changed[SRC_H];
static int anyChanged = 0;

uint16_t view_paper = 0xE73B;       // light grey-green, #E0E4D8
uint16_t view_ink   = 0x1082;       // near black, #101010


void view_init(void) {
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
  for(int r = 0; r < SRC_H; r++) {
    if(!changed[r]) {
      continue;
    }
    changed[r] = 0;
    const uint8_t *line = dmcp_shown_line(r);
    uint16_t *out = fb + r * VIEW_W;
    // x = 0..395; pixel x is bit (399-x)&7 of byte (399-x)>>3, 0 = black
    for(int x = 0; x < VIEW_W; x++) {
      int b = 399 - x;
      *out++ = (line[b >> 3] >> (b & 7) & 1) ? paper : ink;
    }
  }
  anyChanged = 0;
  plat_present();
}
