/*
 * link.cpp
 *
 * See link.h.
 */

#include "link.h"

#include <QDir>
#include <QFile>
#include <QSerialPortInfo>

static const int POLL_MS = 500;

/* The Pico-Thing's USB device (pico-thing-main's src/usb_descriptors.c). Its
 * CDC ports carry the ACIAs' bytes over USB, with no UART behind them to
 * set. */
static const quint16 PICO_THING_VID = 0xCAFE;
static const quint16 PICO_THING_PID = 0x4001;

const QString Link::LOOPBACK = QStringLiteral("Loopback");

Link::Link(QObject *parent)
	: QObject(parent)
{
	poll.setInterval(POLL_MS);
	connect(&poll, &QTimer::timeout, this, &Link::check);
	connect(&port, &QSerialPort::readyRead, this, &Link::readPort);
	connect(&port, &QSerialPort::errorOccurred, this, &Link::portError);
}

QStringList
Link::choices(const QStringList &available, const QString &last,
    int *preferred)
{
	// The loopback plug first: always there, needing no board.
	QStringList names = { LOOPBACK };
	int usbmodem = -1;

	for (const QString &name : available) {
		if (name.startsWith("tty."))
			continue;
		if (usbmodem < 0 && name.contains("usbmodem"))
			usbmodem = (int)names.size();
		names << name;
	}
	if (!last.isEmpty() && !names.contains(last))
		names << last;
	if (!last.isEmpty())
		*preferred = (int)names.indexOf(last);
	else
		*preferred = usbmodem >= 0 ? usbmodem : 0;
	return names;
}

QString
Link::typedName(const QString &typed)
{
	const QString name = typed.trimmed();

	if (name == "~" || name.startsWith("~/"))
		return QDir::homePath() + name.mid(1);
	return name;
}

bool
Link::ignoresLineSettings(const QString &name)
{
	if (name == LOOPBACK)
		return true;
	const QSerialPortInfo info(name);
	return info.hasVendorIdentifier() && info.hasProductIdentifier() &&
	    info.vendorIdentifier() == PICO_THING_VID &&
	    info.productIdentifier() == PICO_THING_PID;
}

QString
Link::path() const
{
	// QSerialPort's own rule: a bare name lives in /dev.
	return wanted.startsWith('/') ? wanted : "/dev/" + wanted;
}

void
Link::open(const QString &name, const LineSettings &settings)
{
	close();
	wanted = name;
	line = settings;
	was_open = false;
	if (name == LOOPBACK) {
		// Nothing to find, lose or wait for: open now.
		loopback = true;
		was_open = true;
		setState(Open);
		emit connected(false);
		return;
	}
	poll.start();
	tryOpen();
	if (st != Open) {
		if (auto_reconnect)
			setState(Waiting);
		else
			giveUp();
	}
}

/* Stop trying, but keep the name, so the port can be reported. */
void
Link::giveUp()
{
	poll.stop();
	setState(Closed);
}

void
Link::close()
{
	poll.stop();
	if (port.isOpen())
		port.close();
	loopback = false;
	looped.clear();
	wanted.clear();
	error.clear();
	setState(Closed);
}

bool
Link::setLineSettings(const LineSettings &settings)
{
	line = settings;
	// The loopback plug has no line; a closed or waiting port takes them
	// when it opens.
	if (loopback || !port.isOpen())
		return true;
	if (!line.applyTo(port)) {
		error = port.errorString();
		port.clearError();
		return false;
	}
	return true;
}

void
Link::write(const QByteArray &bytes)
{
	if (st != Open || bytes.isEmpty())
		return;
	emit sent(bytes);
	if (!loopback) {
		port.write(bytes);
		return;
	}
	// Straight back, as received data. Delivered from the event loop, as
	// a real port's input is, so a write never re-enters the reader; and
	// queued, so order is kept across writes made before it runs. The
	// handshake lines are tied: nothing here ever waits for clear to send.
	if (looped.isEmpty())
		QTimer::singleShot(0, this, &Link::echo);
	looped += bytes;
}

void
Link::echo()
{
	if (!loopback || looped.isEmpty())
		return;
	const QByteArray bytes = looped;
	looped.clear();
	emit received(bytes);
}

void
Link::tryOpen()
{
	if (!QFile::exists(path())) {
		error = "no such device";
		return;
	}

	// Set on the closed port, the settings are applied as it opens, and
	// an open that cannot apply them fails.
	port.setPortName(path());
	line.applyTo(port);
	if (!port.open(QIODevice::ReadWrite)) {
		error = port.errorString();
		return;
	}
	// Some CDC firmware holds its output until the host asserts DTR. A
	// pty (USim's console) has no modem lines, and refuses with an
	// unsupported-operation error that is no fault of the link's: let it
	// go, so the port is not left reporting it.
	if (!port.setDataTerminalReady(true))
		port.clearError();
	error.clear();

	const bool again = was_open;
	was_open = true;
	setState(Open);
	emit connected(again);
}

void
Link::drop(const QString &why)
{
	if (st != Open)
		return;
	error = why;
	port.close();
	if (auto_reconnect)
		setState(Waiting);
	else
		giveUp();
	emit lost();
}

void
Link::readPort()
{
	const QByteArray bytes = port.readAll();

	if (!bytes.isEmpty())
		emit received(bytes);
}

void
Link::portError(QSerialPort::SerialPortError e)
{
	// ResourceError is the device going away underneath an open port.
	// Close it from the event loop, not inside the port's own signal.
	if (e == QSerialPort::ResourceError) {
		const QString why = port.errorString();
		QTimer::singleShot(0, this, [this, why] { drop(why); });
	}
}

void
Link::check()
{
	if (st == Open) {
		if (!QFile::exists(path()))
			drop("device removed");
	} else if (st == Waiting) {
		tryOpen();
	}
}

void
Link::setState(State s)
{
	if (s == st)
		return;
	st = s;
	emit stateChanged(s);
}
