/*
 * keymap.h
 *
 * Qt key events to the bytes the guest expects. The reference is the video
 * Pico's USB keyboard cooker (hid_cook.cpp in the firmware tree), so a
 * program sees the same bytes whether it is typed at on the DVI console or
 * here: plain bytes, CR for Enter, Ctrl chords folded onto C0, the arrows
 * from vtio's KeyTbl, F1-F12 as $80-$8B, and the UK pound and not signs as
 * $9C and $AA.
 */

#pragma once

#include <QByteArray>

class QKeyEvent;

/* The bytes for one key press; empty if the key produces nothing. */
QByteArray keymap_translate(const QKeyEvent *ev);
