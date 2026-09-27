/*
 * shim/pico.h
 *
 * Host stand-in for the pico-sdk decorations the firmware's portable
 * translation units use. Maps them onto nothing, so the SAME sources build
 * for the RP2350 (real SDK) and here. Keep it to what those units need.
 */

#pragma once

#define __scratch_y(group)
#define __not_in_flash_func(f) f
