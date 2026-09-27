/*
 * test_fontface.cpp
 *
 * Faces from each source. The PSF files are built here, glyph N filled with
 * the byte N, so where each glyph landed in the face says which glyph it was.
 */

#include <cstdio>
#include <cstring>

#include <QGuiApplication>
#include <QSet>

#include "fontface.h"
#include "font_spleen_8x16.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

/* Glyph `code` of `face` is every row equal to `v`. */
static bool
glyph_is(const QByteArray &face, int code, uint8_t v)
{
	for (int r = 0; r < FONTFACE_ROWS; r++)
		if ((uint8_t)face[code * FONTFACE_ROWS + r] != v)
			return false;
	return true;
}

static bool
glyph_builtin(const QByteArray &face, int code)
{
	return memcmp(face.constData() + code * FONTFACE_ROWS,
	    font_spleen_8x16[code], FONTFACE_ROWS) == 0;
}

static bool
glyph_blank(const QByteArray &face, int code)
{
	return glyph_is(face, code, 0);
}

static void
put_le32(QByteArray &b, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		b.append((char)(v >> (8 * i)));
}

/* `count` glyphs, glyph N filled with N. */
static QByteArray
glyph_block(int count)
{
	QByteArray b;

	for (int g = 0; g < count; g++)
		b.append(QByteArray(FONTFACE_ROWS, (char)g));
	return b;
}

static QByteArray
psf1(int height, const QByteArray &table)
{
	QByteArray f;

	f.append((char)0x36);
	f.append((char)0x04);
	f.append((char)(table.isEmpty() ? 0x00 : 0x02));
	f.append((char)height);
	for (int g = 0; g < 256; g++)
		f.append(QByteArray(height, (char)g));
	return f + table;
}

static void
psf1_entry(QByteArray &t, std::initializer_list<uint16_t> us)
{
	for (uint16_t u : us) {
		t.append((char)u);
		t.append((char)(u >> 8));
	}
	t.append((char)0xFF);
	t.append((char)0xFF);
}

static QByteArray
psf2(int count, int width, int height, const QByteArray &table)
{
	QByteArray f;

	put_le32(f, 0x864AB572u);
	put_le32(f, 0);				/* version */
	put_le32(f, 32);			/* header size */
	put_le32(f, table.isEmpty() ? 0 : 1);	/* flags: Unicode table */
	put_le32(f, count);
	put_le32(f, ((width + 7) / 8) * height);
	put_le32(f, height);
	put_le32(f, width);
	if (width == 8 && height == FONTFACE_ROWS)
		f += glyph_block(count);
	else
		f += QByteArray(count * ((width + 7) / 8) * height, 0);
	return f + table;
}

static void
test_cp437()
{
	CHECK(fontface_cp437(0) == 0);
	CHECK(fontface_cp437('A') == U'A');
	CHECK(fontface_cp437(0x7F) == U'⌂');
	CHECK(fontface_cp437(0x9C) == U'£');	/* the keyboard's pound */
	CHECK(fontface_cp437(0xAA) == U'¬');	/* the keyboard's not */
	CHECK(fontface_cp437(0xB3) == U'│');
	CHECK(fontface_cp437(0xDB) == U'█');
	CHECK(fontface_cp437(0xFF) == U' ');

	/* A code for every character and a character for every code: each
	 * entry distinct, or the table has slipped a place somewhere. */
	QSet<char32_t> seen;
	for (int c = 1; c < FONTFACE_GLYPHS; c++)
		seen.insert(fontface_cp437(c));
	CHECK(seen.size() == FONTFACE_GLYPHS - 1);
}

static void
test_builtin_and_raw()
{
	QString error;
	const QByteArray builtin = fontface_builtin();

	CHECK(builtin.size() == FONTFACE_BYTES);
	CHECK(glyph_builtin(builtin, 'A'));

	const QByteArray raw = glyph_block(FONTFACE_GLYPHS);
	CHECK(fontface_parse(raw, &error) == raw);
	CHECK(error.isEmpty());
}

