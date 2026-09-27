/*
 * fw_platform.cpp
 *
 * The platform-provided entry points the firmware's portable units call.
 * On the board these touch hardware; a serial terminal has none of it.
 */

#include "video.h"
#include "vid_link.h"

/* The guest-link !IRQ pin. There is no 6809 bus here, so nothing to drive. */
extern "C" void
vid_link_irq_out(bool assert)
{
	(void)assert;
}

/* On the board this waits for vsync; the Qt side paints on its own timer. */
extern "C" void
vidterm_present(void)
{
}
