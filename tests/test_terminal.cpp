/*
 * test_terminal.cpp
 *
 * Drives Terminal with no board attached: bytes in as the serial link would
 * deliver them, the firmware's own framebuffer and command trace as oracles.
 */

#include <cstdio>
#include <cstring>

#include "fw_platform.h"
#include "screentext.h"
#include "terminal.h"
#include "video.h"
#include "video_render.h"
#include "vid_link.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

/* Run the firmware until it has taken every byte and finished drawing. */
static void
settle(Terminal &t)
{
	for (int i = 0; i < 100000; i++) {
		t.pump(5);
		if (t.backlog() == 0 && vid_link_idle())
			return;
	}
	fprintf(stderr, "settle: never went idle (backlog %lld)\n",
	    (long long)t.backlog());
	failures++;
}

static uint32_t
consumed()
{
	uint32_t oldest, next;

	vid_link_trace_span(&oldest, &next);
	return next;
}

static QByteArray
point(uint8_t sub, int x, int y)
{
	QByteArray b;

	b.append((char)0x1B);
	b.append((char)sub);
	b.append((char)(x >> 8));
	b.append((char)x);
	b.append((char)(y >> 8));
	b.append((char)y);
	return b;
}

static bool
cell_blank(int col, int row, uint8_t bg)
{
	for (int y = row * CHAR_H; y < (row + 1) * CHAR_H; y++)
		for (int x = col * CHAR_W; x < (col + 1) * CHAR_W; x++)
			if (video_framebuffer[y][x] != bg)
				return false;
	return true;
}

/* A burst far larger than the 2 KB command FIFO arrives at once, as a USB
 * CDC read can deliver it: every byte must reach the interpreter, because a
 * byte dropped at a full FIFO silently shifts every later parameter. */
static void
test_backlog_is_lossless(Terminal &t)
{
	QByteArray burst;

	for (int i = 0; i < 20000; i++)
		burst.append((char)(i % 64 == 63 ? '\n' : 'A' + i % 26));

	const uint32_t before = consumed();
	t.feed(burst);
	CHECK(t.backlog() == burst.size());
	settle(t);
	CHECK(t.backlog() == 0);
	CHECK(consumed() - before == (uint32_t)burst.size());
}

/* Text lands in the cell the cursor is on, and nowhere else. */
static void
test_text(Terminal &t)
{
	/* Cursor off first: it is drawn in the cell it sits on. */
	t.feed(QByteArray("\x05\x20\x0C", 3));	/* cursor off, clear, home */
	settle(t);
	const uint8_t bg = video_framebuffer[FB_HEIGHT - 1][FB_WIDTH - 1];
	CHECK(cell_blank(0, 0, bg));

	t.feed("X");
	settle(t);
	CHECK(!cell_blank(0, 0, bg));
	CHECK(cell_blank(1, 0, bg));
	CHECK(cell_blank(0, 1, bg));

	t.feed(QByteArray("\x02\x25\x22", 3) + "Y");	/* cursor-XY 5,2 */
	settle(t);
	CHECK(!cell_blank(5, 2, bg));
	CHECK(cell_blank(4, 2, bg));
}

/* A $1B graphics command draws: a Bar fills its rectangle and stops there. */
static void
test_bar(Terminal &t)
{
	t.feed(QByteArray("\x0C", 1));
	settle(t);
	const uint8_t bg = video_framebuffer[300][600];

	t.feed(point(0x40, 100, 200) + point(0x4A, 199, 299));
	settle(t);
	CHECK(video_framebuffer[250][150] != bg);
	CHECK(video_framebuffer[200][100] != bg);
	CHECK(video_framebuffer[299][199] != bg);
	CHECK(video_framebuffer[250][201] == bg);
	CHECK(video_framebuffer[301][150] == bg);
}

/* Every pixel of the cell one colour, and not the background: a glyph drawn
 * with every row $FF. */
static bool
cell_solid(int col, int row, uint8_t bg)
{
	const uint8_t fg = video_framebuffer[row * CHAR_H][col * CHAR_W];

	if (fg == bg)
		return false;
	for (int y = row * CHAR_H; y < (row + 1) * CHAR_H; y++)
		for (int x = col * CHAR_W; x < (col + 1) * CHAR_W; x++)
			if (video_framebuffer[y][x] != fg)
				return false;
	return true;
}

