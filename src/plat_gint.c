// SPDX-License-Identifier: GPL-3.0-only
/*
 * The calculator side of plat.h, on gint.
 *
 * Keys: the CG50's keys are mapped to the R47's key positions (1..43, numbered
 * row by row; see docs/KEYS.md). MENU goes to the OS's main menu, SHIFT+AC/ON switches the
 * calculator off (both after C47 has saved its state).
 * Time: a 1 kHz timer counts milliseconds and wakes the CPU from sleep().
 * Screen: gint's single VRAM; plat_present sends only the rows that changed, by DMA.
 * Files: gint's POSIX calls on the storage memory, each inside gint_world_switch(), since they
 * use the OS's file system (BFile).
 */
#include <gint/gint.h>
#include <gint/display.h>
#include <gint/drivers/r61524.h>
#include <gint/dma.h>
#include <gint/keyboard.h>
#include <gint/timer.h>
#include <gint/rtc.h>
#include <gint/cpu.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include "plat.h"
#include "view.h"

static volatile uint32_t milliseconds = 0;

static int tick(void) {
  milliseconds++;
  return TIMER_CONTINUE;
}


// gint key code -> DMCP key code, 0 = not mapped
static uint8_t keymap[256];

// The table below names R47 key positions (1..37 row by row, F1..F6 = 38..43). The R47
// hardware sends DMCP's (DM42) codes, which C47 turns back into positions (convertKeyCode in
// upstream src/c47/c47.c): six keys differ, so they are sent as the code that becomes them.
static int codeOfPosition(int pos) {
  switch(pos) {
    case 11: return 28;   // f
    case 18: return 11;   // XEQ
    case 23: return 18;   // up
    case 28: return 23;   // down
    case 15: return 16;   // CHS
    case 16: return 15;   // EEX
    default: return pos;
  }
}

static void mapKeys(void) {
  static const uint8_t pairs[][2] = {
    // F1..F6 -> the six softkeys
    {KEY_F1, 38}, {KEY_F2, 39}, {KEY_F3, 40}, {KEY_F4, 41}, {KEY_F5, 42}, {KEY_F6, 43},
    // X,θ,T log ln sin cos tan -> the R47's top row (keys 1..6)
    {KEY_XOT, 1}, {KEY_LOG, 2}, {KEY_LN, 3}, {KEY_SIN, 4}, {KEY_COS, 5}, {KEY_TAN, 6},
    // a b/c, F<>D, (, ), ',', -> -> STO RCL R-down DRG f g (7..12)
    {KEY_FRAC, 7}, {KEY_FD, 8}, {KEY_LEFTP, 9}, {KEY_RIGHTP, 10}, {KEY_COMMA, 11}, {KEY_ARROW, 12},
    // SHIFT and ALPHA are f and g as well
    {KEY_SHIFT, 11}, {KEY_ALPHA, 12},
    // EXE ENTER, x^2 x<>y, (-) CHS, x10^x EEX, DEL backspace
    {KEY_EXE, 13}, {KEY_SQUARE, 14}, {KEY_NEG, 15}, {KEY_EXP, 16}, {KEY_DEL, 17},
    // OPTN XEQ, up/down arrows, EXIT and AC/ON EXIT, VARS R/S, ^ y^x (top row key 4)
    {KEY_OPTN, 18}, {KEY_UP, 23}, {KEY_DOWN, 28}, {KEY_EXIT, 33}, {KEY_ACON, 33}, {KEY_VARS, 36},
    {KEY_POWER, 4},
    // digits and operators
    {KEY_7, 19}, {KEY_8, 20}, {KEY_9, 21}, {KEY_DIV, 22},
    {KEY_4, 24}, {KEY_5, 25}, {KEY_6, 26}, {KEY_MUL, 27},
    {KEY_1, 29}, {KEY_2, 30}, {KEY_3, 31}, {KEY_SUB, 32},
    {KEY_0, 34}, {KEY_DOT, 35}, {KEY_ADD, 37},
  };
  for(unsigned i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
    keymap[pairs[i][0]] = codeOfPosition(pairs[i][1]);
  }
}


void plat_init(void) {
  mapKeys();
  int t = timer_configure(TIMER_ANY, 1000, GINT_CALL(tick));
  if(t >= 0) {
    timer_start(t);
  }
  dclear(C_WHITE);
}


// The DMA sends the changed rows while the CPU goes on (R61524_DMA, as gint's own dupdate();
// R61524_DMA_WAIT polls with the DMA's interrupt off, which our emulator doesn't complete);
// the next drawing waits for it to finish reading the VRAM.
uint16_t *plat_framebuffer(void) {
  dma_transfer_wait(0);
  return gint_vram;
}


// The DMA moves 32-byte units and needs its source on a 32-byte boundary; a row is 792 bytes,
// so a band must start and end on a multiple of 4 rows (otherwise the DMA flags an address
// error, transfers nothing, and the next wait for it never returns).
void plat_present(int y, int height) {
  int top = y & ~3;
  int bottom = (y + height + 3) & ~3;
  if(bottom > DHEIGHT) {
    bottom = DHEIGHT;
  }
  r61524_display(gint_vram, top, bottom - top, R61524_DMA);
}


