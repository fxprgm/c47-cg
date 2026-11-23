// SPDX-License-Identifier: GPL-3.0-only
/*
 * The DMCP system functions C47 uses: the key buffer, sleep and timers, the run-state flags
 * (calc_state), the real-time clock, and the rest, over plat.h.
 *
 * DMCP's run loop protocol, as C47's mainLoop (upstream src/c47/c47.c) relies on it:
 *   - sys_sleep() returns when a key is in the buffer, timer 0 has expired, or (with
 *     STAT_CLK_WKUP_ENABLE) the minute changed, which sets STAT_CLK_WKUP_FLAG;
 *   - the auto-off countdown (reset_auto_off) sets STAT_PGM_END when it runs out;
 *   - once C47 has gone off (STAT_OFF), sys_sleep waits for the ON key (EXIT) and clears
 *     STAT_PGM_END, which C47 never clears itself.
 */
#include "c47.h"
#include "plat.h"
#include "view.h"

sys_sdb_t c47cg_sdb;

#define KEY_BUFFER_SIZE 64
static int keyBuffer[KEY_BUFFER_SIZE];
static int keyCount = 0;
static int lastKey = -1;
static int osRequest = 0;                // PLAT_KEY_MENU / PLAT_KEY_POWEROFF, done at the next sys_sleep
static bool_t keyDown = false;

#define AUTO_OFF_MS (10u * 60u * 1000u)
static uint32_t autoOffStart;

#define TIMERS 4
static bool_t timerOn[TIMERS];
static uint32_t timerStart[TIMERS], timerLength[TIMERS];

static int lastMinute = -1;
static int dmyFlag = 0, clk24Flag = 1, beepVolume = 8;
static char resetStateFile[64];

static char auxBuffer[AUX_BUF_SIZE] __attribute__((aligned(8)));
#define WRITE_BUF_SIZE 4096
static uint8_t writeBuffer[WRITE_BUF_SIZE] __attribute__((aligned(8)));

static disp_stat_t ds_t20, ds_t24, ds_fReg;
static FIL pgmFile;                     // DMCP's ppgm_fp: the one file C47's io.c opens


// ---- the key buffer ----

static void pushKey(int k) {
  if(keyCount < KEY_BUFFER_SIZE) {
    keyBuffer[keyCount++] = k;
  }
  if(k > 0) {
    lastKey = k;
  }
  keyDown = (k > 0);
  autoOffStart = plat_ms();
}


// Move what the platform has into the buffer.
static void pollKeys(void) {
  int k;
  while((k = plat_next_key()) >= 0) {
    if(k == PLAT_KEY_MENU || k == PLAT_KEY_POWEROFF) {
      osRequest = k;
    }
    else {
      pushKey(k);
    }
  }
}


int key_empty(void) {
  pollKeys();
  return keyCount == 0;
}


int key_push(int k1) {
  pushKey(k1);
  return keyCount;
}


int key_tail(void) {
  pollKeys();
  return keyCount ? keyBuffer[keyCount - 1] : -1;
}


int key_pop(void) {
  pollKeys();
  if(keyCount == 0) {
    return -1;
  }
  int k = keyBuffer[0];
  memmove(keyBuffer, keyBuffer + 1, (size_t)(keyCount - 1) * sizeof(int));
  keyCount--;
  return k;
}


int key_pop_last(void) {
  pollKeys();
  if(keyCount == 0) {
    return -1;
  }
  return keyBuffer[--keyCount];
}


void key_pop_all(void) {
  pollKeys();
  keyCount = 0;
}


int key_to_nr(int key) {
  return key;
}


int sys_last_key(void) {
  return lastKey;
}


// ---- time ----

uint32_t sys_current_ms(void) {
  return plat_ms();
}


uint32_t sys_tick_count(void) {
  return plat_ms();
}


void sys_timer_start(int timer_ix, uint32_t ms_value) {
  if(timer_ix >= 0 && timer_ix < TIMERS) {
    timerOn[timer_ix] = true;
    timerStart[timer_ix] = plat_ms();
    timerLength[timer_ix] = ms_value;
  }
}


void sys_timer_disable(int timer_ix) {
  if(timer_ix >= 0 && timer_ix < TIMERS) {
    timerOn[timer_ix] = false;
  }
}


int sys_timer_active(int timer_ix) {
  return timer_ix >= 0 && timer_ix < TIMERS && timerOn[timer_ix];
}


int sys_timer_timeout(int timer_ix) {
  return sys_timer_active(timer_ix) && plat_ms() - timerStart[timer_ix] >= timerLength[timer_ix];
}