/* A chosen face draws the text, redraws what is already on screen, and
 * survives a reset; an empty one puts the built-in face back. */
static void
test_face(Terminal &t)
{
	QByteArray solid_x(4096, 0);
	for (int r = 0; r < 16; r++)
		solid_x['X' * 16 + r] = (char)0xFF;

	t.feed(QByteArray("\x05\x20\x0C", 3) + "X");
	settle(t);
	const uint8_t bg = video_framebuffer[FB_HEIGHT - 1][FB_WIDTH - 1];
	CHECK(!cell_solid(0, 0, bg));		/* Spleen's X */

	t.setFace(solid_x);			/* no new bytes */
	settle(t);
	CHECK(cell_solid(0, 0, bg));

	t.reset();
	t.feed(QByteArray("\x05\x20\x0C", 3) + "X");
	settle(t);
	CHECK(cell_solid(0, 0, bg));

	t.setFace({});
	settle(t);
	CHECK(!cell_solid(0, 0, bg));
	CHECK(!cell_blank(0, 0, bg));
}

/* minted asks SS.ScSiz for the screen size, which over serial is the Term
 * descriptor's 80x25, then draws each screen row as exactly 80 characters,
 * space-padded, back to back with no CR or LF: it relies on the terminal
 * wrapping at column 80. With the text area at 80x25 it must. */
static void
test_text_area_wraps_like_minted(Terminal &t)
{
	const int cols = 80, rows = 25;
	const int col0 = 0, row0 = 0;		/* the top left corner */

	t.setTextArea(cols, rows);
	t.reset();
	t.feed(QByteArray("\x05\x20\x0C", 3));	/* cursor off, clear */
	settle(t);
	const uint8_t bg = video_framebuffer[0][0];

	/* Three rows, as minted's _render writes them: a short line padded
	 * with spaces to the full width, then the next. */
	QByteArray screen;
	for (const char *line : { "#include <stdio.h>", "", "int main()" }) {
		QByteArray row(line);
		row += QByteArray(cols - row.size(), ' ');
		screen += row;
	}
	t.feed(screen);
	settle(t);

	CHECK(!cell_blank(col0, row0, bg));		/* '#' */
	CHECK(cell_blank(col0 + cols, row0, bg));	/* outside the window */
	CHECK(cell_blank(col0 + 18, row0, bg));	/* the padding */
	CHECK(!cell_blank(col0, row0 + 2, bg));	/* 'i' of int */
	/* On a 128-column line the third row would have started at column
	 * 160 % 128 = 32 of the second line, on the right of the screen. */
	CHECK(cell_blank(col0 + 32, row0 + 1, bg));

	/* Back to the whole panel for any later test. */
	t.setTextArea(TEXT_COLS, TEXT_ROWS);
	t.reset();
	settle(t);
}

static int bells;

/* The console bell ($07) from the board rings the platform's bell once. */
static void
test_bell(Terminal &t)
{
	fw_platform_set_bell([] { bells++; });
	t.feed(QByteArray("A\x07" "B", 3));
	settle(t);
	CHECK(bells == 1);
	fw_platform_set_bell(nullptr);
}

/* Write `count` numbered lines, "line 00" upwards, each ended CR LF. */
static void
numbered(Terminal &t, int first, int count)
{
	for (int i = first; i < first + count; i++)
		t.feed(QString("line %1\r\n").arg(i, 2, 10, QChar('0')).toLatin1());
	settle(t);
}

static QString
text_of(Terminal &t, qint64 n)
{
	return screentext_cells(t.lineCells(n), 0, TEXT_COLS - 1);
}

/* Lines scrolling off the top of the text area are kept, text and pixels,
 * up to the limit; deleting a line lower down is not scrolling. */