static void
test_psf1()
{
	QString error;

	/* No table: glyphs by position. */
	QByteArray face = fontface_parse(psf1(16, {}), &error);
	CHECK(error.isEmpty());
	CHECK(face.size() == FONTFACE_BYTES);
	CHECK(glyph_is(face, 'A', 'A'));
	CHECK(glyph_is(face, 0x9C, 0x9C));
	CHECK(glyph_builtin(face, 0));

	/* A table: glyph 5 is 'A' (and U+0391), glyph 7 the pound sign, glyph
	 * 9 'Z' then a sequence for 'e' that must not place 'e'. */
	QByteArray table;
	for (int g = 0; g < 256; g++) {
		if (g == 5)
			psf1_entry(table, { 'A', 0x0391 });
		else if (g == 7)
			psf1_entry(table, { 0x00A3 });
		else if (g == 9)
			psf1_entry(table, { 'Z', 0xFFFE, 'e', 0x0301 });
		else
			psf1_entry(table, {});
	}
	face = fontface_parse(psf1(16, table), &error);
	CHECK(error.isEmpty());
	CHECK(glyph_is(face, 'A', 5));
	CHECK(glyph_is(face, 0x9C, 7));
	CHECK(glyph_is(face, 'Z', 9));
	CHECK(glyph_builtin(face, 'e'));
	CHECK(glyph_builtin(face, 'B'));	/* not in the table */

	/* 8x8 glyphs do not fit the cell. */
	error.clear();
	CHECK(fontface_parse(psf1(8, {}), &error).isEmpty());
	CHECK(!error.isEmpty());
}

static void
test_psf2()
{
	QString error;

	QByteArray face = fontface_parse(psf2(256, 8, 16, {}), &error);
	CHECK(error.isEmpty());
	CHECK(glyph_is(face, 'q', 'q'));

	/* A 512-glyph font with a UTF-8 table: glyph 300 is the pound sign,
	 * glyph 3 is 'x' then a sequence that must not place 'y'. */
	QByteArray table;
	for (int g = 0; g < 512; g++) {
		if (g == 300)
			table += QString(QChar(0x00A3)).toUtf8();
		else if (g == 3)
			table += QByteArray("x") + (char)0xFE + "y";
		table.append((char)0xFF);
	}
	QByteArray file = psf2(512, 8, 16, table);
	/* Glyph 300 filled with its own index would wrap a byte; mark it. */
	memset(file.data() + 32 + 300 * FONTFACE_ROWS, 0xA5, FONTFACE_ROWS);
	face = fontface_parse(file, &error);
	CHECK(error.isEmpty());
	CHECK(glyph_is(face, 0x9C, 0xA5));
	CHECK(glyph_is(face, 'x', 3));
	CHECK(glyph_builtin(face, 'y'));

	error.clear();
	CHECK(fontface_parse(psf2(256, 9, 16, {}), &error).isEmpty());
	CHECK(!error.isEmpty());

	error.clear();
	CHECK(fontface_parse(psf2(256, 8, 16, {}).left(1000), &error).isEmpty());
	CHECK(!error.isEmpty());
}

static void
test_rejects()
{
	QString error;

	CHECK(fontface_parse(QByteArray(100, 'x'), &error).isEmpty());
	CHECK(!error.isEmpty());

	error.clear();
	CHECK(fontface_load("/nonexistent/font.psf", &error).isEmpty());
	CHECK(!error.isEmpty());
}

static void
test_render()
{
	const QByteArray keep = fontface_render(QFont("Menlo"), true);
	const QByteArray drawn = fontface_render(QFont("Menlo"), false);

	CHECK(keep.size() == FONTFACE_BYTES);
	CHECK(!glyph_blank(keep, 'A'));
	CHECK(!glyph_blank(keep, 'g'));
	CHECK(glyph_blank(keep, ' '));
	CHECK(!glyph_builtin(keep, 'A'));	/* drawn, not left as Spleen */

	/* Box glyphs kept from the built-in face only when asked. */
	int kept = 0, redrawn = 0;
	for (int c = 0xB0; c <= 0xDF; c++) {
		kept += glyph_builtin(keep, c);
		redrawn += !glyph_builtin(drawn, c);
	}
	CHECK(kept == 0xDF - 0xB0 + 1);
	CHECK(redrawn > 0);
}

static void
test_sample()
{
	const QImage img = fontface_sample({}, "A", qRgb(255, 255, 255),
	    qRgb(0, 0, 0));

	CHECK(img.width() == 8 && img.height() == FONTFACE_ROWS);
	for (int r = 0; r < FONTFACE_ROWS; r++)
		for (int c = 0; c < 8; c++) {
			const bool on = font_spleen_8x16['A'][r] & (0x80 >> c);
			CHECK((img.pixel(c, r) == qRgb(255, 255, 255)) == on);
		}
}

int
main(int argc, char *argv[])
{
	QGuiApplication app(argc, argv);

	test_cp437();
	test_builtin_and_raw();
	test_psf1();
	test_psf2();
	test_rejects();
	test_render();
	test_sample();

	if (failures == 0)
		printf("test_fontface: all passed\n");
	return failures == 0 ? 0 : 1;
}