uint32_t plat_ms(void) {
  return milliseconds;
}


// DMCP sends one release (0) when the keys are let go; a press while another key is held
// is a new press. The release is decided from gint's key state (is any mapped key still
// down?), not by counting presses and releases: a key already down when the add-in starts
// (the EXE that launched it) has a release without a press.
static bool anyMappedKeyDown(void) {
  for(int k = 0; k < 256; k++) {
    if(keymap[k] && keydown(k)) {
      return true;
    }
  }
  return false;
}

static bool pressSent = false;

int plat_next_key(void) {
  for(;;) {
    key_event_t ev = pollevent();
    if(ev.type == KEYEV_NONE) {
      return -1;
    }
    if(ev.type == KEYEV_DOWN) {
      if(ev.key == KEY_MENU) {
        return PLAT_KEY_MENU;
      }
      if(ev.key == KEY_ACON && keydown(KEY_SHIFT)) {
        return PLAT_KEY_POWEROFF;
      }
      int k = keymap[ev.key & 0xff];
      if(k) {
        pressSent = true;
        return k;
      }
    }
    else if(ev.type == KEYEV_UP) {
      if(pressSent && keymap[ev.key & 0xff] && !anyMappedKeyDown()) {
        pressSent = false;
        return 0;
      }
    }
  }
}


void plat_wait(uint32_t ms) {
  uint32_t start = milliseconds;
  // sleep() returns at the next interrupt: the 1 ms tick or the keyboard
  while(milliseconds - start < ms) {
    sleep();
    if(milliseconds - start >= 1) {
      return;           // let the caller look at the keyboard again
    }
  }
}


void plat_os_menu(void) {
  gint_osmenu();
  pressSent = false;
  clearevents();
}


void plat_power_off(void) {
  gint_poweroff(true);
  pressSent = false;
  clearevents();
}


void plat_rtc_read(plat_time_t *t) {
  rtc_time_t r;
  rtc_get_time(&r);
  t->year = r.year;
  t->month = r.month + 1;
  t->day = r.month_day;
  t->hour = r.hours;
  t->minute = r.minutes;
  t->second = r.seconds;
  t->centisecond = r.ticks * 100 / 128;
  t->weekday = (r.week_day + 6) % 7;       // gint 0 = Sunday, DMCP 0 = Monday
}


void plat_rtc_write(const plat_time_t *t) {
  rtc_time_t r;
  rtc_get_time(&r);
  r.year = (uint16_t)t->year;
  r.month = (uint8_t)(t->month - 1);
  r.month_day = (uint8_t)t->day;
  r.hours = (uint8_t)t->hour;
  r.minutes = (uint8_t)t->minute;
  r.seconds = (uint8_t)t->second;
  rtc_set_time(&r);
}


const char *plat_files_root(void) {
  return "/C47";
}


// ---- files: one world switch per call ----

static int os_open(const char *path, int flags) { return open(path, flags, 0644); }
static int os_read(int fd, void *buf, int n) { return (int)read(fd, buf, (size_t)n); }
static int os_write(int fd, const void *buf, int n) { return (int)write(fd, buf, (size_t)n); }
static int os_seek(int fd, int offset, int whence) { return (int)lseek(fd, offset, whence); }
static int os_close(int fd) { return close(fd); }
static int os_unlink(const char *path) { return unlink(path); }
static int os_rename(const char *from, const char *to) { return rename(from, to); }
static int os_mkdir(const char *path) { return mkdir(path, 0755); }

static int os_size(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 ? (int)st.st_size : -1;
}

int plat_fs_open(const char *path, int flags) { return gint_world_switch(GINT_CALL(os_open, path, flags)); }
int plat_fs_read(int fd, void *buf, int n) { return gint_world_switch(GINT_CALL(os_read, fd, buf, n)); }
int plat_fs_write(int fd, const void *buf, int n) { return gint_world_switch(GINT_CALL(os_write, fd, (void *)buf, n)); }
int plat_fs_seek(int fd, int offset, int whence) { return gint_world_switch(GINT_CALL(os_seek, fd, offset, whence)); }
int plat_fs_close(int fd) { return gint_world_switch(GINT_CALL(os_close, fd)); }
int plat_fs_unlink(const char *path) { return gint_world_switch(GINT_CALL(os_unlink, path)); }
int plat_fs_rename(const char *from, const char *to) { return gint_world_switch(GINT_CALL(os_rename, from, to)); }
int plat_fs_mkdir(const char *path) { return gint_world_switch(GINT_CALL(os_mkdir, path)); }
int plat_fs_size(const char *path) { return gint_world_switch(GINT_CALL(os_size, path)); }


int main(void) {
  plat_init();
  plat_fs_mkdir(plat_files_root());
  view_init();
  dmcp_init();
  program_main();
  return 1;
}
