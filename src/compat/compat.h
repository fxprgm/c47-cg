/* SPDX-License-Identifier: GPL-3.0-only
 * Included before every file of the calculator build (CMakeLists.txt): what C47 uses that
 * fxlibc doesn't declare. Implemented in compat.c. */
#ifndef C47CG_COMPAT_H
#define C47CG_COMPAT_H
char *stpcpy(char *dst, const char *src);
#endif
