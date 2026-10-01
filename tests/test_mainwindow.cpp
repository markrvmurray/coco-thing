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
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>

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
 * the panel fitted to the widget with its aspect ratio kept, centred. */
static QPointF
centre(QWidget *screen, int col, int row)
{
	const QSize fit = QSize(FB_WIDTH, FB_HEIGHT).scaled(screen->size(),
	    Qt::KeepAspectRatio);
	const qreal k = (qreal)fit.width() / FB_WIDTH;

	return QPointF((screen->width() - fit.width()) / 2 +
	    (col * CHAR_W + CHAR_W / 2) * k,
	    (screen->height() - fit.height()) / 2 +
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

	/* Settings of its own, so the user's are never read or touched. */
	QApplication::setOrganizationName("coco-thing-test");
	QApplication::setApplicationName("test_mainwindow");
	QSettings().clear();

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

	/* Destroyed with the screen holding the keyboard. */
	delete w;

	QSettings().clear();
	close(board);
	if (failures == 0)
		printf("test_mainwindow: all passed\n");
	return failures == 0 ? 0 : 1;
}
