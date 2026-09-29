#ifndef FRONTEND_H
#define FRONTEND_H

#include "gb.h"
#include "joypad.h"

/* Implemented by the platform window code (frontend_cocoa.m on macOS). */
void   frontend_init(int scale, const char *title);
bool   frontend_pick_rom(char *out_path, size_t size);  /* file picker; false if cancelled */
void   frontend_update(const uint32_t *framebuffer);
bool   frontend_process_events(Joypad *joypad);         /* false when the window closes */
void   frontend_set_title(const char *title);

/* Display sync. frontend_wait_for_refresh() blocks until the screen's next
   refresh and returns how many refreshes happened since the last call
   (usually 1), 0 if the wait timed out, or -1 if display sync isn't available. */
int    frontend_wait_for_refresh(void);
double frontend_refresh_rate(void);                     /* in Hz, 0 if unknown */

#endif
