// SPDX-License-Identifier: GPL-3.0-only
/*
 * DMCP's own screens that C47 uses: its text output (lcd_puts and friends, for the short
 * dialogs in upstream's src/c47-dmcp5/hal/io.c: "Saving state ...", "WARNING ... Press
 * [ENTER]") and its file browser (file_selection_screen, for every manual save, load and
 * export), plus the disk information screen.
 *
 * Drawn with C47's own standard font into its screen buffer, on 22-pixel lines (10 on the
 * CG50's 224 rows; DMCP's t24 has 10 of 24 on 240).
 *
 * file_selection_screen as DMCP's: it lists the files of base_dir ending in ext; ENTER calls
 * sel_fn(path, name, data), which returns 0 to stay in the list or a code (MRET_SAVESTATE,
 * MRET_LOADSTATE) that ends it; EXIT ends it with MRET_EXIT. With disp_new the list starts
 * with a new file, whose name is typed: letters as on the R47's keys in alpha mode (C47's
 * key_to_alpha_table), f then a digit key for the digit, <- deletes. With overwrite_check,
 * choosing an existing file asks first.
 */
#include "c47.h"
#include "plat.h"
#include "dmcp_cg.h"
#include <stdarg.h>

#define LINE_H     22
#define LINES      (SCREEN_HEIGHT / LINE_H)
#define LIST_LINES (LINES - 2)       // a title line and a hint line around the list
#define MAX_FILES  128
#define NAME_MAX   MAX_PGM_FN_LEN    // 24: data is a 40-byte buffer for "DIR/name.ext"


// ---- text output ----

void lcd_setLine(disp_stat_t *ds, int ln_nr) {
  ds->x = 0;
  ds->y = (int16_t)(ln_nr * LINE_H);
}


void lcd_setXY(disp_stat_t *ds, int x, int y) {
  ds->x = (int16_t)x;
  ds->y = (int16_t)y;
}


int lcd_lineHeight(disp_stat_t *ds) {
  (void)ds;
  return LINE_H;
}


// A whole line: cleared (black when inverted), the text from the left, then the next line.
void lcd_writeText(disp_stat_t *ds, const char *text) {
  if(ds->y < 0 || ds->y + LINE_H > SCREEN_HEIGHT) {
    return;
  }
  lcd_fill_rect(0, ds->y, SCREEN_WIDTH, LINE_H, ds->inv ? 1 : 0);
  if(text && *text) {
    showString(text, &standardFont, ds->x + 4, ds->y, ds->inv ? vmReverse : vmNormal, false, false);
  }
  ds->x = 0;
  ds->y += LINE_H;
}


