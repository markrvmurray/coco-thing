/*
 * test_screen.cpp
 *
 * The panel outside the text area is shown grey, whatever the framebuffer
 * holds there, and exactly up to the area's edge at any scale.
 */

#include <cstdio>
#include <cstring>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>

#include "screen.h"
#include "terminal.h"
#include "vid_link.h"
#include "video.h"
#include "video_render.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static const QRgb RED = qRgb(255, 0, 0);	/* RGB332 $E0 */

static QImage
shot(Screen &s, int scale)
{
	s.resize(FB_WIDTH * scale, FB_HEIGHT * scale);
	return s.grab().toImage();
}

static bool
grey(const QImage &img, int x, int y)
{
	return img.pixel(x, y) == Screen::UNUSED.rgb();
}

/* A cell's centre in the widget, at whole-multiple scale k. */
static QPointF
centre(int col, int row, int k)
{
	return QPointF((col * CHAR_W + CHAR_W / 2) * k,
	    (row * CHAR_H + CHAR_H / 2) * k);
}

static void
mouse(Screen &s, QEvent::Type type, QPointF at)
{
	QMouseEvent ev(type, at, at, Qt::LeftButton,
	    type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
	    Qt::NoModifier);
	QApplication::sendEvent(&s, &ev);
}

static void
drag(Screen &s, QPointF from, QPointF to)
{
	mouse(s, QEvent::MouseButtonPress, from);
	mouse(s, QEvent::MouseMove, (from + to) / 2);
	mouse(s, QEvent::MouseMove, to);
	mouse(s, QEvent::MouseButtonRelease, to);
}

/* Text selected with the mouse, at 2x, over what the firmware drew. */
static void
test_selection(Screen &s)
{
	const int k = 2;
	Terminal t;
	int made = 0;

	QObject::connect(&s, &Screen::selectionMade, [&made] { made++; });
	t.feed(QByteArray("\x05\x20\x0C", 3));
	t.feed("hello world\r\nsecond line");
	for (int i = 0; i < 1000 && !(t.backlog() == 0 && vid_link_idle()); i++)
		t.pump(5);
	s.setTextArea(80, 25);

	const QImage before = shot(s, k);

	/* A click alone selects nothing. */
	drag(s, centre(3, 0, k), centre(3, 0, k));
	CHECK(!s.hasSelection());
	CHECK(made == 0);

	/* A drag across "world", either way round. */
	drag(s, centre(6, 0, k), centre(10, 0, k));
	CHECK(s.selectedText() == "world");
	CHECK(made == 1);
	drag(s, centre(10, 0, k), centre(6, 0, k));
	CHECK(s.selectedText() == "world");
	CHECK(made == 2);

	/* Highlighted in reverse video: the selected cells, and no others. */
	const QImage after = shot(s, k);
	CHECK(after.pixel(centre(8, 0, k).toPoint()) !=
	    before.pixel(centre(8, 0, k).toPoint()));
	CHECK(after.pixel(centre(5, 0, k).toPoint()) ==
	    before.pixel(centre(5, 0, k).toPoint()));
	CHECK(after.pixel(centre(11, 0, k).toPoint()) ==
	    before.pixel(centre(11, 0, k).toPoint()));

	/* Across rows; a drag beyond the text area stops at its edge. */
	drag(s, centre(6, 0, k), centre(5, 1, k));
	CHECK(s.selectedText() == "world\nsecond");
	/* Past the right edge and the bottom of the panel: clamped to the
	 * area's last cell, row 24, so rows 1-24 -- 23 line breaks. */
	drag(s, centre(0, 1, k), QPointF(FB_WIDTH * k - 1, FB_HEIGHT * k - 1));
	CHECK(s.selectedText() == "second line" + QString(23, '\n'));

	/* A double-click selects the word, and copies like a drag. */
	const int was = made;
	mouse(s, QEvent::MouseButtonPress, centre(8, 0, k));
	mouse(s, QEvent::MouseButtonRelease, centre(8, 0, k));
	mouse(s, QEvent::MouseButtonDblClick, centre(8, 0, k));
	mouse(s, QEvent::MouseButtonRelease, centre(8, 0, k));
	CHECK(s.selectedText() == "world");
	CHECK(made == was + 1);

	/* On a blank, nothing. */
	mouse(s, QEvent::MouseButtonPress, centre(5, 0, k));
	mouse(s, QEvent::MouseButtonRelease, centre(5, 0, k));
	mouse(s, QEvent::MouseButtonDblClick, centre(5, 0, k));
	mouse(s, QEvent::MouseButtonRelease, centre(5, 0, k));
	CHECK(!s.hasSelection());
	CHECK(made == was + 1);

	s.selectAll();
	CHECK(s.selectedText().startsWith("hello world\nsecond line\n"));
	s.clearSelection();
	CHECK(!s.hasSelection());
}

int
main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	Screen s;

	/* Every framebuffer pixel red, so anything grey came from the screen. */
	memset(video_framebuffer, 0xE0, sizeof(video_framebuffer));
	s.setScaling(true, false);

	/* The whole panel: nothing grey. */
	QImage img = shot(s, 1);
	CHECK(img.pixel(900, 100) == RED);
	CHECK(img.pixel(1023, 479) == RED);

	/* 80x25: 640x400 pixels in the top left corner, the rest grey. */
	s.setTextArea(80, 25);
	for (int k = 1; k <= 2; k++) {
		img = shot(s, k);
		CHECK(img.pixel(0, 0) == RED);
		CHECK(img.pixel(639 * k, 399 * k) == RED);
		CHECK(grey(img, 640 * k, 0));		/* right of it */
		CHECK(grey(img, 0, 400 * k));		/* below it */
		CHECK(grey(img, 1023 * k, 479 * k));
	}

	s.setTextArea(TEXT_COLS, TEXT_ROWS);
	test_selection(s);

	if (failures == 0)
		printf("test_screen: all passed\n");
	return failures == 0 ? 0 : 1;
}