static void
test_scrollback(Terminal &t)
{
	t.setTextArea(80, 25);
	t.setScrollbackLimit(500);
	t.reset();
	t.feed(QByteArray("\x05\x20\x0C", 3));	/* cursor off, clear */
	settle(t);
	const qint64 base = t.linesScrolled();

	/* 30 lines in 25 rows: the first 24 newlines move down the screen,
	 * the last 6 scroll "line 00" to "line 05" off the top. */
	numbered(t, 0, 30);
	CHECK(t.linesScrolled() - base == 6);
	CHECK(t.historySize() == 6);
	CHECK(text_of(t, base) == "line 00");
	CHECK(text_of(t, base + 5) == "line 05");
	CHECK(text_of(t, base + 6) == "line 06");	/* the live top row */
	CHECK(t.linePixels(base + 6).isEmpty());	/* live, not held */

	/* A held line's pixels are what the panel showed for its text:
	 * draw "line 00" on the live screen again and compare. */
	const QByteArray kept = t.linePixels(base);
	CHECK(kept.size() == 80 * CHAR_W * CHAR_H);
	t.feed(QByteArray("\x01\x0B", 2) + "line 00");	/* home, erase line */
	settle(t);
	vidterm_refresh();
	bool same = kept.size() == 80 * CHAR_W * CHAR_H;
	for (int y = 0; same && y < CHAR_H; y++)
		same = memcmp(kept.constData() + y * 80 * CHAR_W,
		    video_framebuffer[y], 80 * CHAR_W) == 0;
	CHECK(same);

	/* Deleting a line below the top moves rows but loses nothing. */
	const qint64 before = t.linesScrolled();
	t.feed(QByteArray("\x02\x20\x25\x1F\x31", 5));	/* row 5, delete */
	settle(t);
	CHECK(t.linesScrolled() == before);

	/* The limit drops the oldest. */
	t.setScrollbackLimit(3);
	CHECK(t.historySize() == 3);
	CHECK(t.lineCells(base).isEmpty());
	CHECK(text_of(t, base + 3) == "line 03");

	/* 0 keeps nothing, though the lines are still counted. */
	t.setScrollbackLimit(0);
	t.feed(QByteArray("\x02\x20\x38", 3));	/* to row 24, the last */
	numbered(t, 30, 2);
	CHECK(t.historySize() == 0);
	CHECK(t.linesScrolled() == before + 2);

	/* The whole panel scrolls by the other path; it is kept too. */
	t.setScrollbackLimit(500);
	t.setTextArea(TEXT_COLS, TEXT_ROWS);
	t.reset();
	t.feed(QByteArray("\x05\x20\x0C", 3));
	numbered(t, 0, TEXT_ROWS + 2);
	CHECK(t.historySize() == 3);
	CHECK(text_of(t, t.linesScrolled() - 3) == "line 00");
	CHECK(t.lineCells(t.linesScrolled() - 3).size() == TEXT_COLS);

	t.setScrollbackLimit(0);
	t.reset();
	settle(t);
}

/* Graphics is its own plane: text scrolling over it moves the text, not the
 * drawing, and only a clear of the panel ($0C) erases it. */
static void
test_scroll_keeps_graphics(Terminal &t)
{
	t.setScrollbackLimit(0);
	t.feed(QByteArray("\x05\x20\x0C", 3));	/* cursor off, clear */
	settle(t);
	const uint8_t bg = video_framebuffer[FB_HEIGHT - 1][FB_WIDTH - 1];

	/* A bar well clear of the text that will scroll past it. */
	t.feed(point(0x40, 600, 200) + point(0x4A, 699, 299));
	settle(t);
	CHECK(video_framebuffer[250][650] != bg);

	numbered(t, 0, TEXT_ROWS + 5);			/* scrolls 6 times */
	vidterm_refresh();
	CHECK(video_framebuffer[250][650] != bg);
	CHECK(video_framebuffer[299][699] != bg);
	CHECK(video_framebuffer[250][701] == bg);

	t.feed(QByteArray("\x0C", 1));
	settle(t);
	vidterm_refresh();
	CHECK(video_framebuffer[250][650] == bg);

	/* Erased for good: text written over its place later does not bring
	 * it back. */
	numbered(t, 0, TEXT_ROWS + 1);
	vidterm_refresh();
	CHECK(video_framebuffer[250][650] == bg);
	t.reset();
	settle(t);
}

int
main()
{
	Terminal t;

	test_backlog_is_lossless(t);
	test_text(t);
	test_bar(t);
	test_face(t);
	test_text_area_wraps_like_minted(t);
	test_bell(t);
	test_scrollback(t);
	test_scroll_keeps_graphics(t);

	if (failures == 0)
		printf("test_terminal: all passed\n");
	return failures == 0 ? 0 : 1;
}