void lcd_print(disp_stat_t *ds, const char *fmt, ...) {
  char line[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  lcd_writeText(ds, line);
}


void lcd_writeClr(disp_stat_t *ds) {
  lcd_setLine(ds, 0);
}


// ---- helpers ----

static void textAt(int line, const char *text, bool_t inverted) {
  t24->inv = inverted;
  lcd_setLine(t24, line);
  lcd_writeText(t24, text);
  t24->inv = 0;
}


static int waitKey(void) {
  for(;;) {
    int k = runner_get_key(NULL);
    if(k > 0) {
      return k;
    }
  }
}


static bool_t endsWith(const char *s, const char *ext) {
  size_t n = strlen(s), e = strlen(ext);
  if(e > n) {
    return false;
  }
  for(size_t i = 0; i < e; i++) {
    if(tolower((unsigned char)s[n - e + i]) != tolower((unsigned char)ext[i])) {
      return false;
    }
  }
  return true;
}


static bool_t confirm(const char *title, const char *line1, const char *line2) {
  lcd_clear_buf();
  textAt(0, title, true);
  textAt(2, line1, false);
  textAt(3, line2, false);
  textAt(LINES - 1, "ENTER yes    EXIT no", false);
  lcd_refresh();
  wait_for_key_release(-1);
  for(;;) {
    int k = waitKey();
    if(k == KEY_ENTER) {
      return true;
    }
    if(IS_EXIT_KEY(k)) {
      return false;
    }
  }
}


// The digit a key gives after f in the name editor (0 = none).
static char digitOfKey(int k) {
  switch(k) {
    case KEY_7: return '7';
    case KEY_8: return '8';
    case KEY_9: return '9';
    case KEY_4: return '4';
    case KEY_5: return '5';
    case KEY_6: return '6';
    case KEY_1: return '1';
    case KEY_2: return '2';
    case KEY_3: return '3';
    case KEY_0: return '0';
    default:    return 0;
  }
}


// The name of a new file, ext added; false if cancelled.
static bool_t editName(const char *title, const char *ext, char *name) {
  size_t room = NAME_MAX - strlen(ext);
  size_t len = 0;
  bool_t shifted = false;
  char line[64];
  name[0] = 0;
  wait_for_key_release(-1);
  for(;;) {
    lcd_clear_buf();
    textAt(0, title, true);
    textAt(2, "New file name:", false);
    snprintf(line, sizeof(line), "%s_%s", name, ext);
    textAt(3, line, false);
    textAt(5, "Letters: the keys, as alpha on the R47", false);
    textAt(6, shifted ? "f: the next digit key types a digit" : "f then a digit key: a digit", false);
    textAt(LINES - 1, "ENTER ok   <- delete   EXIT cancel", false);
    lcd_refresh();

    int k = waitKey();
    if(k == KEY_ENTER) {
      if(len > 0) {
        strcat(name, ext);
        return true;
      }
    }
    else if(k == KEY_EXIT) {
      return false;
    }
    else if(k == KEY_BSP) {
      if(len > 0) {
        name[--len] = 0;
      }
      shifted = false;
    }
    else if(k == KEY_SHIFT) {        // the R47's f (DMCP's SHIFT position)
      shifted = !shifted;
    }
    else {
      char c = 0;
      if(shifted) {
        c = digitOfKey(k);
        shifted = false;
      }
      else if(k > 0 && k <= 37 && key_to_alpha_table) {
        c = key_to_alpha_table[k];
        if(c == '@' || c == ' ' || c == ',' || c == '?') {
          c = 0;                      // not in a file name
        }
      }
      if(c && len < room) {
        name[len++] = c;
        name[len] = 0;
      }
    }
  }
}


static int compareNames(const void *a, const void *b) {
  return strcmp((const char *)a, (const char *)b);
}


// ---- the file browser ----

int file_selection_screen(const char *title, const char *base_dir, const char *ext, file_sel_fn_t sel_fn,
                          int disp_new, int overwrite_check, void *data) {
  static char names[MAX_FILES][PLAT_NAME_MAX];
  char dir[128], path[64], line[64];
  int cur = 0, top = 0;

  dmcp_host_path(dir, sizeof(dir), base_dir);

  for(;;) {
    // (re)read the folder: a save may have added a file
    int found = plat_fs_list(dir, names, MAX_FILES), n = 0;
    for(int i = 0; i < found; i++) {
      if(endsWith(names[i], ext) && strlen(names[i]) <= NAME_MAX) {
        if(n != i) {
          strcpy(names[n], names[i]);
        }
        n++;
      }
    }
    qsort(names, (size_t)n, PLAT_NAME_MAX, compareNames);
    int first = disp_new ? 1 : 0;     // item 0 is the new file
    int items = n + first;
    if(cur >= items) {
      cur = items - 1;
    }
    if(cur < 0) {
      cur = 0;
    }

    for(bool_t reread = false; !reread; ) {
      if(cur < top) {
        top = cur;
      }
      if(cur >= top + LIST_LINES) {
        top = cur - LIST_LINES + 1;
      }
      lcd_clear_buf();
      textAt(0, title, true);
      if(items == 0) {
        snprintf(line, sizeof(line), "No %s files in %s", ext, base_dir);
        textAt(2, line, false);
      }
      for(int i = top; i < items && i < top + LIST_LINES; i++) {
        const char *label = (i < first) ? "[ New file ]" : names[i - first];
        textAt(1 + i - top, label, i == cur);
      }
      textAt(LINES - 1, "ENTER choose   EXIT cancel", false);
      lcd_refresh();

      int k = waitKey();
      if(k == KEY_UP) {
        cur = (cur > 0) ? cur - 1 : (items ? items - 1 : 0);
      }
      else if(k == KEY_DOWN) {
        cur = (cur + 1 < items) ? cur + 1 : 0;
      }
      else if(k == KEY_EXIT) {
        return MRET_EXIT;
      }
      else if(k == KEY_ENTER && items > 0) {
        char name[NAME_MAX + 1];
        if(cur < first) {
          if(!editName(title, ext, name)) {
            reread = true;
            continue;
          }
        }
        else {
          strcpy(name, names[cur - first]);
        }
        snprintf(path, sizeof(path), "%s/%s", base_dir, name);
        if(overwrite_check && file_exists(path)) {
          if(!confirm(title, name, "exists. Replace it?")) {
            reread = true;
            continue;
          }
        }
        lcd_clear_buf();
        lcd_setLine(t24, 0);
        int r = sel_fn(path, name, data);
        if(r != 0) {
          return r;
        }
        reread = true;
      }
    }
  }
}


void disp_disk_info(const char *hdr) {
  char dir[128], line[64];
  static char names[MAX_FILES][PLAT_NAME_MAX];
  dmcp_host_path(dir, sizeof(dir), "");
  lcd_clear_buf();
  textAt(0, hdr, true);
  textAt(2, "C47's files are in the storage", false);
  textAt(3, "memory, folder \\C47", false);
  static const char *const folders[] = {"SAVFILES", "STATE", "PROGRAMS", "DATA", "LIBRARY"};
  for(unsigned i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
    char path[128];
    snprintf(path, sizeof(path), "%s%s", dir, folders[i]);
    int n = plat_fs_list(path, names, MAX_FILES);
    snprintf(line, sizeof(line), "  %-9s %d file%s", folders[i], n < 0 ? 0 : n, n == 1 ? "" : "s");
    textAt(5 + (int)i, line, false);
  }
  lcd_refresh();
}
