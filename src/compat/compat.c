/* SPDX-License-Identifier: GPL-3.0-only
 * See compat.h. */
#include <string.h>
#include "compat.h"

char *stpcpy(char *dst, const char *src) {
  size_t n = strlen(src);
  memcpy(dst, src, n + 1);
  return dst + n;
}
