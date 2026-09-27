/*
 * link.cpp
 *
 * See link.h.
 */

#include "link.h"

#include <QFile>

static const int POLL_MS = 500;

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
	poll.start();
	tryOpen();
	if (st == Closed)
		setState(Waiting);
}

void
Link::close()
{
	poll.stop();
	if (port.isOpen())
		port.close();
	wanted.clear();
	error.clear();
	setState(Closed);
}

void
Link::write(const QByteArray &bytes)
{
	if (st == Open)
		port.write(bytes);
}

void
Link::tryOpen()
{
	if (!QFile::exists(path()))
		return;

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
	setState(Waiting);
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
