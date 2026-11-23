// SPDX-License-Identifier: GPL-3.0-only
// Shared between the DMCP layer's files.
#ifndef C47CG_DMCP_CG_H
#define C47CG_DMCP_CG_H

#include <stddef.h>

// A DMCP path ("STATE\x.s47", "/SCREENS/...") as a path under plat_files_root() (dmcp_ff.c).
void dmcp_host_path(char *out, size_t size, const char *path);

#endif
