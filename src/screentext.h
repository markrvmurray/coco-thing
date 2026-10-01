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

/* The text of the cells from (col0, row0) to (col1, row1) inclusive, read
 * as a terminal selection runs: to the end of each row, and from column
 * `left` of every row after the first (the text area's left edge). Trailing
 * spaces on each row are dropped and rows are joined with '\n'. Read from
 * the renderer's live text grid, so it is what is on screen. */
QString screentext_read(int col0, int row0, int col1, int row1, int left,
    int right);

/* The word under cell (col, row): the run of non-blank cells around it,
 * within columns left..right. A word is anything between blanks, so a whole
 * path or option (/dd/cmds, -e=foo) is one. False, leaving *from and *to
 * alone, if the cell is blank. */
bool screentext_word(int col, int row, int left, int right, int *from,
    int *to);

/* Pasted text as the bytes the guest would have been typed: CP437, with each
 * line ending (CR LF, LF or CR) as the CR the OS-9 line editor expects, tab
 * as itself, other control characters dropped, and any character CP437
 * lacks as '?'. */
QByteArray screentext_paste(const QString &text);