// Milliseconds until the first running timer expires (0 = now), or `limit`.
static uint32_t untilTimer(uint32_t limit) {
  uint32_t now = plat_ms();
  for(int i = 0; i < TIMERS; i++) {
    if(timerOn[i]) {
      uint32_t gone = now - timerStart[i];
      uint32_t left = gone >= timerLength[i] ? 0 : timerLength[i] - gone;
      if(left < limit) {
        limit = left;
      }
    }
  }
  return limit;
}


void sys_delay(uint32_t ms_delay) {
  plat_flush();
  uint32_t start = plat_ms();
  while(plat_ms() - start < ms_delay) {
    plat_wait(ms_delay - (plat_ms() - start));
    pollKeys();
  }
}


static bool_t minuteChanged(void) {
  plat_time_t t;
  plat_rtc_read(&t);
  if(t.minute != lastMinute) {
    bool_t first = (lastMinute < 0);
    lastMinute = t.minute;
    return !first;
  }
  return false;
}


// After fnSaveAuto and a trip to the OS: the save wrote "Saving state file ..." over the
// status bar and set a full redraw (screenUpdatingMode = SCRUPD_AUTO) that C47 would only do
// at the next key; do it now, and show the whole screen again over what the OS drew.
static void redrawAfterSave(void) {
  refreshScreen(0);
  lcd_forced_refresh();
  view_all_changed();
  plat_flush();
}


// Leave for the OS (main menu or power off) from the run loop, where C47's state is
// consistent: save it first, since from the main menu the user may start another add-in and
// this one would start afresh.
static void doOsRequest(void) {
  int request = osRequest;
  osRequest = 0;
  fnSaveAuto(0);
  if(request == PLAT_KEY_MENU) {
    plat_os_menu();
  }
  else {
    plat_power_off();
  }
  key_pop_all();
  lastMinute = -1;
  autoOffStart = plat_ms();
  redrawAfterSave();
}


void sys_sleep(void) {
  plat_flush();

  if(ST(STAT_OFF)) {
    // C47 switched off (OFF, or auto-off): switch the calculator off. It comes back on here.
    fnSaveAuto(0);
    plat_power_off();
    key_pop_all();
    redrawAfterSave();
    CLR_ST(STAT_PGM_END);
    autoOffStart = plat_ms();
    lastMinute = -1;
    return;
  }

  for(;;) {
    pollKeys();
    if(osRequest) {
      doOsRequest();
    }
    if(keyCount) {
      return;
    }
    if(untilTimer(1) == 0 && sys_timer_active(0)) {
      return;
    }
    if(ST(STAT_CLK_WKUP_ENABLE) && minuteChanged()) {
      SET_ST(STAT_CLK_WKUP_FLAG);
      return;
    }
    if(plat_ms() - autoOffStart >= AUTO_OFF_MS && !ST(STAT_PGM_END)) {
      SET_ST(STAT_PGM_END);
      return;
    }
    plat_wait(untilTimer(100));
  }
}


void reset_auto_off(void) {
  autoOffStart = plat_ms();
}


int is_auto_off(void) {
  return ST(STAT_PGM_END) != 0;
}


int is_menu_auto_off(void) {
  return 0;
}


int sys_auto_off_cnt(void) {
  return (int)((AUTO_OFF_MS - (plat_ms() - autoOffStart)) / 1000u);
}


// ---- waiting for keys outside the main loop ----

void wait_for_key_press(void) {
  for(;;) {
    int k = key_pop();
    if(k > 0) {
      return;
    }
    if(k < 0) {
      plat_flush();
      plat_wait(100);
    }
  }
}


void wait_for_key_release(int tout) {
  uint32_t start = plat_ms();
  plat_flush();
  for(;;) {
    pollKeys();
    if(!keyDown) {
      return;
    }
    if(tout >= 0 && plat_ms() - start >= (uint32_t)tout) {
      return;
    }
    plat_wait(20);
  }
}


int runner_get_key(int *repeat) {
  if(repeat) {
    *repeat = 0;
  }
  for(;;) {
    int k = key_pop();
    if(k >= 0) {
      return k;
    }
    plat_flush();
    plat_wait(100);
  }
}


int runner_get_key_delay(int *repeat, uint timeout, uint rep0, uint rep1, uint rep1tout) {
  (void)rep0; (void)rep1; (void)rep1tout;
  if(repeat) {
    *repeat = 0;
  }
  uint32_t start = plat_ms();
  for(;;) {
    int k = key_pop();
    if(k >= 0) {
      return k;
    }
    if(plat_ms() - start >= timeout) {
      return -1;
    }
    plat_flush();
    plat_wait(20);
  }
}


