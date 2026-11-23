/* SPDX-License-Identifier: GPL-3.0-only
 * fxlibc has no <libgen.h>; C47's c47.h includes it but calls nothing from it on the
 * calculator. */
#ifndef C47CG_LIBGEN_H
#define C47CG_LIBGEN_H
char *basename(char *path);
char *dirname(char *path);
#endif
