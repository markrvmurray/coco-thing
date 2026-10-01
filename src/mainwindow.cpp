/*
 * mainwindow.cpp
 *
 * See mainwindow.h.
 */

#include "mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QInputDialog>
#include <QMenuBar>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStatusBar>

#include "fw_platform.h"
#include "prefsdialog.h"
#include "screentext.h"

/* One frame at the board's ~60 Hz, and the share of it the firmware may
 * spend interpreting before the paint. */
static const int FRAME_MS = 16;
static const int PUMP_BUDGET_MS = 10;

/* Pasted bytes sent each frame: about 1000 a second. USB CDC would deliver a
 * paste all at once, but the board's ACIA driver (sc6850) has a receive
 * buffer of under a page and no flow control on this link, so a large paste
 * arriving faster than the program reading it would overrun the buffer and
 * lose characters without a word. This is faster than anyone types and slow
 * enough for the shell and editors to keep up. */
static const int PASTE_PER_FRAME = 16;

/* A menu shortcut that leaves every Ctrl chord to the guest. main() keeps
 * Qt from swapping Control and Command on macOS, so there "Ctrl" in a key
 * sequence is the real Control key; menus go on Command (Qt's Meta) instead.
 * Elsewhere, Ctrl+Shift keeps them off the guest's control codes. */
static QKeySequence
shortcut(Qt::Key key)
{
#ifdef Q_OS_MACOS
	return QKeySequence(Qt::META | key);
#else
	return QKeySequence(Qt::CTRL | Qt::SHIFT | key);
#endif
}

MainWindow::MainWindow(const QString &port_name, QWidget *parent)
	: QMainWindow(parent)
{
	setWindowTitle("coco-thing");

	screen = new Screen(this);
	setCentralWidget(screen);
	screen->setKeyboard(&keyboard);
	fw_platform_set_bell([] { QApplication::beep(); });
	connect(screen, &Screen::keysTyped, this, &MainWindow::sendKeys);
	clock.start();

	QMenu *conn = menuBar()->addMenu("&Connection");
	conn->addAction("&Connect…", shortcut(Qt::Key_K), this,
	    &MainWindow::choosePort);
	conn->addAction("&Disconnect", this, &MainWindow::disconnectPort);
	conn->addSeparator();
	conn->addAction("&Reset Screen", shortcut(Qt::Key_R), this,
	    &MainWindow::resetScreen);
	conn->addSeparator();
	// On macOS the role moves this into the application menu.
	QAction *prefs_action = conn->addAction("&Preferences…",
	    shortcut(Qt::Key_Comma), this, &MainWindow::editPrefs);
	prefs_action->setMenuRole(QAction::PreferencesRole);

	// The board's screen cannot be edited from here, so Cut copies.
	QMenu *edit = menuBar()->addMenu("&Edit");
	edit->addAction("Cu&t", shortcut(Qt::Key_X), this, &MainWindow::cut);
	edit->addAction("&Copy", shortcut(Qt::Key_C), this, &MainWindow::copy);
	edit->addAction("&Paste", shortcut(Qt::Key_V), this,
	    &MainWindow::paste);
	edit->addSeparator();
	edit->addAction("Select &All", shortcut(Qt::Key_A), this,
	    &MainWindow::selectAll);
	// Selecting copies, as the pasteboard's "primary selection".
	connect(screen, &Screen::selectionMade, this, &MainWindow::copy);

	state = new QLabel(this);
	statusBar()->addPermanentWidget(state, 1);

	connect(&link, &Link::received, &terminal, &Terminal::feed);
	connect(&link, &Link::connected, this, &MainWindow::linkConnected);
	connect(&link, &Link::lost, this, &MainWindow::linkLost);
	connect(&link, &Link::stateChanged, this, &MainWindow::showState);
	connect(&timer, &QTimer::timeout, this, &MainWindow::tick);
	timer.start(FRAME_MS);

	QSettings settings;
	restoreGeometry(settings.value("window/geometry").toByteArray());

	prefs = Prefs::load();
	applyPrefs();

	QString name = port_name;
	if (name.isEmpty() && prefs.connect_at_start)
		name = settings.value("connection/last_port").toString();
	if (!name.isEmpty())
		link.open(name);
	showState();
}

MainWindow::~MainWindow()
{
	// The members (the link, the keyboard, the terminal) are destroyed
	// before ~QWidget hides the child widgets, and hiding the screen takes
	// its focus, which releases the keys and signals sendKeys(). Cut the
	// screen loose first, so nothing reaches a member that has gone.
	screen->setKeyboard(nullptr);
	disconnect(screen, nullptr, this, nullptr);
}

