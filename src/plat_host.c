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
 *     shot FILE     write the screen to FILE (.ppm)
 *     quit          stop (also at the end of the script)
 * FILES_DIR (default ./host_files) stands in for the calculator's storage memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include "plat.h"
#include "view.h"

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

static const struct { const char *name; int code; } names[] = {
  {"ENTER", 13}, {"SWAP", 14}, {"CHS", 15}, {"EEX", 16}, {"BSP", 17},
  {"XEQ", 18}, {"DIV", 22}, {"UP", 23}, {"MUL", 27}, {"DOWN", 28}, {"SUB", 32},
  {"EXIT", 33}, {"DOT", 35}, {"RS", 36}, {"ADD", 37},
  {"STO", 7}, {"RCL", 8}, {"RDN", 9}, {"DRG", 10}, {"SHIFT_F", 11}, {"SHIFT_G", 12},
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


void plat_present(void) {
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
