/*
 * fontface.cpp
 *
 * See fontface.h.
 */

#include "fontface.h"

#include <cstdint>
#include <cstring>

#include <QFile>
#include <QFontMetrics>
#include <QHash>
#include <QPainter>

#include "font_spleen_8x16.h"

/* CP437, the order the firmware's face and the guest's bytes both use (the
 * keyboard sends the pound sign as $9C and the not sign as $AA, which is
 * where CP437 has them). Code 0 draws nothing; $FF is a no-break space. */
static const char16_t cp437[FONTFACE_GLYPHS + 1] =
    u" ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼"
    u" !\"#$%&'()*+,-./0123456789:;<=>?"
    u"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
    u"`abcdefghijklmnopqrstuvwxyz{|}~⌂"
    u"ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒ"
    u"áíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐"
    u"└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀"
    u"αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ";

static_assert(sizeof(cp437) / sizeof(cp437[0]) == FONTFACE_GLYPHS + 1,
    "the CP437 table must have exactly 256 entries");

/* The box-drawing and block glyphs, which must meet the cell edges. */
static const int BOX_FIRST = 0xB0;
static const int BOX_LAST = 0xDF;

char32_t
fontface_cp437(int code)
{
	if (code <= 0 || code >= FONTFACE_GLYPHS)
		return 0;
	return cp437[code];
}

QByteArray
fontface_builtin()
{
	return QByteArray(reinterpret_cast<const char *>(font_spleen_8x16),
	    FONTFACE_BYTES);
}

/* The largest pixel size at which the font fits the 8x16 cell. */
static QFont
fit_to_cell(const QFont &base)
{
	QFont f(base);

	f.setStyleStrategy(QFont::NoAntialias);
	f.setHintingPreference(QFont::PreferFullHinting);
	for (int px = FONTFACE_ROWS; px > 4; px--) {
		f.setPixelSize(px);
		const QFontMetrics fm(f);
		if (fm.height() <= FONTFACE_ROWS &&
		    fm.horizontalAdvance(QChar('M')) <= 8)
			break;
	}
	return f;
}

QByteArray
fontface_render(const QFont &font, bool keep_box)
{
	QByteArray face = fontface_builtin();
	const QFont f = fit_to_cell(font);
	const QFontMetrics fm(f);
	const int baseline = (FONTFACE_ROWS - fm.height()) / 2 + fm.ascent();
	QImage cell(8, FONTFACE_ROWS, QImage::Format_Grayscale8);

	for (int code = 1; code < FONTFACE_GLYPHS; code++) {
		const char32_t u = fontface_cp437(code);

		if (keep_box && code >= BOX_FIRST && code <= BOX_LAST)
			continue;
		if (!fm.inFontUcs4(u))
			continue;	/* keep the built-in glyph */

		const QString s = QString::fromUcs4(&u, 1);
		const int x = qMax(0, (8 - fm.horizontalAdvance(s)) / 2);

		cell.fill(0);
		{
			QPainter p(&cell);
			p.setFont(f);
			p.setPen(Qt::white);
			p.drawText(x, baseline, s);
		}
		for (int row = 0; row < FONTFACE_ROWS; row++) {
			const uint8_t *px = cell.constScanLine(row);
			uint8_t bits = 0;

			for (int col = 0; col < 8; col++)
				if (px[col] > 127)
					bits |= (uint8_t)(0x80 >> col);
			face[code * FONTFACE_ROWS + row] = (char)bits;
		}
	}
	return face;
}

/* ---- PSF console fonts ------------------------------------------------- */

