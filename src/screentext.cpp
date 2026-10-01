/*
 * screentext.cpp
 *
 * See screentext.h.
 */

#include "screentext.h"

#include <QHash>

#include "fontface.h"
#include "video.h"

QString
screentext_read(int col0, int row0, int col1, int row1, int left, int right)
{
	QStringList lines;
	uint8_t cells[TEXT_COLS * 2];

	for (int row = row0; row <= row1; row++) {
		const int from = row == row0 ? col0 : left;
		const int to = row == row1 ? col1 : right;
		QString line;

		if (row < 0 || row >= TEXT_ROWS || from > to)
			continue;
		vidterm_read_row(0, row, TEXT_COLS, cells);
		for (int col = qMax(from, 0); col <= to && col < TEXT_COLS; col++) {
			const char32_t u = fontface_cp437(cells[col * 2]);
			// Code 0 is an empty cell; the no-break space is just
			// a space to anyone pasting it.
			line += (u == 0 || u == U' ') ? QChar(' ') :
			    QString::fromUcs4(&u, 1);
		}
		while (line.endsWith(' '))
			line.chop(1);
		lines << line;
	}
	return lines.join('\n');
}

/* A cell showing nothing: empty, space, or CP437's no-break space. */
static bool
blank(uint8_t code)
{
	return code == 0 || code == ' ' || code == 0xFF;
}

bool
screentext_word(int col, int row, int left, int right, int *from, int *to)
{
	uint8_t cells[TEXT_COLS * 2];

	if (row < 0 || row >= TEXT_ROWS || col < left || col > right ||
	    col >= TEXT_COLS)
		return false;
	vidterm_read_row(0, row, TEXT_COLS, cells);
	if (blank(cells[col * 2]))
		return false;

	int a = col, b = col;
	while (a > left && !blank(cells[(a - 1) * 2]))
		a--;
	while (b < right && b + 1 < TEXT_COLS && !blank(cells[(b + 1) * 2]))
		b++;
	*from = a;
	*to = b;
	return true;
}

QByteArray
screentext_paste(const QString &text)
{
	static QHash<char32_t, uint8_t> code_of;
	if (code_of.isEmpty()) {
		for (int c = 1; c < FONTFACE_GLYPHS; c++)
			code_of.insert(fontface_cp437(c), (uint8_t)c);
	}

	QByteArray out;
	const QList<uint> chars = text.toUcs4();

	for (int i = 0; i < chars.size(); i++) {
		const char32_t u = chars[i];

		if (u == '\r' || u == '\n') {
			out.append('\r');
			if (u == '\r' && i + 1 < chars.size() && chars[i + 1] == '\n')
				i++;
			continue;
		}
		// Tab is typed as itself; any other control character on the
		// pasteboard is dropped rather than handed to the guest as a
		// command (an ESC would start one).
		if (u == '\t') {
			out.append('\t');
			continue;
		}
		if (u < 0x20 || u == 0x7F)
			continue;
		out.append((char)code_of.value(u, '?'));
	}
	return out;
}
