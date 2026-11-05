/*
 * link.cpp
 *
 * See link.h.
 */

#include "link.h"

#include <QFile>

static const int POLL_MS = 500;

const QString Link::LOOPBACK = QStringLiteral("Loopback");

Link::Link(QObject *parent)
	: QObject(parent)
{
	poll.setInterval(POLL_MS);
	connect(&poll, &QTimer::timeout, this, &Link::check);
	connect(&port, &QSerialPort::readyRead, this, &Link::readPort);
	connect(&port, &QSerialPort::errorOccurred, this, &Link::portError);
}

QString
Link::path() const
{
	// QSerialPort's own rule: a bare name lives in /dev.
	return wanted.startsWith('/') ? wanted : "/dev/" + wanted;
}

void
Link::open(const QString &name)
{
	close();
	wanted = name;
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

void
Link::write(const QByteArray &bytes)
{
	if (st != Open)
		return;
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

	// A CDC ACM port ignores the line settings, so the defaults stand.
	port.setPortName(path());
	if (!port.open(QIODevice::ReadWrite)) {
		error = port.errorString();
		return;
	}
	// Some CDC firmware holds its output until the host asserts DTR.
	port.setDataTerminalReady(true);
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
