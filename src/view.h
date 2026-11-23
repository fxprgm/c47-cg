// SPDX-License-Identifier: GPL-3.0-only
#ifndef C47CG_VIEW_H
#define C47CG_VIEW_H

#include <stdint.h>

#define VIEW_W 396
#define VIEW_H 224

extern uint16_t view_paper, view_ink;

void view_init(void);
void view_all_changed(void);        // redraw everything at the next plat_flush (after the OS drew)

// From the platform: the 396x224 RGB565 frame to draw into, and showing rows [y, y+height).
uint16_t *plat_framebuffer(void);
void plat_present(int y, int height);

void dmcp_init(void);               // dmcp_sys.c
void program_main(void);            // C47 (upstream src/c47/c47.c)

#endif
