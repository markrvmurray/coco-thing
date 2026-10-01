/*
 * fw_platform.cpp
 *
 * The platform-provided entry points the firmware's portable units call.
 * On the board these touch hardware; a serial terminal has none of it.
 */

#include "fw_platform.h"
#include "video.h"
#include "vid_link.h"

static void (*bell_hook)(void);

void
fw_platform_set_bell(void (*bell)(void))
{
	bell_hook = bell;
}

/* The console bell ($07). On the board a PWM tone; here whatever the app
 * set, which is the system alert sound. */
extern "C" void
vid_link_bell(void)
{
	if (bell_hook != nullptr)
		bell_hook();
}

/* The guest's Tone command ($1B $27). Not yet played: the host has no
 * synthesiser behind it. The console has already framed and consumed the
 * command, so ignoring it here desynchronises nothing. */
extern "C" void
vid_link_tone(uint16_t pitch, uint8_t ticks, uint8_t volume)
{
	(void)pitch;
	(void)ticks;
	(void)volume;
}

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