// ---- clock ----

void rtc_read(tm_t *tm, dt_t *dt) {
  plat_time_t t;
  plat_rtc_read(&t);
  if(tm) {
    tm->hour = (uint8_t)t.hour;
    tm->min = (uint8_t)t.minute;
    tm->sec = (uint8_t)t.second;
    tm->csec = (uint8_t)t.centisecond;
    tm->dow = (uint8_t)t.weekday;
  }
  if(dt) {
    dt->year = (uint16_t)t.year;
    dt->month = (uint8_t)t.month;
    dt->day = (uint8_t)t.day;
  }
}


void rtc_write(tm_t *tm, dt_t *dt) {
  plat_time_t t;
  plat_rtc_read(&t);
  if(tm) {
    t.hour = tm->hour;
    t.minute = tm->min;
    t.second = tm->sec;
    t.centisecond = tm->csec;
  }
  if(dt) {
    t.year = dt->year;
    t.month = dt->month;
    t.day = dt->day;
  }
  plat_rtc_write(&t);
  lastMinute = -1;
}


uint8_t rtc_read_min(void) {
  plat_time_t t;
  plat_rtc_read(&t);
  return (uint8_t)t.minute;
}


uint8_t rtc_read_sec(void) {
  plat_time_t t;
  plat_rtc_read(&t);
  return (uint8_t)t.second;
}


void rtc_wakeup_delay(void) {
}


static int getDmy(void) { return dmyFlag; }
static void setDmy(int val) { dmyFlag = val; }
static int getClk24(void) { return clk24Flag; }
static void setClk24(int val) { clk24Flag = val; }


// ---- power, sound, printer: the CG50 has no buzzer, IR printer or battery voltage reading ----

int get_vbat(void) {
  return 3300;
}


int get_lowbat_state(void) {
  return 0;
}


int usb_powered(void) {
  return 0;
}


void start_buzzer_freq(uint32_t freq) {
  (void)freq;
}


void stop_buzzer(void) {
}


int get_beep_volume(void) {
  return beepVolume;
}


void beep_volume_up(void) {
  if(beepVolume < 11) {
    beepVolume++;
  }
}


void beep_volume_down(void) {
  if(beepVolume > 0) {
    beepVolume--;
  }
}


void print_byte(uint8_t b) {
  (void)b;
}


static uint printerDelay = 1800;

uint printer_get_delay(void) {
  return printerDelay;
}


void printer_set_delay(uint val) {
  printerDelay = val;
}


void printer_advance_buf(int what) {
  (void)what;
}


int printer_busy_for(int what) {
  (void)what;
  return 0;
}


// ---- memory and misc ----

char *aux_buf_ptr(void) {
  return auxBuffer;
}


void *write_buf_ptr(void) {
  return writeBuffer;
}


int write_buf_size(void) {
  return WRITE_BUF_SIZE;
}


int sys_free_mem(void) {
  return 0;
}


void sys_reset(void) {
  exit(1);
}


int run_menu_item_sys(uint8_t line_id) {
  (void)line_id;
  return 0;
}


void set_reset_state_file(const char *str) {
  strncpy(resetStateFile, str ? str : "", sizeof(resetStateFile) - 1);
}


char *get_reset_state_file(void) {
  return resetStateFile;
}


int power_check_screen(void) {
  return 0;
}


int create_screenshot(int report_error) {
  (void)report_error;
  return 0;
}


void make_date_filename(char *str, const char *dir, const char *ext) {
  plat_time_t t;
  plat_rtc_read(&t);
  sprintf(str, "%s/%04d%02d%02d_%02d%02d%02d%s", dir, t.year, t.month, t.day, t.hour, t.minute, t.second, ext);
}


void dmcp_init(void) {
  memset(&c47cg_sdb, 0, sizeof(c47cg_sdb));
  get_flag_dmy = getDmy;
  set_flag_dmy = setDmy;
  is_flag_clk24 = getClk24;
  set_flag_clk24 = setClk24;
  ppgm_fp = &pgmFile;
  c47cg_sdb.pds_t20 = &ds_t20;
  c47cg_sdb.pds_t24 = &ds_t24;
  c47cg_sdb.pds_fReg = &ds_fReg;
  SET_ST(STAT_CLK24);
  autoOffStart = plat_ms();
}
