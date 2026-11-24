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
 * A port is named as QSerialPort names it (cu.usbmodem1101, found in /dev), or
 * by an absolute path, which may be a symlink. USim's `usim09pt
 * --console-pty=PATH` publishes the emulator's console that way: PATH links
 * to a pty that changes with every run, and is removed when the run ends, so
 * the emulator quitting and starting again looks just like a board reset.
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
#include <QStringList>
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

	/* The ports to offer for Connect, in order: LOOPBACK, then
	 * `available` (QSerialPort's names; the macOS tty.* dial-in twins are
	 * left out, as the cu.* call-out device is the one to open), then
	 * `last` if it is not among them -- a typed path, or a board that is
	 * not plugged in yet. `*preferred` is the one to offer first: `last`,
	 * or else the first usbmodem (the Pico-Thing's CDC port), or else
	 * LOOPBACK. */
	static QStringList choices(const QStringList &available,
	    const QString &last, int *preferred);

	/* A port name as typed: surrounding spaces dropped, and a leading ~/
	 * taken as the home directory. */
	static QString typedName(const QString &typed);

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
