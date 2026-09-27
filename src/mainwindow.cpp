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

MainWindow::MainWindow(const QString &port_name, QWidget *parent)
	: QMainWindow(parent)
{
	setWindowTitle("coco-thing");

	screen = new Screen(this);
	setCentralWidget(screen);
	connect(screen, &Screen::keyBytes, this, &MainWindow::sendKeys);

	QMenu *link = menuBar()->addMenu("&Connection");
	link->addAction("&Connect…", QKeySequence("Ctrl+K"), this,
	    &MainWindow::choosePort);
	link->addAction("&Disconnect", this, &MainWindow::disconnectPort);
	link->addSeparator();
	link->addAction("&Reset Screen", QKeySequence("Ctrl+R"), this,
	    &MainWindow::resetScreen);

	state = new QLabel(this);
	statusBar()->addPermanentWidget(state, 1);

	connect(&serial, &QSerialPort::readyRead, this,
	    &MainWindow::readSerial);
	connect(&serial, &QSerialPort::errorOccurred, this,
	    &MainWindow::serialError);
	connect(&timer, &QTimer::timeout, this, &MainWindow::tick);
	timer.start(FRAME_MS);

	if (!port_name.isEmpty())
		openPort(port_name);
	showState();
}

void
MainWindow::openPort(const QString &name)
{
	if (serial.isOpen())
		serial.close();

	// A CDC ACM port ignores the line settings, so the defaults stand.
	serial.setPortName(name);
	if (!serial.open(QIODevice::ReadWrite)) {
		statusBar()->showMessage(QString("Cannot open %1: %2")
		    .arg(name, serial.errorString()));
		showState();
		return;
	}
	// Some CDC firmware holds its output until the host asserts DTR.
	serial.setDataTerminalReady(true);
	statusBar()->clearMessage();
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
		openPort(name);
}

void
MainWindow::disconnectPort()
{
	serial.close();
	showState();
}

void
MainWindow::resetScreen()
{
	terminal.reset();
	screen->update();
}

void
MainWindow::readSerial()
{
	terminal.feed(serial.readAll());
}

void
MainWindow::serialError(QSerialPort::SerialPortError error)
{
	if (error == QSerialPort::NoError)
		return;
	statusBar()->showMessage(QString("%1: %2")
	    .arg(serial.portName(), serial.errorString()));
	// The board unplugged or reset: the device node has gone.
	if (error == QSerialPort::ResourceError)
		serial.close();
	showState();
}

void
MainWindow::sendKeys(const QByteArray &bytes)
{
	if (serial.isOpen())
		serial.write(bytes);
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
	QString s = serial.isOpen()
	    ? QString("Connected: %1").arg(serial.portName())
	    : QString("Not connected");
	if (terminal.backlog() > 0)
		s += QString("   backlog %1 B").arg(terminal.backlog());
	state->setText(s);
}
