/*
 * fw_platform.h
 *
 * Hooks for the platform entry points the firmware's portable units call
 * (fw_platform.cpp), for the parts the app supplies.
 */

#pragma once

/* What the console bell ($07) does; nothing until the app sets it. */
void fw_platform_set_bell(void (*bell)(void));
