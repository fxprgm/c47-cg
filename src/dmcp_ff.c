// SPDX-License-Identifier: GPL-3.0-only
/*
 * The FatFs subset C47 uses (f_open/f_read/f_write/f_lseek/f_close/f_unlink, the f_size and
 * f_eof macros) and DMCP's file helpers, over POSIX open/read/write, which gint provides on the
 * calculator's storage memory and the C library on a PC.
 *
 * The calls go through plat_fs_* (plat.h): on the calculator each one is a trip into the OS.
 *
 * Paths: C47 writes "SAVFILES\C47auto.sav" or "/SCREENS/..."; they are made relative to
 * plat_files_root() ("/C47" on the calculator), with '/' separators.
 *
 * C47 reads its state file one byte per f_read, which on the calculator would be one trip to
 * the OS's file system each, so reads and writes go through the FIL's own 512-byte buffer.
 * FIL fields used: obj.objsize and fptr (the f_size/f_eof macros read them), obj.lockid = file
 * descriptor + 1 (0 = not open), flag = FA_* mode, buf = the buffer, clust = bytes in the
 * buffer, sect = file position of buf[0], err = 1 while buf holds unwritten data.
 */
#include "c47.h"
#include "plat.h"

#include <fcntl.h>

#define BUF_SIZE ((UINT)sizeof(((FIL *)0)->buf))


static void hostPath(char *out, size_t size, const char *path) {
  while(*path == '/' || *path == '\\') {
    path++;
  }
  snprintf(out, size, "%s/%s", plat_files_root(), path);
  for(char *p = out; *p; p++) {
    if(*p == '\\') {
      *p = '/';
    }
  }
}


static int fdOf(FIL *fp) {
  return (int)fp->obj.lockid - 1;
}


static FRESULT flushWrite(FIL *fp) {
  if(fp->err && fp->clust) {
    int n = plat_fs_write(fdOf(fp), fp->buf, (int)fp->clust);
    if(n != (int)fp->clust) {
      return FR_DISK_ERR;
    }
  }
  fp->err = 0;
  fp->sect += fp->clust;
  fp->clust = 0;
  return FR_OK;
}


FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode) {
  char name[128];
  memset(fp, 0, sizeof(*fp));
  hostPath(name, sizeof(name), path);

  int flags = (mode & FA_WRITE) ? ((mode & FA_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
  if(mode & FA_CREATE_ALWAYS) {
    flags |= O_CREAT | O_TRUNC;
  }
  else if(mode & FA_CREATE_NEW) {
    flags |= O_CREAT | O_EXCL;
  }
  else if(mode & FA_OPEN_ALWAYS) {        // FA_OPEN_APPEND includes this bit
    flags |= O_CREAT;
  }

  int fd = plat_fs_open(name, flags);
  if(fd < 0) {
    return (flags & O_EXCL) && plat_fs_size(name) >= 0 ? FR_EXIST : FR_NO_FILE;
  }
  int size = plat_fs_seek(fd, 0, SEEK_END);
  if(size < 0) {
    size = 0;
  }
  fp->obj.objsize = (FSIZE_t)size;
  if((mode & FA_OPEN_APPEND) == FA_OPEN_APPEND) {
    fp->fptr = (FSIZE_t)size;
  }
  else {
    plat_fs_seek(fd, 0, SEEK_SET);
    fp->fptr = 0;
  }
  fp->sect = fp->fptr;
  fp->obj.lockid = (UINT)(fd + 1);
  fp->flag = mode;
  return FR_OK;
}


FRESULT f_close(FIL *fp) {
  if(fp == NULL || fp->obj.lockid == 0) {
    return FR_INVALID_OBJECT;
  }
  FRESULT r = flushWrite(fp);
  plat_fs_close(fdOf(fp));
  fp->obj.lockid = 0;
  return r;
}


FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) {
  UINT done = 0;
  if(br) {
    *br = 0;
  }
  if(fp == NULL || fp->obj.lockid == 0) {
    return FR_INVALID_OBJECT;
  }
  if(fp->err) {
    FRESULT r = flushWrite(fp);
    if(r != FR_OK) {
      return r;
    }
  }
  while(done < btr) {
    // buffer holds file bytes [sect, sect + clust)
    if(fp->fptr < fp->sect || fp->fptr >= fp->sect + fp->clust) {
      if(plat_fs_seek(fdOf(fp), (int)fp->fptr, SEEK_SET) < 0) {
        return FR_DISK_ERR;
      }
      int n = plat_fs_read(fdOf(fp), fp->buf, (int)BUF_SIZE);
      if(n < 0) {
        return FR_DISK_ERR;
      }
      fp->sect = fp->fptr;
      fp->clust = (DWORD)n;
      if(n == 0) {
        break;
      }
    }
    UINT off = (UINT)(fp->fptr - fp->sect);
    UINT take = (UINT)fp->clust - off;
    if(take > btr - done) {
      take = btr - done;
    }
    memcpy((uint8_t *)buff + done, fp->buf + off, take);
    done += take;
    fp->fptr += take;
  }
  if(br) {
    *br = done;
  }
  return FR_OK;
}


FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw) {
  UINT done = 0;
  if(bw) {
    *bw = 0;
  }
  if(fp == NULL || fp->obj.lockid == 0) {
    return FR_INVALID_OBJECT;
  }
  if(!fp->err) {                          // the buffer starts empty at the current position
    if(plat_fs_seek(fdOf(fp), (int)fp->fptr, SEEK_SET) < 0) {
      return FR_DISK_ERR;
    }
    fp->sect = fp->fptr;
    fp->clust = 0;
    fp->err = 1;
  }
  while(done < btw) {
    if(fp->clust == BUF_SIZE) {
      FRESULT r = flushWrite(fp);
      if(r != FR_OK) {
        return r;
      }
      fp->err = 1;
    }
    UINT take = BUF_SIZE - (UINT)fp->clust;
    if(take > btw - done) {
      take = btw - done;
    }
    memcpy(fp->buf + fp->clust, (const uint8_t *)buff + done, take);
    fp->clust += take;
    done += take;
    fp->fptr += take;
    if(fp->fptr > fp->obj.objsize) {
      fp->obj.objsize = fp->fptr;
    }
  }
  if(bw) {
    *bw = done;
  }
  return FR_OK;
}


