/*
 * shim/hardware/pio.h -- host stand-in (see ../pico.h).
 *
 * pico_thing_video.h names the PIO handle type in a struct; give the host an
 * opaque equivalent.
 */

#pragma once

typedef void *PIO;
