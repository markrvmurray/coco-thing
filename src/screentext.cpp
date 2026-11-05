/*
 * screentext.cpp
 *
 * See screentext.h.
 */

#include "screentext.h"

#include <QHash>

#include "fontface.h"

/* A cell showing nothing: empty, space, or CP437's no-break space. */
static bool
blank(uint8_t code)
{
	return code == 0 || code == ' ' || code == 0xFF;
}

QString
screentext_cells(const QByteArray &cells, int from, int to)
{
	QString line;

	for (int col = qMax(from, 0); col <= to && col < cells.size(); col++) {
		const uint8_t code = (uint8_t)cells[col];
		const char32_t u = fontface_cp437(code);

		line += blank(code) ? QChar(' ') : QString::fromUcs4(&u, 1);
	}
	while (line.endsWith(' '))
		line.chop(1);
	return line;
}

bool
screentext_word(const QByteArray &cells, int col, int left, int right,
    int *from, int *to)
{
	const auto at = [&cells](int c) { return (uint8_t)cells[c]; };

	if (col < left || col > right || col < 0 || col >= cells.size())
		return false;
	if (blank(at(col)))
		return false;

	int a = col, b = col;
	while (a > left && !blank(at(a - 1)))
		a--;
	while (b < right && b + 1 < cells.size() && !blank(at(b + 1)))
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
