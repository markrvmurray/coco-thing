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
#include <termios.h>
#include <unistd.h>

#include <Carbon/Carbon.h>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>


#include "connectdialog.h"
#include "fontface.h"
#include "linesettingsbox.h"
#include "mainwindow.h"
#include "prefs.h"
#include "prefsdialog.h"
#include "screen.h"
#include "trafficpane.h"
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

/* Settings are stored only while they differ from the defaults, so a value
 * the user never chose follows a new version's default: 80x25 saved by a
 * version whose default it was must not pin the text area for good. */
static void
test_prefs_keep_only_changes()
{
	QSettings().clear();
	Prefs p;
	p.save();
	CHECK(QSettings().allKeys().isEmpty());

	p.text_cols = 80;
	p.text_rows = 25;
	p.save();
	CHECK(QSettings().value("display/text_cols").toInt() == 80);
	CHECK(Prefs::load().text_rows == 25);

	/* Back at the default, the setting is forgotten, not stored. */
	p.text_cols = Prefs().text_cols;
	p.text_rows = Prefs().text_rows;
	p.save();
	CHECK(!QSettings().contains("display/text_cols"));
	CHECK(!QSettings().contains("display/text_rows"));
	CHECK(Prefs::load().text_cols == Prefs().text_cols);
	QSettings().clear();
}

/* Connect's list is editable, starts on the preferred port, and shows the
 * chosen line white on blue -- in the box, its line and its drop-down --
 * whether or not the dialog has the focus; and what it returns is what
 * the box holds, typed or chosen. */
static void
test_connect_dialog()
{
	const QStringList names = { "Loopback", "cu.usbmodem1101",
	    "cu.usbserial-A1", "/tmp/usim-console" };
	LineSettings kept;
	kept.baud = 115200;
	kept.parity = QSerialPort::EvenParity;
	kept.stop_bits = QSerialPort::TwoStop;
	kept.flow = LineSettings::FlowRtsCts;
	{
		QSettings s;
		kept.save(s, "cu.usbserial-A1");
	}

	ConnectDialog d(names, 3);
	QComboBox *box = d.findChild<QComboBox *>("port");
	QComboBox *baud = d.findChild<QComboBox *>("baud");
	QComboBox *flow = d.findChild<QComboBox *>("flow");
	QLabel *note = d.findChild<QLabel *>("lineNote");
	LineSettingsBox *line = d.findChild<LineSettingsBox *>();
	CHECK(box != nullptr && baud != nullptr && flow != nullptr &&
	    note != nullptr && line != nullptr);
	if (box == nullptr || baud == nullptr || flow == nullptr ||
	    note == nullptr || line == nullptr)
		return;

	CHECK(box->count() == names.size());
	CHECK(box->isEditable() && box->lineEdit() != nullptr);
	CHECK(d.port() == "/tmp/usim-console");
	const QList<QWidget *> parts = { box, box->lineEdit(), box->view() };
	for (QWidget *w : parts) {
		for (QPalette::ColorGroup g :
		    { QPalette::Active, QPalette::Inactive }) {
			const QPalette p = w->palette();
			CHECK(p.color(g, QPalette::HighlightedText) ==
			    QColor(Qt::white));
			const QColor back = p.color(g, QPalette::Highlight);
			CHECK(back.lightness() < 128 && back.blue() > back.red()
			    && back.blue() > back.green());
		}
	}
	/* A port with nothing kept gets the defaults, to be chosen. */
	CHECK(d.lineSettings() == LineSettings());
	CHECK(line->applicable() && baud->isEnabled() && note->isHidden());

	/* Choosing a port shows what was kept for it. */
	box->setCurrentIndex(2);
	CHECK(d.port() == "cu.usbserial-A1");
	CHECK(d.lineSettings() == kept);
	CHECK(baud->currentText() == "115200");
	CHECK(flow->currentText() == "RTS/CTS");

	/* Any rate may be typed; one not yet a number leaves the last. */
	baud->setEditText("31250");
	CHECK(d.lineSettings().baud == 31250);
	baud->setEditText("");
	CHECK(d.lineSettings().baud == kept.baud);

	/* The loopback plug takes no settings, and says so. */
	box->setCurrentIndex(0);
	CHECK(!line->applicable() && !baud->isEnabled() && !flow->isEnabled());
	CHECK(!note->isHidden() && note->text().contains("loopback"));

	/* A typed path; back to a port that takes settings. */
	box->lineEdit()->setText("~/usim-console");
	CHECK(d.port() == QDir::homePath() + "/usim-console");
	CHECK(line->applicable() && baud->isEnabled() && note->isHidden());
	CHECK(d.lineSettings() == LineSettings());

	QSettings().remove("ports");
}

