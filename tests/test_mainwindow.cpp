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
#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QSettings>

#include "mainwindow.h"
#include "screen.h"

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

	/* Destroyed with the screen holding the keyboard. */
	delete w;

	QSettings().clear();
	close(board);
	if (failures == 0)
		printf("test_mainwindow: all passed\n");
	return failures == 0 ? 0 : 1;
}
