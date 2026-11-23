/* SPDX-License-Identifier: GPL-3.0-only
 * See compat.h; also C library functions fxlibc gets wrong (below). */
#include <string.h>
#include "compat.h"

char *stpcpy(char *dst, const char *src) {
  size_t n = strlen(src);
  memcpy(dst, src, n + 1);
  return dst + n;
}


/* fxlibc 1.5.1's strchr and strrchr never match the terminating null (C: "The terminating
   null character is considered to be part of the string", so strchr(s, '\0') is the end of
   s; fxlibc returns NULL) and compare a plain (signed) char with the int argument, so a
   byte >= 0x80 passed as 0x80..0xFF never matches. C47 does both: complex numbers lost their
   imaginary part on screen (display.c appends it at strchr(displayString, '\0')). These
   replace fxlibc's, which the linker then leaves out. */
char *strchr(const char *s, int c) {
  const char ch = (char)c;
  for(;; s++) {
    if(*s == ch) {
      return (char *)s;
    }
    if(*s == '\0') {
      return NULL;
    }
  }
}


char *strrchr(const char *s, int c) {
  const char ch = (char)c;
  const char *last = NULL;
  for(;; s++) {
    if(*s == ch) {
      last = s;
    }
    if(*s == '\0') {
      return (char *)last;
    }
  }
}


char *strchrnul(const char *s, int c) {
  const char ch = (char)c;
  while(*s != '\0' && *s != ch) {
    s++;
  }
  return (char *)s;
}