void
MainWindow::closeEvent(QCloseEvent *ev)
{
	QSettings().setValue("window/geometry", saveGeometry());
	QMainWindow::closeEvent(ev);
}

void
MainWindow::editPrefs()
{
	PrefsDialog dialog(prefs, this);

	if (dialog.exec() != QDialog::Accepted)
		return;
	prefs = dialog.prefs();
	prefs.save();
	applyPrefs();
}

void
MainWindow::applyPrefs()
{
	QString error;
	const QByteArray face = prefs.face(&error);

	// A font file that has gone away falls back to the built-in face
	// rather than leaving the screen blank.
	if (!error.isEmpty())
		statusBar()->showMessage(QString("Font: %1; using the built-in "
		    "face").arg(error));
	terminal.setFace(face);

	// A new text area means a new window, which starts clear.
	if (prefs.text_cols != terminal.textCols() ||
	    prefs.text_rows != terminal.textRows()) {
		terminal.setTextArea(prefs.text_cols, prefs.text_rows);
		terminal.reset();
		screen->setTextArea(terminal.textCols(), terminal.textRows());
	}

	screen->setScaling(prefs.scaling == Prefs::ScaleInteger, prefs.smooth);
	link.setAutoReconnect(prefs.auto_reconnect);
	screen->update();
}

void
MainWindow::choosePort()
{
	QStringList names;
	int preferred = 0;

	for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
		// The call-out device is the one to open on macOS.
		if (info.portName().startsWith("tty."))
			continue;
		if (info.portName().contains("usbmodem"))
			preferred = names.size();
		names << info.portName();
	}
	if (names.isEmpty()) {
		statusBar()->showMessage("No serial ports found");
		return;
	}

	bool ok = false;
	const QString name = QInputDialog::getItem(this, "Connect",
	    "Serial port:", names, preferred, false, &ok);
	if (!ok)
		return;
	link.open(name);
	if (link.state() == Link::Closed)
		statusBar()->showMessage(QString("Cannot open %1: %2")
		    .arg(name, link.lastError()));
}

void
MainWindow::disconnectPort()
{
	link.close();
}

void
MainWindow::resetScreen()
{
	screen->clearSelection();
	terminal.reset();
	screen->update();
}

void
MainWindow::linkConnected(bool again)
{
	QSettings().setValue("connection/last_port", link.portName());

	// The board came back from a reset. The video Pico clears its
	// console on the host's !RESET, so this one does too by default, and
	// anything still queued from before the reset is stale.
	if (again && prefs.clear_on_reconnect)
		resetScreen();
	statusBar()->clearMessage();
}

void
MainWindow::linkLost()
{
	// The screen is left as it was, so the last output stays readable.
	statusBar()->showMessage(link.state() == Link::Waiting
	    ? QString("%1 lost (%2); waiting for it")
	      .arg(link.portName(), link.lastError())
	    : QString("%1 lost (%2)").arg(link.portName(), link.lastError()));
}

void
MainWindow::sendKeys()
{
	// Read out even when nothing is connected, so keys typed then are
	// dropped rather than sent later.
	link.write(terminal.takeInput());
}

void
MainWindow::tick()
{
	keyboard.tick((uint32_t)clock.elapsed());
	sendKeys();
	if (!pasting.isEmpty()) {
		if (link.state() == Link::Open)
			link.write(pasting.left(PASTE_PER_FRAME));
		pasting.remove(0, PASTE_PER_FRAME);
	}
	terminal.pump(PUMP_BUDGET_MS);
	terminal.frame();
	screen->update();
	showState();
}

void
MainWindow::copy()
{
	if (screen->hasSelection())
		QApplication::clipboard()->setText(screen->selectedText());
}

void
MainWindow::cut()
{
	copy();
	screen->clearSelection();
}

void
MainWindow::paste()
{
	if (link.state() != Link::Open) {
		statusBar()->showMessage("Not connected: nothing pasted");
		return;
	}
	pasting += screentext_paste(QApplication::clipboard()->text());
}

void
MainWindow::selectAll()
{
	screen->selectAll();
	copy();
}

void
MainWindow::showState()
{
	QString s;

	switch (link.state()) {
	case Link::Closed:
		s = "Not connected";
		break;
	case Link::Waiting:
		s = QString("Waiting for %1").arg(link.portName());
		break;
	case Link::Open:
		s = QString("Connected: %1").arg(link.portName());
		break;
	}
	if (terminal.backlog() > 0)
		s += QString("   backlog %1 B").arg(terminal.backlog());
	if (!pasting.isEmpty())
		s += QString("   pasting %1 B").arg(pasting.size());
	state->setText(s);
}