FRESULT f_lseek(FIL *fp, FSIZE_t ofs) {
  if(fp == NULL || fp->obj.lockid == 0) {
    return FR_INVALID_OBJECT;
  }
  FRESULT r = flushWrite(fp);
  fp->clust = 0;
  fp->sect = ofs;
  fp->fptr = ofs;
  return r;
}


FRESULT f_unlink(const TCHAR *path) {
  char name[128];
  hostPath(name, sizeof(name), path);
  return plat_fs_unlink(name) == 0 ? FR_OK : FR_NO_FILE;
}


FRESULT f_rename(const TCHAR *path_old, const TCHAR *path_new) {
  char a[128], b[128];
  hostPath(a, sizeof(a), path_old);
  hostPath(b, sizeof(b), path_new);
  return plat_fs_rename(a, b) == 0 ? FR_OK : FR_NO_FILE;
}


int check_create_dir(const char *dir) {
  char name[128];
  hostPath(name, sizeof(name), dir);
  // DMCP makes the whole path; so do we, one level at a time.
  for(char *p = name + strlen(plat_files_root()) + 1; *p; p++) {
    if(*p == '/') {
      *p = 0;
      plat_fs_mkdir(name);
      *p = '/';
    }
  }
  plat_fs_mkdir(name);
  return 0;
}


int file_exists(const char *fn) {
  char name[128];
  hostPath(name, sizeof(name), fn);
  return plat_fs_size(name) >= 0;
}


int file_size(const char *fn) {
  char name[128];
  hostPath(name, sizeof(name), fn);
  return plat_fs_size(name);
}


int sys_disk_ok(void) {
  return 1;
}


int sys_disk_write_enable(int val) {
  (void)val;
  return 1;
}


int sys_is_disk_write_enable(void) {
  return 1;
}


void disp_disk_info(const char *hdr) {
  (void)hdr;
}


// DMCP's file browser (save/load under a chosen name). Not written yet: every manual
// save/load/export is cancelled. The automatic state save does not use it.
int file_selection_screen(const char *title, const char *base_dir, const char *ext, file_sel_fn_t sel_fn,
                          int disp_new, int overwrite_check, void *data) {
  (void)title; (void)base_dir; (void)ext; (void)sel_fn; (void)disp_new; (void)overwrite_check; (void)data;
  return MRET_EXIT;
}


// ---- DMCP's text output for its own dialogs (io.c's "Saving state ..." and the like).
// Not drawn yet; the dialogs are short-lived. ----

void lcd_writeText(disp_stat_t *ds, const char *text) {
  (void)ds; (void)text;
}


void lcd_setLine(disp_stat_t *ds, int ln_nr) {
  (void)ds; (void)ln_nr;
}


void lcd_print(disp_stat_t *ds, const char *fmt, ...) {
  (void)ds; (void)fmt;
}
