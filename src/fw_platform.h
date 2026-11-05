/*
 * fw_platform.h
 *
 * Hooks for the platform entry points the firmware's portable units call
 * (fw_platform.cpp), for the parts the app supplies.
 */

#pragma once

/* What the console bell ($07) does; nothing until the app sets it. */
void fw_platform_set_bell(void (*bell)(void));

/* Called as the console is about to scroll the text rectangle (col, row,
 * cols, rows) up one row, losing its top row -- while that row is still on
 * screen. The console also scrolls a rectangle to delete a line ($1F $31),
 * which arrives here the same way. See fw_scroll.cpp. */
void fw_platform_set_scroll(void (*hook)(int col, int row, int cols,
    int rows));
