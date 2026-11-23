// SPDX-License-Identifier: GPL-3.0-only
/*
 * The DMCP display functions C47 uses, over a platform that shows a 396x224 window of them
 * (plat.h).
 *
 * lcd_buffer is DMCP's: 240 lines of 52 bytes, [0] the line's dirty flag, [1] the line's LCD
 * address (239 - row, as upstream's simulator sets it), [2..51] the 400 pixels, pixel x at bit
 * 415 - x of the line counted from byte 0 (so x = 399 is byte 2 bit 0), 0 = black. C47 writes
 * it directly as well as through these functions, so the layout is kept exactly.
 *
 * The real DMCP display has its own memory, which LCD_write_line writes: what is on the
 * glass. `shown` is that memory here. A line is copied into it when written; the platform is
 * told which rows changed and shows them the next time the calculator waits (plat_flush,
 * from dmcp_sys.c) or a refresh function is called.
 *
 * Bit operations follow upstream's simulator (src/c47-gtk/hal/lcd.c), which C47's authors
 * keep matching the hardware.
 */
#include "c47.h"
#include "plat.h"

#define LINE_STRIDE 52

static uint8_t lcdMemory[SCREEN_HEIGHT * LINE_STRIDE + 4] __attribute__((aligned(4)));
static uint8_t shown[SCREEN_HEIGHT][LCD_LINE_SIZE] __attribute__((aligned(4)));
static int bufCleared = 0;


const uint8_t *dmcp_shown_line(int row) {
  return shown[row];
}


uint8_t *lcd_line_addr(int y) {
  if(y < 0 || y >= SCREEN_HEIGHT) {
    y = 0;
  }
  lcdMemory[LINE_STRIDE * y] = 1u;
  return lcdMemory + LINE_STRIDE * y + 2;
}


static void showRow(int row, const uint8_t *data) {
  if(row < 0 || row >= SCREEN_HEIGHT) {
    return;
  }
  memcpy(shown[row], data, LCD_LINE_SIZE);
  plat_row_changed(row);
}


void LCD_write_line(uint8_t *buf) {
  // A line of lcd_buffer knows its row by where it is; a line copied out of it (C47's overlays)
  // by its address byte.
  int row;
  if(buf >= lcdMemory && buf < lcdMemory + sizeof(lcdMemory)) {
    row = (buf - lcdMemory) / LINE_STRIDE;
  }
  else {
    row = SCREEN_HEIGHT - 1 - buf[1];
  }
  showRow(row, buf + 2);
  buf[0] = 0u;
}


void lcd_clear_buf(void) {
  for(int row = 0; row < SCREEN_HEIGHT; row++) {
    uint8_t *line = lcdMemory + LINE_STRIDE * row;
    memset(line + 2, 0xFF, LCD_LINE_SIZE);
    line[1] = SCREEN_HEIGHT - 1 - row;
    line[0] = 1u;
  }
}


void lcd_refresh(void) {
  for(int row = 0; row < SCREEN_HEIGHT; row++) {
    if(lcdMemory[LINE_STRIDE * row]) {
      LCD_write_line(lcdMemory + LINE_STRIDE * row);
    }
  }
  plat_flush();
}


void lcd_refresh_dma(void) {
  lcd_refresh();
}


void lcd_refresh_wait(void) {
  lcd_refresh();
}


void lcd_forced_refresh(void) {
  for(int row = 0; row < SCREEN_HEIGHT; row++) {
    LCD_write_line(lcdMemory + LINE_STRIDE * row);
  }
  plat_flush();
}


void lcd_refresh_lines(int ln, int cnt) {
  for(int row = ln; row < ln + cnt && row < SCREEN_HEIGHT; row++) {
    if(row >= 0) {
      LCD_write_line(lcdMemory + LINE_STRIDE * row);
    }
  }
  plat_flush();
}


void bitblt24(uint32_t x, uint32_t dx, uint32_t y, uint32_t val, int blt_op, int fill) {
  if(dx < 1 || dx > 24 || y >= SCREEN_HEIGHT) {
    return;
  }
  if(x >= SCREEN_WIDTH || x + dx > SCREEN_WIDTH) {
    return;
  }
  x = SCREEN_WIDTH - dx - x;

  const uint32_t byte_i   = x >> 3;
  const uint32_t bit_off  = x & 7u;
  const uint32_t lowmask  = (1u << dx) - 1u;
  const uint32_t bytes_needed = (bit_off + dx + 7) / 8;
  const uint32_t srcbits  = (val & lowmask) << bit_off;
  const uint32_t fillbits = (fill == BLT_SET) ? lowmask << bit_off : 0u;
  uint8_t srcbytes[4]  = {(uint8_t)srcbits,  (uint8_t)(srcbits >> 8),  (uint8_t)(srcbits >> 16),  (uint8_t)(srcbits >> 24)};
  uint8_t fillbytes[4] = {(uint8_t)fillbits, (uint8_t)(fillbits >> 8), (uint8_t)(fillbits >> 16), (uint8_t)(fillbits >> 24)};
  uint8_t *j = &lcdMemory[y * LINE_STRIDE + byte_i + 2];

  switch(blt_op) {
    case BLT_OR:
      for(uint32_t i = 0; i < bytes_needed; i++) {
        j[i] = (j[i] | fillbytes[i]) & ~srcbytes[i];
      }
      break;
    case BLT_XOR:
      for(uint32_t i = 0; i < bytes_needed; i++) {
        j[i] ^= srcbytes[i];
      }
      break;
    case BLT_ANDN:
      for(uint32_t i = 0; i < bytes_needed; i++) {
        j[i] = (j[i] & ~fillbytes[i]) | srcbytes[i];
      }
      break;
    default:
      return;
  }
  lcdMemory[y * LINE_STRIDE] = 1u;
}


void lcd_fill_rect(uint32_t x, uint32_t y, uint32_t dx, uint32_t dy, int val) {
  uint32_t endX = x + dx, endY = y + dy;
  if(endX > SCREEN_WIDTH || endY > SCREEN_HEIGHT) {
    return;
  }
  int blt_op = val ? BLT_OR : BLT_ANDN;
  for(uint32_t col = x; col < endX; col += 24) {
    uint32_t cols = endX - col < 24 ? endX - col : 24;
    for(uint32_t line = y; line < endY; line++) {
      bitblt24(col, cols, line, 0xFFFFFF, blt_op, BLT_NONE);
    }
  }
}


void lcd_set_buf_cleared(int val) {
  bufCleared = val;
}


int lcd_get_buf_cleared(void) {
  return bufCleared;
}


void LCD_power_on(void) {
}


void LCD_power_off(int clear) {
  (void)clear;
}


void draw_power_off_image(int allow_errors) {
  (void)allow_errors;
}


uint8_t reverse_byte(uint8_t x) {
  x = (uint8_t)((x & 0xF0) >> 4 | (x & 0x0F) << 4);
  x = (uint8_t)((x & 0xCC) >> 2 | (x & 0x33) << 2);
  return (uint8_t)((x & 0xAA) >> 1 | (x & 0x55) << 1);
}
