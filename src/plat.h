// SPDX-License-Identifier: GPL-3.0-only
/*
 * What the DMCP layer (src/dmcp_*.c) needs from the machine it runs on. Two implementations:
 * plat_gint.c (the calculator) and plat_host.c (a PC test build driven by a script, which
 * writes screenshots).
 */
#ifndef C47CG_PLAT_H
#define C47CG_PLAT_H

#include <stdint.h>

void plat_init(void);

// Display: row (0..239 of C47's 400x240 screen) of dmcp_shown_line() changed; plat_flush shows
// every changed row.
void plat_row_changed(int row);
void plat_flush(void);
const uint8_t *dmcp_shown_line(int row);   // dmcp_lcd.c: 50 bytes, pixel x at byte (399-x)/8 bit (399-x)%8, 0 = black

// Milliseconds since start, wrapping at 2^32.
uint32_t plat_ms(void);

// The next keyboard event as a DMCP key code (1..37 keys, 38..43 F1..F6, 0 = release),
// or -1 if none is waiting.
int plat_next_key(void);

// Wait until a keyboard event may be waiting or `ms` milliseconds have passed, whichever comes
// first. Called only when the calculator has nothing to do.
void plat_wait(uint32_t ms);

typedef struct {
  int year, month, day;      // month 1..12
  int hour, minute, second, centisecond;
  int weekday;               // 0 = Monday
} plat_time_t;

void plat_rtc_read(plat_time_t *t);
void plat_rtc_write(const plat_time_t *t);

// Folder (no trailing separator) under which the calculator's files live.
const char *plat_files_root(void);

#endif
