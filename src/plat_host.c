// SPDX-License-Identifier: GPL-3.0-only
/*
 * The PC test build: runs C47 with the same DMCP layer as the add-in, driven by a script, on a
 * simulated clock (so a run is repeatable), and writes the CG50's 396x224 view as PPM images.
 *
 *     c47cg_host SCRIPT [FILES_DIR]
 *
 * SCRIPT, one command per line ('#' comments):
 *     key K...      press and release each key, 80 ms apart (K = DMCP code 1..43 or a name
 *                   from tools/keys.txt-like names below, e.g. 7, ENTER, F1, SHIFT_F)
 *     wait MS       let MS milliseconds pass
 *     shot FILE     write the screen to FILE (.ppm); warns if C47 drew outside the 396x224 shown
 *     shotfull FILE write C47's whole 400x240 buffer to FILE (.ppm), what the CG50 doesn't show
 *                   (columns 396..399, rows 224..239) on a pink paper
 *     lcd FILE      write C47's whole 400x240 buffer to FILE as it is, 1 bit a pixel (.pbm, black
 *                   = set): the R47's own screen, for documentation (fxprgm/c47-manual)
 *     menu          press MENU (C47 saves its state, "goes to the main menu" and comes back)
 *     off           press SHIFT+AC/ON (the same, "switching off")
 *     quit          stop (also at the end of the script)
 * FILES_DIR (default ./host_files) stands in for the calculator's storage memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include "plat.h"
#include "view.h"

#define SRC_ROWS 240                   // C47's lcd buffer (DMCP's 400x240)

static uint16_t frame[VIEW_W * VIEW_H];
static uint32_t now = 0;               // simulated ms
static FILE *script;
static const char *filesRoot = "host_files";

#define QUEUE 256
static int queue[QUEUE];
static uint32_t queueAt[QUEUE];
static int qHead = 0, qTail = 0;
static uint32_t scriptResume = 0;      // the script continues at this time
static uint32_t idleSince = 0;

// The DMCP key codes are those of the DM42's keyboard; C47's R47 build moves six of them to
// the R47's keys (convertKeyCode in upstream src/c47/c47.c): XEQ 11, EEX 15, CHS 16, UP 18,
// DOWN 23, f 28 (the DM42's COS, CHS, E, UP, DOWN, SHIFT positions).
static const struct { const char *name; int code; } names[] = {
  {"ENTER", 13}, {"SWAP", 14}, {"CHS", 16}, {"EEX", 15}, {"BSP", 17},
  {"XEQ", 11}, {"DIV", 22}, {"UP", 18}, {"MUL", 27}, {"DOWN", 23}, {"SUB", 32},
  {"EXIT", 33}, {"DOT", 35}, {"RS", 36}, {"ADD", 37},
  {"X2", 1}, {"SQRT", 2}, {"INV", 3}, {"YX", 4}, {"LOG", 5}, {"LN", 6},
  {"STO", 7}, {"RCL", 8}, {"RDN", 9}, {"DRG", 10}, {"SHIFT_F", 28}, {"SHIFT_G", 12},
  {"F1", 38}, {"F2", 39}, {"F3", 40}, {"F4", 41}, {"F5", 42}, {"F6", 43},
  {"0", 34}, {"1", 29}, {"2", 30}, {"3", 31}, {"4", 24}, {"5", 25}, {"6", 26},
  {"7", 19}, {"8", 20}, {"9", 21},
};


static int keyCode(const char *s) {
  for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    if(strcmp(s, names[i].name) == 0) {
      return names[i].code;
    }
  }
  if(s[0] == 'k' && isdigit((unsigned char)s[1])) {   // k13 = raw DMCP code 13
    return atoi(s + 1);
  }
  fprintf(stderr, "script: unknown key '%s'\n", s);
  exit(2);
}


static void enqueue(int code, uint32_t at) {
  queue[qTail % QUEUE] = code;
  queueAt[qTail % QUEUE] = at;
  qTail++;
}


static void writeShot(const char *path) {
  plat_flush();
  FILE *f = fopen(path, "wb");
  if(!f) {
    perror(path);
    exit(2);
  }
  fprintf(f, "P6\n%d %d\n255\n", VIEW_W, VIEW_H);
  for(int i = 0; i < VIEW_W * VIEW_H; i++) {
    uint16_t c = frame[i];
    unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  fprintf(stderr, "[%6u ms] shot %s\n", now, path);
  // C47 is laid out for 396x224 (patches/0001-layout-396x224.patch); its buffer keeps 400x240.
  int first = -1, last = -1, right = 0;
  for(int r = 0; r < SRC_ROWS; r++) {
    const uint8_t *line = dmcp_shown_line(r);
    int ink = 0;
    for(int x = r < VIEW_H ? VIEW_W : 0; x < 400; x++) {
      int b = 399 - x;
      if(!(line[b >> 3] >> (b & 7) & 1)) {
        ink = 1;
        right |= r < VIEW_H;
        break;
      }
    }
    if(ink) {
      if(first < 0) {
        first = r;
      }
      last = r;
    }
  }
  if(first >= 0) {
    fprintf(stderr, "[%6u ms] WARNING %s: ink outside the view, rows %d..%d%s\n", now, path, first, last,
            right ? " (some in columns 396..399)" : "");
  }
}


static void writeFullShot(const char *path) {
  plat_flush();
  FILE *f = fopen(path, "wb");
  if(!f) {
    perror(path);
    exit(2);
  }
  fprintf(f, "P6\n400 %d\n255\n", SRC_ROWS);
  for(int r = 0; r < SRC_ROWS; r++) {
    const uint8_t *line = dmcp_shown_line(r);
    for(int x = 0; x < 400; x++) {
      int b = 399 - x;
      int white = line[b >> 3] >> (b & 7) & 1;
      unsigned char rgb[3] = {0x10, 0x10, 0x10};
      if(white) {
        int shown = r < VIEW_H && x < VIEW_W;
        rgb[0] = 0xE0; rgb[1] = shown ? 0xE4 : 0xB0; rgb[2] = shown ? 0xD8 : 0xC0;
      }
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
  fprintf(stderr, "[%6u ms] shotfull %s\n", now, path);
}


static void writeLcd(const char *path) {
  plat_flush();
  FILE *f = fopen(path, "wb");
  if(!f) {
    perror(path);
    exit(2);
  }
  fprintf(f, "P4\n400 %d\n", SRC_ROWS);
  for(int r = 0; r < SRC_ROWS; r++) {
    const uint8_t *line = dmcp_shown_line(r);
    unsigned char out[50] = {0};
    for(int x = 0; x < 400; x++) {
      int b = 399 - x;
      if(!(line[b >> 3] >> (b & 7) & 1)) {
        out[x >> 3] |= 0x80 >> (x & 7);
      }
    }
    fwrite(out, 1, sizeof(out), f);
  }
  fclose(f);
  fprintf(stderr, "[%6u ms] lcd %s\n", now, path);
}


// Run script lines until one has to wait (a key or `wait`) or the script ends.
static void stepScript(void) {
  char line[256];
  while(now >= scriptResume && qHead == qTail) {
    if(!fgets(line, sizeof(line), script)) {
      fprintf(stderr, "[%6u ms] end of script\n", now);
      exit(0);
    }
    char *p = strchr(line, '#');
    if(p) {
      *p = 0;
    }
    char *cmd = strtok(line, " \t\r\n");
    if(!cmd) {
      continue;
    }
    if(strcmp(cmd, "key") == 0) {
      uint32_t t = now;
      for(char *k = strtok(NULL, " \t\r\n"); k; k = strtok(NULL, " \t\r\n")) {
        enqueue(keyCode(k), t);
        enqueue(0, t + 80);
        t += 160;
      }
      scriptResume = t;
    }
    else if(strcmp(cmd, "wait") == 0) {
      scriptResume = now + (uint32_t)atoi(strtok(NULL, " \t\r\n"));
    }
    else if(strcmp(cmd, "shot") == 0) {
      writeShot(strtok(NULL, " \t\r\n"));
    }
    else if(strcmp(cmd, "menu") == 0 || strcmp(cmd, "off") == 0) {
      enqueue(cmd[0] == 'm' ? PLAT_KEY_MENU : PLAT_KEY_POWEROFF, now);
      scriptResume = now + 200;
    }
    else if(strcmp(cmd, "shotfull") == 0) {
      writeFullShot(strtok(NULL, " \t\r\n"));
    }
    else if(strcmp(cmd, "lcd") == 0) {
      writeLcd(strtok(NULL, " \t\r\n"));
    }
    else if(strcmp(cmd, "quit") == 0) {
      exit(0);
    }
    else {
      fprintf(stderr, "script: unknown command '%s'\n", cmd);
      exit(2);
    }
  }
}


void plat_init(void) {
}


uint16_t *plat_framebuffer(void) {
  return frame;
}


void plat_present(int y, int height) {
  (void)y; (void)height;
}


void plat_os_menu(void) {
  fprintf(stderr, "[%6u ms] main menu (and back)\n", now);
}


void plat_power_off(void) {
  fprintf(stderr, "[%6u ms] power off (and on)\n", now);
}


uint32_t plat_ms(void) {
  return now;
}


int plat_next_key(void) {
  if(qHead != qTail && queueAt[qHead % QUEUE] <= now) {
    return queue[qHead++ % QUEUE];
  }
  return -1;
}


void plat_wait(uint32_t ms) {
  if(ms == 0) {
    ms = 1;
  }
  stepScript();
  uint32_t until = now + ms;
  if(qHead != qTail && queueAt[qHead % QUEUE] < until) {
    until = queueAt[qHead % QUEUE] > now ? queueAt[qHead % QUEUE] : now + 1;
  }
  if(scriptResume > now && scriptResume < until) {
    until = scriptResume;
  }
  now = until;
  (void)idleSince;
}


void plat_rtc_read(plat_time_t *t) {
  // A fixed date and the simulated time, so screenshots are repeatable.
  uint32_t s = now / 1000;
  t->year = 2026; t->month = 11; t->day = 23; t->weekday = 0;
  t->hour = 12 + (int)(s / 3600) % 12;
  t->minute = (int)(s / 60) % 60;
  t->second = (int)s % 60;
  t->centisecond = (int)(now / 10) % 100;
}


void plat_rtc_write(const plat_time_t *t) {
  (void)t;
}


const char *plat_files_root(void) {
  return filesRoot;
}


int plat_fs_open(const char *path, int flags) { return open(path, flags, 0644); }
int plat_fs_read(int fd, void *buf, int n) { return (int)read(fd, buf, (size_t)n); }
int plat_fs_write(int fd, const void *buf, int n) { return (int)write(fd, buf, (size_t)n); }
int plat_fs_seek(int fd, int offset, int whence) { return (int)lseek(fd, offset, whence); }
int plat_fs_close(int fd) { return close(fd); }
int plat_fs_unlink(const char *path) { return unlink(path); }
int plat_fs_rename(const char *from, const char *to) { return rename(from, to); }
int plat_fs_mkdir(const char *path) { return mkdir(path, 0755); }

int plat_fs_size(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 ? (int)st.st_size : -1;
}


int plat_fs_list(const char *path, char (*names)[PLAT_NAME_MAX], int max) {
  DIR *d = opendir(path);
  if(!d) {
    return -1;
  }
  int n = 0;
  struct dirent *e;
  while(n < max && (e = readdir(d))) {
    if(e->d_type == DT_REG) {
      snprintf(names[n++], PLAT_NAME_MAX, "%s", e->d_name);
    }
  }
  closedir(d);
  return n;
}


int main(int argc, char **argv) {
  if(argc < 2) {
    fprintf(stderr, "usage: %s SCRIPT [FILES_DIR]\n", argv[0]);
    return 2;
  }
  script = fopen(argv[1], "r");
  if(!script) {
    perror(argv[1]);
    return 2;
  }
  if(argc > 2) {
    filesRoot = argv[2];
  }
  mkdir(filesRoot, 0755);
  setvbuf(stdout, NULL, _IONBF, 0);
  view_init();
  dmcp_init();
  program_main();
  return 0;
}
