/*
 * test_mainwindow.cpp
 *
 * The window as the app builds it, over a pty standing in for the board:
 * keys typed at the screen arrive at the board as the firmware's cooker
 * makes them, and the window can be destroyed while the screen has the
 * keyboard (which once reached a destroyed member from focusOutEvent).
 */

#include <cstdio>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <Carbon/Carbon.h>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>

#include "fontface.h"
#include "mainwindow.h"
#include "screen.h"
#include "video.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static void
spin(int ms)
{
	QElapsedTimer t;

	t.start();
	while (t.elapsed() < ms)
		QApplication::processEvents(QEventLoop::AllEvents, 10);
}

static void
key(QWidget *w, unsigned code, Qt::KeyboardModifiers mods)
{
	QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, mods, 0, code, 0);
	QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, mods, 0, code, 0);

	QApplication::sendEvent(w, &press);
	QApplication::sendEvent(w, &release);
}

static QAction *
action(QWidget *w, const QString &text)
{
	for (QAction *a : w->findChildren<QAction *>())
		if (a->text() == text)
			return a;
	return nullptr;
}

/* Cell (col, row)'s centre in the screen widget, by the screen's own rule:
 * the text area (its size hint) fitted to the widget with its aspect ratio
 * kept, anchored at the top left. */
static QPointF
centre(QWidget *screen, int col, int row)
{
	const QSize area = screen->sizeHint();
	const QSize fit = area.scaled(screen->size(), Qt::KeepAspectRatio);
	const qreal k = (qreal)fit.width() / area.width();

	return QPointF((col * CHAR_W + CHAR_W / 2) * k,
	    (row * CHAR_H + CHAR_H / 2) * k);
}

static void
mouse(QWidget *w, QEvent::Type type, QPointF at)
{
	QMouseEvent ev(type, at, at, Qt::LeftButton,
	    type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
	    Qt::NoModifier);
	QApplication::sendEvent(w, &ev);
}

static QByteArray
drain(int fd)
{
	QByteArray out;
	char buf[256];
	ssize_t n;

	while ((n = read(fd, buf, sizeof(buf))) > 0)
		out.append(buf, (int)n);
	return out;
}

