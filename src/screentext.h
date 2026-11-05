/*
 * screentext.h
 *
 * Text between the firmware's screen and the Mac's pasteboard. The screen
 * holds CP437 character codes (the order of the renderer's face and of the
 * guest's bytes); the pasteboard holds Unicode.
 */

#pragma once

#include <QByteArray>
#include <QString>

/* A line's character codes (Terminal::lineCells()) from column `from` to
 * `to` inclusive, as Unicode, with trailing blanks dropped. */
QString screentext_cells(const QByteArray &cells, int from, int to);

/* The word at column `col` of a line's character codes: the run of
 * non-blank cells around it, within columns left..right. A word is anything
 * between blanks, so a whole path or option (/dd/cmds, -e=foo) is one. False,
 * leaving *from and *to alone, if the cell is blank. */
bool screentext_word(const QByteArray &cells, int col, int left, int right,
    int *from, int *to);

/* Pasted text as the bytes the guest would have been typed: CP437, with each
 * line ending (CR LF, LF or CR) as the CR the OS-9 line editor expects, tab
 * as itself, other control characters dropped, and any character CP437
 * lacks as '?'. */
QByteArray screentext_paste(const QString &text);