static uint32_t
le32(const QByteArray &b, int at)
{
	const uint8_t *p = reinterpret_cast<const uint8_t *>(b.constData()) + at;

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Build the face from `count` 16-byte glyphs at `glyphs`, placed by `where`
 * (Unicode character to glyph index) when the font has a table, or by
 * position when it does not. */
static QByteArray
place_glyphs(const QByteArray &file, int glyphs, int count,
    const QHash<char32_t, int> &where)
{
	QByteArray face = fontface_builtin();

	for (int code = 0; code < FONTFACE_GLYPHS; code++) {
		int g;

		if (where.isEmpty())
			g = code < count ? code : -1;
		else
			g = where.value(fontface_cp437(code), -1);
		if (code == 0 || g < 0)
			continue;	/* keep the built-in glyph */
		memcpy(face.data() + code * FONTFACE_ROWS,
		    file.constData() + glyphs + g * FONTFACE_ROWS, FONTFACE_ROWS);
	}
	return face;
}

/* PSF1: magic 36 04, mode, bytes per glyph; the Unicode table is 16-bit
 * little-endian entries per glyph, $FFFE starting a sequence (skipped) and
 * $FFFF ending the glyph's list. */
static QByteArray
parse_psf1(const QByteArray &file, QString *error)
{
	const uint8_t mode = (uint8_t)file[2];
	const int height = (uint8_t)file[3];
	const int count = (mode & 0x01) ? 512 : 256;
	const int glyphs = 4;

	if (height != FONTFACE_ROWS) {
		*error = QString("PSF1 glyphs are 8x%1; only 8x16 fits the cell")
		    .arg(height);
		return {};
	}
	if (file.size() < glyphs + count * height) {
		*error = "PSF1 file is truncated";
		return {};
	}

	QHash<char32_t, int> where;
	if (mode & 0x02) {
		int at = glyphs + count * height;
		for (int g = 0; g < count && at + 1 < file.size(); g++) {
			bool seq = false;
			for (; at + 1 < file.size(); at += 2) {
				const uint16_t u = (uint8_t)file[at] |
				    (uint16_t)((uint8_t)file[at + 1] << 8);
				if (u == 0xFFFF) {
					at += 2;
					break;
				}
				if (u == 0xFFFE)
					seq = true;
				else if (!seq && !where.contains(u))
					where.insert(u, g);
			}
		}
	}
	return place_glyphs(file, glyphs, count, where);
}

/* PSF2: a 32-byte little-endian header; the Unicode table is UTF-8 per glyph,
 * $FE starting a sequence (skipped) and $FF ending the glyph's list. */
static QByteArray
parse_psf2(const QByteArray &file, QString *error)
{
	if (file.size() < 32) {
		*error = "PSF2 header is truncated";
		return {};
	}
	const uint32_t header = le32(file, 8);
	const uint32_t flags = le32(file, 12);
	const uint32_t count = le32(file, 16);
	const uint32_t bytes = le32(file, 20);
	const uint32_t height = le32(file, 24);
	const uint32_t width = le32(file, 28);

	if (width != 8 || height != FONTFACE_ROWS || bytes != FONTFACE_ROWS) {
		*error = QString("PSF2 glyphs are %1x%2; only 8x16 fits the cell")
		    .arg(width).arg(height);
		return {};
	}
	if (count == 0 || header < 32 ||
	    (uint64_t)header + (uint64_t)count * bytes > (uint64_t)file.size()) {
		*error = "PSF2 file is truncated";
		return {};
	}

	QHash<char32_t, int> where;
	if (flags & 0x01) {
		int at = (int)(header + count * bytes);
		for (uint32_t g = 0; g < count && at < file.size(); g++) {
			const int end = file.indexOf((char)0xFF, at);
			const int stop = end < 0 ? (int)file.size() : end;
			const int seq = file.indexOf((char)0xFE, at);
			const int single_end = (seq >= 0 && seq < stop) ? seq : stop;
			const QString s = QString::fromUtf8(
			    file.constData() + at, single_end - at);

			for (const char32_t u : s.toUcs4())
				if (!where.contains(u))
					where.insert(u, (int)g);
			at = stop + 1;
		}
	}
	return place_glyphs(file, (int)header, (int)count, where);
}

QByteArray
fontface_parse(const QByteArray &file, QString *error)
{
	if (file.size() == FONTFACE_BYTES)
		return file;	/* a raw face, as the protocol loads one */
	if (file.size() >= 4 && (uint8_t)file[0] == 0x36 &&
	    (uint8_t)file[1] == 0x04)
		return parse_psf1(file, error);
	if (file.size() >= 4 && le32(file, 0) == 0x864AB572u)
		return parse_psf2(file, error);
	*error = QString("not a font this terminal can use: expected a raw "
	    "%1-byte face or a PSF1/PSF2 8x16 font").arg(FONTFACE_BYTES);
	return {};
}

QByteArray
fontface_load(const QString &path, QString *error)
{
	QFile f(path);

	if (!f.open(QIODevice::ReadOnly)) {
		*error = f.errorString();
		return {};
	}
	return fontface_parse(f.readAll(), error);
}

QImage
fontface_sample(const QByteArray &face, const QString &text, QRgb fg, QRgb bg)
{
	const QByteArray use = face.isEmpty() ? fontface_builtin() : face;
	const QList<uint> chars = text.toUcs4();
	QImage img(qMax(1, (int)chars.size()) * 8, FONTFACE_ROWS,
	    QImage::Format_ARGB32);

	img.fill(bg);
	for (int i = 0; i < chars.size(); i++) {
		int code = '?';
		for (int c = 1; c < FONTFACE_GLYPHS; c++)
			if (fontface_cp437(c) == chars[i]) {
				code = c;
				break;
			}
		for (int row = 0; row < FONTFACE_ROWS; row++) {
			const int at = code * FONTFACE_ROWS + row;
			const uint8_t bits = at < use.size() ? (uint8_t)use[at] : 0;
			for (int col = 0; col < 8; col++)
				if (bits & (0x80 >> col))
					img.setPixel(i * 8 + col, row, fg);
		}
	}
	return img;
}
