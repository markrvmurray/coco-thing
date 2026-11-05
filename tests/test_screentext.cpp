/*
 * test_screentext.cpp
 *
 * Text off the firmware's screen for the pasteboard, and pasted text into
 * the bytes the guest would have been typed.
 */

#include <cstdio>

#include "screentext.h"
#include "terminal.h"
#include "video.h"
#include "vid_link.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static void
settle(Terminal &t)
{
	for (int i = 0; i < 100000; i++) {
		t.pump(5);
		if (t.backlog() == 0 && vid_link_idle())
			return;
	}
	failures++;
}

static QByteArray
bytes(std::initializer_list<int> b)
{
	QByteArray out;
	for (int v : b)
		out.append((char)v);
	return out;
}

/* A line's cells as text: CP437 to Unicode, blanks inside kept, trailing
 * blanks dropped. Lines come from Terminal::lineCells(). */
static void
test_cells(Terminal &t)
{
	const int right = TEXT_COLS - 1;

	t.feed(QByteArray("\x05\x20\x0C", 3));		/* cursor off, clear */
	t.feed("hello world\r\n");
	t.feed("  second line   \r\n");
	t.feed(QByteArray("\x9C" "5 \xAA" "x", 5));	/* CP437 pound and not */
	settle(t);

	const qint64 top = t.linesScrolled();
	CHECK(screentext_cells(t.lineCells(top), 0, right) == "hello world");
	CHECK(screentext_cells(t.lineCells(top), 6, 10) == "world");
	CHECK(screentext_cells(t.lineCells(top + 1), 0, right) ==
	    "  second line");
	CHECK(screentext_cells(t.lineCells(top + 1), 0, 5) == "  seco");
	CHECK(screentext_cells(t.lineCells(top + 2), 0, right) ==
	    "\u00A35 \u00ACx");
	CHECK(screentext_cells(t.lineCells(top + 3), 0, right).isEmpty());
	/* A line beyond the area does not exist. */
	CHECK(t.lineCells(top + TEXT_ROWS).isEmpty());
}

/* Words run between blanks, so a path or an option is one word; the
 * area's edges bound them. */
static void
test_word(Terminal &t)
{
	const int right = TEXT_COLS - 1;
	int from = -1, to = -1;

	t.feed(QByteArray("\x0C", 1));
	t.feed("ls /dd/cmds -e=foo  x");
	settle(t);
	const QByteArray line = t.lineCells(t.linesScrolled());

	CHECK(screentext_word(line, 5, 0, right, &from, &to));
	CHECK(from == 3 && to == 10);			/* /dd/cmds */
	CHECK(screentext_word(line, 0, 0, right, &from, &to));
	CHECK(from == 0 && to == 1);			/* ls */
	CHECK(screentext_word(line, 15, 0, right, &from, &to));
	CHECK(from == 12 && to == 17);			/* -e=foo */
	CHECK(screentext_word(line, 20, 0, right, &from, &to));
	CHECK(from == 20 && to == 20);			/* x */

	from = to = -1;
	CHECK(!screentext_word(line, 2, 0, right, &from, &to));	/* a blank */
	CHECK(!screentext_word(line, 19, 0, right, &from, &to));
	CHECK(!screentext_word(line, 40, 0, right, &from, &to));	/* empty */
	CHECK(from == -1 && to == -1);

	/* Bounded by the area: columns 5..8 only. */
	CHECK(screentext_word(line, 6, 5, 8, &from, &to));
	CHECK(from == 5 && to == 8);
}

static void
test_paste()
{
	CHECK(screentext_paste("ls -l\n") == "ls -l\r");
	CHECK(screentext_paste("a\r\nb\rc\nd") == "a\rb\rc\rd");
	CHECK(screentext_paste("\r\n\r\n") == "\r\r");
	CHECK(screentext_paste(QString::fromUtf8("£¬é")) ==
	    bytes({ 0x9C, 0xAA, 0x82 }));
	CHECK(screentext_paste(QString::fromUtf8("€")) == "?");
	CHECK(screentext_paste("a\tb") == "a\tb");
	/* Control characters are dropped, never sent: an ESC would start a
	 * command on the board. */
	CHECK(screentext_paste(QString("x\x1B[2Jy\x07z")) == "x[2Jyz");
}

int
main()
{
	Terminal t;

	test_cells(t);
	test_word(t);
	test_paste();

	if (failures == 0)
		printf("test_screentext: all passed\n");
	return failures == 0 ? 0 : 1;
}
