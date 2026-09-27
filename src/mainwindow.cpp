/*
 * mainwindow.cpp
 *
 * See mainwindow.h.
 */

#include "mainwindow.h"

#include <QInputDialog>
#include <QMenuBar>
#include <QSerialPortInfo>
#include <QStatusBar>

/* One frame at the board's ~60 Hz, and the share of it the firmware may
 * spend interpreting before the paint. */
static const int FRAME_MS = 16;
static const int PUMP_BUDGET_MS = 10;

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
	connect(screen, &Screen::keyBytes, this, &MainWindow::sendKeys);

	QMenu *conn = menuBar()->addMenu("&Connection");
	conn->addAction("&Connect…", shortcut(Qt::Key_K), this,
	    &MainWindow::choosePort);
	conn->addAction("&Disconnect", this, &MainWindow::disconnectPort);
	conn->addSeparator();
	conn->addAction("&Reset Screen", shortcut(Qt::Key_R), this,
	    &MainWindow::resetScreen);

	state = new QLabel(this);
	statusBar()->addPermanentWidget(state, 1);

	connect(&link, &Link::received, &terminal, &Terminal::feed);
	connect(&link, &Link::connected, this, &MainWindow::linkConnected);
	connect(&link, &Link::lost, this, &MainWindow::linkLost);
	connect(&link, &Link::stateChanged, this, &MainWindow::showState);
	connect(&timer, &QTimer::timeout, this, &MainWindow::tick);
	timer.start(FRAME_MS);

	if (!port_name.isEmpty())
		link.open(port_name);
	showState();
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
	if (ok)
		link.open(name);
}

void
MainWindow::disconnectPort()
{
	link.close();
}

void
MainWindow::resetScreen()
{
	terminal.reset();
	screen->update();
}

void
MainWindow::linkConnected(bool again)
{
	// The board came back from a reset. The video Pico clears its
	// console on the host's !RESET, so this one does too, and anything
	// still queued from before the reset is stale.
	if (again)
		resetScreen();
	statusBar()->clearMessage();
}

void
MainWindow::linkLost()
{
	// The screen is left as it was, so the last output stays readable.
	statusBar()->showMessage(QString("%1 lost (%2); waiting for it")
	    .arg(link.portName(), link.lastError()));
}

void
MainWindow::sendKeys(const QByteArray &bytes)
{
	link.write(bytes);
}

void
MainWindow::tick()
{
	terminal.pump(PUMP_BUDGET_MS);
	terminal.frame();
	screen->update();
	showState();
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
	state->setText(s);
}