/* Preferences edits the line settings of the port in use, and has none to
 * edit with no port. */
static void
test_prefs_line_settings()
{
	LineSettings l;
	l.baud = 4800;
	l.data_bits = QSerialPort::Data7;

	PrefsDialog with(Prefs(), "/dev/cu.usbserial-A1", l);
	LineSettingsBox *box = with.findChild<LineSettingsBox *>();
	QGroupBox *group = with.findChild<QGroupBox *>("lineGroup");
	CHECK(box != nullptr && group != nullptr);
	if (box == nullptr || group == nullptr)
		return;
	CHECK(with.lineSettings() == l && box->applicable());
	CHECK(group->title() == "Line settings for cu.usbserial-A1");
	QComboBox *flow = with.findChild<QComboBox *>("flow");
	CHECK(flow != nullptr);
	if (flow != nullptr)
		flow->setCurrentIndex(flow->findText("RTS/CTS"));
	CHECK(with.lineSettings().flow == LineSettings::FlowRtsCts);

	PrefsDialog without{ Prefs() };
	box = without.findChild<LineSettingsBox *>();
	CHECK(box != nullptr && !box->applicable());

	PrefsDialog loopback(Prefs(), Link::LOOPBACK);
	box = loopback.findChild<LineSettingsBox *>();
	CHECK(box != nullptr && !box->applicable());
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

	test_prefs_keep_only_changes();
	test_connect_dialog();
	test_prefs_line_settings();

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

	/* The board's port has settings kept for it, which the window opens
	 * it with at start-up and shows. */
	LineSettings line;
	line.baud = 2400;
	line.parity = QSerialPort::EvenParity;
	line.stop_bits = QSerialPort::TwoStop;
	{
		QSettings s;
		line.save(s, ptsname(board));
	}

	MainWindow *w = new MainWindow(ptsname(board));
	w->show();
	spin(200);
	{
		struct termios t = {};
		CHECK(tcgetattr(board, &t) == 0);
		CHECK(cfgetospeed(&t) == 2400);
		CHECK((t.c_cflag & (PARENB | PARODD | CSTOPB | CSIZE)) ==
		    (PARENB | CSTOPB | CS8));
		QLabel *state = w->findChild<QLabel *>("linkState");
		CHECK(state != nullptr &&
		    state->text().endsWith("(2400 8E2)"));
	}
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
	    QByteArray("hI\r\x94\x81\x9C", 6));

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
	QScrollBar *bar = w->findChild<QScrollBar *>("screenScrollBar");
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

	/* The traffic pane: hidden until asked for, then the link's bytes
	 * both ways as `hexdump -C` shows them, from before it opened too,
	 * each byte green from the board and red to it. Showing and hiding it
	 * leaves the screen's size alone. */
	TrafficPane *pane = w->findChild<TrafficPane *>();
	QAction *show_traffic = action(w, "Show &Traffic");
	CHECK(pane != nullptr && show_traffic != nullptr);
	if (pane != nullptr && show_traffic != nullptr) {
		CHECK(!pane->isVisible());
		const QSize before = screen->size();
		show_traffic->trigger();
		spin(100);
		CHECK(pane->isVisible());
		CHECK(screen->size() == before);
		CHECK(screen->hasFocus());
		/* Beside the screen, wide enough for a whole line. */
		CHECK(pane->x() > screen->mapTo(w, QPoint(0, 0)).x() +
		    screen->width());
		CHECK(pane->width() >= pane->lineWidth());

		/* The first keys typed, and the board's greeting. */
		CHECK(pane->text().startsWith("00000000  68 49 0d 94 81 9c "));
		CHECK(pane->text().contains("|hI.....hello fro|"));

		/* Each byte coloured by its direction: the keys red, the
		 * board's reply green. */
		QPlainTextEdit *view = pane->findChild<QPlainTextEdit *>();
		CHECK(view != nullptr);
		int to = 0, from = 0;
		for (QTextBlock b = view != nullptr ? view->document()->begin()
		    : QTextBlock(); b.isValid(); b = b.next()) {
			for (QTextBlock::iterator it = b.begin(); !it.atEnd();
			    ++it) {
				const QTextFragment f = it.fragment();
				const QColor c = f.charFormat().foreground()
				    .color();
				if (f.text() == "68" && to == 0) {	/* h */
					CHECK(c.red() > c.green());
					to++;
				}
				if (f.text().contains("hello") && from == 0) {
					CHECK(c.green() > c.red());
					from++;
				}
			}
		}
		CHECK(to == 1 && from == 1);

		/* New traffic shows on the next frame. */
		(void)write(board, "late news\r\n", 11);
		spin(200);
		CHECK(pane->text().contains("late news"));
		CHECK(pane->text().trimmed() == pane->log().text().trimmed());

		/* Saved as Python, or as a C header for a .h name. */
		QString error;
		const QString py = settings_dir.filePath("traffic.py");
		CHECK(pane->saveTo(py, &error));
		QFile f(py);
		CHECK(f.open(QIODevice::ReadOnly));
		CHECK(QString::fromUtf8(f.readAll()) == pane->log().python());
		const QString h = settings_dir.filePath("traffic.h");
		CHECK(pane->saveTo(h, &error));
		QFile g(h);
		CHECK(g.open(QIODevice::ReadOnly));
		CHECK(QString::fromUtf8(g.readAll()) == pane->log().cHeader());

		show_traffic->trigger();
		spin(100);
		CHECK(!pane->isVisible());
		CHECK(screen->size() == before);
	}

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

		/* White on the selected tab's blue while the window is
		 * active; the style's text colour on its grey otherwise. */
		const QColor want_ink = w->isActiveWindow() ? QColor(Qt::white)
		    : tabs->tabBar()->palette().color(QPalette::WindowText);
		const QImage ink = pm.toImage();
		int inked = 0;
		bool right_ink = true;
		for (int y = 0; y < ink.height(); y++)
			for (int x = 0; x < ink.width(); x++)
				if (qAlpha(ink.pixel(x, y)) > 0) {
					inked++;
					right_ink = right_ink &&
					    QColor(ink.pixel(x, y)).rgb() ==
					    want_ink.rgb();
				}
		CHECK(inked > 0 && right_ink);
		CHECK(w->isActiveWindow());

		/* Another window in front: the title is redrawn in the
		 * style's colour, then white again when this one is back. */
		auto ink_of = [&]() {
			const QImage im = title->pixmap().toImage();
			for (int y = 0; y < im.height(); y++)
				for (int x = 0; x < im.width(); x++)
					if (qAlpha(im.pixel(x, y)) > 0)
						return QColor(im.pixel(x, y)).rgb();
			return QColor(Qt::transparent).rgb();
		};
		const QRgb style_ink = tabs->tabBar()->palette()
		    .color(QPalette::WindowText).rgb();
		CHECK(style_ink != QColor(Qt::white).rgb());
		{
			QWidget other;
			other.show();
			other.activateWindow();
			spin(100);
			CHECK(!w->isActiveWindow());
			CHECK(ink_of() == style_ink);
		}
		w->activateWindow();
		screen->setFocus();
		spin(100);
		CHECK(w->isActiveWindow());
		CHECK(ink_of() == QColor(Qt::white).rgb());

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

	/* Waiting for a port, the status bar says why. */
	{
		const QString missing = settings_dir.filePath("no-such-port");
		MainWindow waiting(missing);
		waiting.show();
		spin(100);
		QLabel *st = waiting.findChild<QLabel *>("linkState");
		CHECK(st != nullptr);
		if (st != nullptr)
			CHECK(st->text().startsWith(QString(
			    "Waiting for %1: no such device").arg(missing)));
	}

	/* On the loopback port, keys come back to the screen: typed, cooked,
	 * echoed and drawn by the firmware, with no board at all. */
	w = new MainWindow(Link::LOOPBACK);
	w->show();
	spin(200);
	screen = w->findChild<Screen *>();
	CHECK(screen != nullptr);
	if (screen != nullptr) {
		screen->setFocus();
		spin(50);
		key(screen, kVK_ANSI_H, Qt::NoModifier);
		key(screen, kVK_ANSI_I, Qt::ShiftModifier);
		spin(200);
		screen->selectAll();
		CHECK(screen->selectedText().startsWith("hI"));
	}
	delete w;

	close(board);
	if (failures == 0)
		printf("test_mainwindow: all passed\n");
	return failures == 0 ? 0 : 1;
}
