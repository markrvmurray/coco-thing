/*
 * test_terminal.cpp
 *
 * Drives Terminal with no board attached: bytes in as the serial link would
 * deliver them, the firmware's own framebuffer and command trace as oracles.
 */

#include <cstdio>
#include <cstring>

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

int
main()
{
	Terminal t;

	test_backlog_is_lossless(t);
	test_text(t);
	test_bar(t);

	if (failures == 0)
		printf("test_terminal: all passed\n");
	return failures == 0 ? 0 : 1;
}
