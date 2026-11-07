/*
 * link.h
 *
 * The serial link to the board's USB CDC console, kept up across resets.
 *
 * When the board resets, its USB device goes away and the port's device node
 * vanishes; it comes back under the same name once the board re-enumerates.
 * The link remembers the port it was asked for and, until close() is called
 * (and unless auto-reconnect is turned off), keeps trying to reopen it:
 * after a loss, and also when the port is not there to begin with, so the
 * terminal can be started before the board.
 *
 * The port named LOOPBACK is not a device but a loopback plug: everything
 * written comes back as received, with the handshake lines tied, so nothing
 * ever holds it up. It opens at once, and is never lost or waited for.
 *
 * A loss is noticed two ways: a ResourceError from the port, or the device
 * node disappearing while the port is open (checked on each poll, because a
 * port with nothing to read may not report the error until it is written).
 */

#pragma once

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QTimer>

class Link : public QObject {
	Q_OBJECT

public:
	enum State {
		Closed,		/* no port wanted */
		Waiting,	/* wanted, not open: polling for it */
		Open,
	};

	/* The virtual loopback port's name. */
	static const QString LOOPBACK;

	explicit Link(QObject *parent = nullptr);

	/* Connect to `name` (e.g. cu.usbmodem1101, or an absolute path) and
	 * keep it connected until close(). */
	void open(const QString &name);
	void close();
	void write(const QByteArray &bytes);

	State state() const { return st; }
	QString portName() const { return wanted; }
	/* Why the last open attempt or the last loss happened, if it did. */
	QString lastError() const { return error; }

	/* Whether a lost or absent port is waited for (the default) or the
	 * link simply closes. */
	void setAutoReconnect(bool on) { auto_reconnect = on; }

	/* How often a waiting link looks for its port, and an open one checks
	 * it is still there. */
	void setPollInterval(int ms) { poll.setInterval(ms); }

signals:
	void received(const QByteArray &bytes);
	/* Bytes handed to an open port by write(), as they go. A write to a
	 * link that is not open sends nothing and says nothing. */
	void sent(const QByteArray &bytes);
	/* The port opened. `again` is true when it had been open before
	 * under this open() -- that is, the board came back. */
	void connected(bool again);
	/* The port went away; the link is now waiting for it, or closed if
	 * auto-reconnect is off. */
	void lost();
	void stateChanged(Link::State st);

private slots:
	void readPort();
	void echo();
	void portError(QSerialPort::SerialPortError e);
	void check();

private:
	void tryOpen();
	void drop(const QString &why);
	void setState(State s);
	void giveUp();
	QString path() const;

	QSerialPort port;
	QTimer poll;
	QString wanted;
	QString error;
	State st = Closed;
	bool was_open = false;
	bool auto_reconnect = true;
	bool loopback = false;		/* open on the virtual loopback port */
	QByteArray looped;		/* written, not yet received back */
};
