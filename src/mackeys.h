/*
 * mackeys.h
 *
 * macOS virtual key codes (NSEvent.keyCode, which Qt hands over as
 * QKeyEvent::nativeVirtualKey()) to USB HID keyboard usages. A key code names
 * a PHYSICAL key, whatever the Mac's layout setting, and so does a usage: this
 * is the table a USB keyboard would have supplied, so the firmware's own HID
 * cooker can turn the keys into bytes exactly as it does on the DVI console.
 *
 * One pair depends on the keyboard's shape. On an ISO keyboard the key left
 * of 1 is kVK_ISO_Section and the key right of left Shift is kVK_ANSI_Grave;
 * on an ANSI keyboard kVK_ANSI_Grave is the key left of 1 and the other key
 * does not exist.
 */

#pragma once

#include <cstdint>

/* The usage for key code `keycode`, or 0 for a key with none (Fn, the media
 * keys) and for the modifiers, which travel in the report's modifier byte. */
uint8_t mackeys_usage(unsigned keycode, bool iso);

/* True if the keyboard in use is ISO-shaped. */
bool mackeys_keyboard_is_iso();

/* Key codes the keyboard handles specially rather than as usages. */
static const unsigned MACKEY_CAPS_LOCK = 0x39;