int
main(int argc, char *argv[])
{
	QApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
	QApplication app(argc, argv);

	/* Settings of its own, in a directory that goes when the test does:
	 * the user's are never read or touched, and nothing is left in their
	 * Library. */
	QTemporaryDir settings_dir;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
	    settings_dir.path());
	QApplication::setOrganizationName("coco-thing-test");
	QApplication::setApplicationName("test_mainwindow");

	/* A face of solid cells, as the font in Preferences, so the tab's
	 * title can be told apart from one in the built-in face. */
	const QString face_path = settings_dir.filePath("solid.fnt");
	{
		QFile f(face_path);
		CHECK(f.open(QIODevice::WriteOnly));
		CHECK(f.write(QByteArray(FONTFACE_BYTES, (char)0xFF)) ==
		    FONTFACE_BYTES);
	}
	QSettings().setValue("font/source", 2);	/* Prefs::FontFile */
	QSettings().setValue("font/file", face_path);

	/* A window saved far wider than the text's shape, as one saved when
	 * the whole panel was shown would be. */
	{
		QWidget wide;
		wide.resize(1468, 860);
		QSettings().setValue("window/geometry", wide.saveGeometry());
	}

	const int board = posix_openpt(O_RDWR | O_NOCTTY);
	if (board < 0 || grantpt(board) != 0 || unlockpt(board) != 0) {
		perror("pty");
		return 2;
	}
	fcntl(board, F_SETFL, O_NONBLOCK);

	MainWindow *w = new MainWindow(ptsname(board));
	w->show();
	spin(200);
	Screen *screen = w->findChild<Screen *>();
	CHECK(screen != nullptr);
	screen->setFocus();
	spin(50);

	key(screen, kVK_ANSI_H, Qt::NoModifier);
	key(screen, kVK_ANSI_I, Qt::ShiftModifier);
	key(screen, kVK_Return, Qt::NoModifier);
	key(screen, kVK_UpArrow, Qt::ControlModifier);
	key(screen, kVK_F2, Qt::NoModifier);
	key(screen, kVK_ANSI_3, Qt::ShiftModifier);
	key(screen, kVK_ANSI_Q, Qt::MetaModifier);	/* the Mac's, not sent */
	spin(300);

	char buf[64];
	const ssize_t n = read(board, buf, sizeof(buf));
	CHECK(QByteArray(buf, n > 0 ? (int)n : 0) ==
	    QByteArray("hI\r\x13\x81\x9C", 6));

	/* Select All copies what the board drew to the pasteboard. */
	const char hello[] = "\x0C" "hello from the board\r\n";
	(void)write(board, hello, sizeof(hello) - 1);
	spin(200);
	QAction *select_all = action(w, "Select &All");
	CHECK(select_all != nullptr);
	if (select_all != nullptr)
		select_all->trigger();
	CHECK(QApplication::clipboard()->text().startsWith(
	    "hello from the board\n"));

	/* Selecting with the mouse copies, with no menu involved. */
	QApplication::clipboard()->clear();
	/* "hello from the board": "from the" is columns 6-13. */
	mouse(screen, QEvent::MouseButtonPress, centre(screen, 6, 0));
	mouse(screen, QEvent::MouseMove, centre(screen, 13, 0));
	mouse(screen, QEvent::MouseButtonRelease, centre(screen, 13, 0));
	CHECK(QApplication::clipboard()->text() == "from the");

	/* So does a double-click: "board" is columns 15-19. */
	QApplication::clipboard()->clear();
	mouse(screen, QEvent::MouseButtonPress, centre(screen, 17, 0));
	mouse(screen, QEvent::MouseButtonRelease, centre(screen, 17, 0));
	mouse(screen, QEvent::MouseButtonDblClick, centre(screen, 17, 0));
	mouse(screen, QEvent::MouseButtonRelease, centre(screen, 17, 0));
	CHECK(QApplication::clipboard()->text() == "board");

	/* Paste reaches the board with CR line ends, paced over frames. */
	const QString text = "echo hi\nls\n" + QString(100, 'x');
	const QByteArray expect = "echo hi\rls\r" + QByteArray(100, 'x');
	QApplication::clipboard()->setText(text);
	QAction *paste = action(w, "&Paste");
	CHECK(paste != nullptr);
	if (paste != nullptr)
		paste->trigger();
	spin(20);
	QByteArray got = drain(board);
	CHECK(got.size() < expect.size());	/* not all at once */
	spin(500);
	got += drain(board);
	CHECK(got == expect);

	/* Scroll-back: enough output to scroll, then the bar and the view
	 * follow each other, and typing returns to the live screen. */
	QByteArray lines;
	for (int i = 0; i < 40; i++)
		lines += QString("out %1\r\n").arg(i).toLatin1();
	(void)write(board, lines.constData(), lines.size());
	spin(300);
	QScrollBar *bar = w->findChild<QScrollBar *>();
	CHECK(bar != nullptr);
	CHECK(screen->scrolledBack() == 0);
	if (bar != nullptr) {
		CHECK(bar->maximum() > 5);
		CHECK(bar->value() == bar->maximum());	/* live */
		screen->scrollBy(5);
		spin(50);
		CHECK(bar->value() == bar->maximum() - 5);
		bar->setValue(bar->maximum() - 2);
		CHECK(screen->scrolledBack() == 2);
	}
	key(screen, kVK_Shift, Qt::ShiftModifier);	/* no bytes: stays */
	CHECK(screen->scrolledBack() == 2);
	key(screen, kVK_ANSI_X, Qt::NoModifier);
	CHECK(screen->scrolledBack() == 0);
	drain(board);

	/* The window fits the text: no spare space beside it, at start-up
	 * (whatever size was saved) and after a resize, on request. */
	CHECK(screen->size() == screen->fittedSize(screen->height()));
	w->resize(w->width() + 300, w->height());
	spin(50);
	CHECK(screen->size() != screen->fittedSize(screen->height()));
	QAction *fit = action(w, "Fit Window to Text");
	CHECK(fit != nullptr);
	if (fit != nullptr)
		fit->trigger();
	spin(50);
	CHECK(screen->size() == screen->fittedSize(screen->height()));

	/* The frame's tab names the port, and says when it is closed. */
	QTabWidget *tabs = w->findChild<QTabWidget *>();
	CHECK(tabs != nullptr && tabs->count() == 1);
	QLabel *title = w->findChild<QLabel *>("tabTitle");
	CHECK(title != nullptr);
	if (tabs != nullptr && title != nullptr) {
		const QString dev = QFileInfo(ptsname(board)).fileName();
		CHECK(title->accessibleName() == dev);
		CHECK(tabs->tabToolTip(0) == QString(ptsname(board)));
		CHECK(tabs->tabBar()->tabButton(0, QTabBar::LeftSide) == title);
		CHECK(title->isVisible());

		/* Drawn in the screen's face, 8x16 a character, inked where
		 * the face's glyphs are and nowhere else -- and smaller than a
		 * row of the screen. */
		const QPixmap pm = title->pixmap();
		const QSize logical = pm.deviceIndependentSize().toSize();
		CHECK(logical == QSize(dev.size() * 8, 16));
		CHECK(16 < screen->height() / 25);
		QString error;
		const QImage want = fontface_sample(
		    fontface_load(face_path, &error), dev,
		    qRgb(255, 255, 255), qRgba(0, 0, 0, 0));
		const QImage got = pm.toImage().scaled(want.size());
		bool same = !want.isNull();
		for (int y = 0; same && y < want.height(); y++)
			for (int x = 0; same && x < want.width(); x++)
				same = (qAlpha(want.pixel(x, y)) > 0) ==
				    (qAlpha(got.pixel(x, y)) > 0);
		CHECK(same);

		CHECK(!tabs->tabBar()->hasFocus() && screen->hasFocus());
		QAction *disconnect = action(w, "&Disconnect");
		CHECK(disconnect != nullptr);
		if (disconnect != nullptr)
			disconnect->trigger();
		spin(50);
		CHECK(title->accessibleName() == "Not connected");
		CHECK(title->pixmap().deviceIndependentSize().toSize() ==
		    QSize(13 * 8, 16));
	}

	/* Destroyed with the screen holding the keyboard. */
	delete w;

	close(board);
	if (failures == 0)
		printf("test_mainwindow: all passed\n");
	return failures == 0 ? 0 : 1;
}
