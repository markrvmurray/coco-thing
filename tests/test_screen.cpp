/*
 * test_screen.cpp
 *
 * Only the text area is shown, filling the widget at any scale; the rest of
 * the panel, whatever the framebuffer holds there, is not.
 */

#include <cstdio>
#include <cstring>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>

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

static const QRgb BLUE = qRgb(0, 0, 255);	/* RGB332 $03 */
static const QRgb BLACK = qRgb(0, 0, 0);

/* The screen at `scale` times its text area's size. */
static QImage
shot(Screen &s, int scale)
{
	s.resize(s.sizeHint() * scale);
	return s.grab().toImage();
}

/* How many pixels of the image are `colour`. */
static int
count(const QImage &img, QRgb colour)
{
	int n = 0;

	for (int y = 0; y < img.height(); y++)
		for (int x = 0; x < img.width(); x++)
			n += img.pixel(x, y) == colour;
	return n;
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

	s.setTerminal(&t);
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
	s.setTerminal(nullptr);
}

/* RGB332 as the screen shows it: high bits replicated into the low. */
static QRgb
rgb332(uint8_t i)
{
	const unsigned r3 = (i >> 5) & 7, g3 = (i >> 2) & 7, b2 = i & 3;

	return qRgb((r3 << 5) | (r3 << 2) | (r3 >> 1),
	    (g3 << 5) | (g3 << 2) | (g3 >> 1),
	    (b2 << 6) | (b2 << 4) | (b2 << 2) | b2);
}

static void
settle(Terminal &t)
{
	for (int i = 0; i < 10000 && !(t.backlog() == 0 && vid_link_idle()); i++)
		t.pump(5);
}

static void
numbered(Terminal &t, int first, int count)
{
	for (int i = first; i < first + count; i++)
		t.feed(QString("line %1\r\n").arg(i, 2, 10, QChar('0')).toLatin1());
	settle(t);
}

/* Scrolled back, the text area shows held lines as they looked; the view
 * stays on them as output arrives; selections reach into the history and
 * keep to their text. */
static void
test_scrollback(Screen &s)
{
	const int k = 1;
	Terminal t;

	t.setTextArea(80, 25);
	t.setScrollbackLimit(500);
	t.reset();
	t.feed(QByteArray("\x05\x20\x0C", 3));
	numbered(t, 0, 30);			/* lines 00-05 held */
	s.setTextArea(80, 25);
	s.setTerminal(&t);
	const qint64 held_first = t.linesScrolled() - t.historySize();

	CHECK(s.scrolledBack() == 0);
	s.scrollBy(3);
	CHECK(s.scrolledBack() == 3);

	/* The top row now shows the held "line 03", pixel for pixel. */
	const QImage img = shot(s, k);
	const QByteArray px = t.linePixels(held_first + 3);
	bool same = px.size() == 640 * CHAR_H;
	int lit = 0;
	for (int y = 0; same && y < CHAR_H; y++)
		for (int x = 0; same && x < 640; x++) {
			const uint8_t v = (uint8_t)px[y * 640 + x];
			same = img.pixel(x, y) == rgb332(v);
			lit += v != (uint8_t)px[0];
		}
	CHECK(same);
	CHECK(lit > 0);			/* there was text to compare */
	/* Row 3 of the view is the live screen's row 0, "line 06". */
	mouse(s, QEvent::MouseButtonPress, centre(0, 3, k));
	mouse(s, QEvent::MouseButtonRelease, centre(0, 3, k));
	mouse(s, QEvent::MouseButtonDblClick, centre(5, 3, k));
	mouse(s, QEvent::MouseButtonRelease, centre(5, 3, k));
	CHECK(s.selectedText() == "06");

	/* A drag in the history copies history text. */
	drag(s, centre(0, 0, k), centre(6, 1, k));
	CHECK(s.selectedText() == "line 03\nline 04");

	/* New output: the view stays on the same lines, and so does the
	 * selection. */
	numbered(t, 30, 2);
	CHECK(s.scrolledBack() == 5);
	CHECK(s.selectedText() == "line 03\nline 04");

	/* Clamped at both ends; the wheel moves three lines a notch. */
	s.scrollBy(1000);
	CHECK(s.scrolledBack() == t.historySize());
	s.scrollToBottom();
	CHECK(s.scrolledBack() == 0);
	s.scrollBy(-5);
	CHECK(s.scrolledBack() == 0);
	QWheelEvent up(QPointF(10, 10), QPointF(10, 10), QPoint(),
	    QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
	    false);
	QApplication::sendEvent(&s, &up);
	CHECK(s.scrolledBack() == 3);

	s.setTerminal(nullptr);
	s.setTextArea(TEXT_COLS, TEXT_ROWS);
}

int
main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	Screen s;

	/* Red inside an 80x25 text area, blue on the rest of the panel. */
	memset(video_framebuffer, 0x03, sizeof(video_framebuffer));
	for (int y = 0; y < 25 * CHAR_H; y++)
		memset(video_framebuffer[y], 0xE0, 80 * CHAR_W);
	s.setScaling(true, false);

	/* The whole panel: both shown. */
	QImage img = shot(s, 1);
	CHECK(img.size() == QSize(FB_WIDTH, FB_HEIGHT));
	CHECK(img.pixel(100, 100) == RED);
	CHECK(img.pixel(900, 100) == BLUE);

	/* 80x25: the text area fills the widget, and nothing outside it is
	 * shown, at 1x and 2x. */
	s.setTextArea(80, 25);
	CHECK(s.sizeHint() == QSize(640, 400));
	for (int k = 1; k <= 2; k++) {
		img = shot(s, k);
		CHECK(count(img, RED) == img.width() * img.height());
		CHECK(count(img, BLUE) == 0);
	}

	/* A wider widget: the area keeps its shape, anchored at the top
	 * left -- no margin there -- with the spare space at the right. */
	s.resize(640 * 2 + 200, 400 * 2);
	img = s.grab().toImage();
	CHECK(img.pixel(0, 0) == RED);
	CHECK(img.pixel(0, 400) == RED);
	CHECK(img.pixel(640 * 2 - 1, 400) == RED);
	CHECK(img.pixel(640 * 2 + 1, 400) == BLACK);
	CHECK(count(img, BLUE) == 0);

	/* The size with no spare space: the area's shape at that height, or a
	 * whole multiple of the area under whole-multiple scaling. */
	s.setScaling(false, false);
	CHECK(s.fittedSize(800) == QSize(1280, 800));
	CHECK(s.fittedSize(500) == QSize(800, 500));
	s.setScaling(true, false);
	CHECK(s.fittedSize(800) == QSize(1280, 800));
	CHECK(s.fittedSize(799) == QSize(640, 400));
	CHECK(s.fittedSize(100) == QSize(640, 400));

	s.setTextArea(TEXT_COLS, TEXT_ROWS);
	test_selection(s);
	test_scrollback(s);

	if (failures == 0)
		printf("test_screen: all passed\n");
	return failures == 0 ? 0 : 1;
}
