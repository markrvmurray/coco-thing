/*
 * linesettings.h
 *
 * A serial port's line settings: baud rate, framing and flow control, kept
 * per port.
 *
 * The Pico-Thing's own USB console ignores them, as does the loopback plug,
 * but a USB-serial adapter wired to an ACIA, or a real RS-232 port, needs
 * them right. They are stored against the port's name, so a port gets its
 * own back whenever it is opened again: from Connect, at start-up, or when
 * the link reopens it after a reset.
 *
 * Parity is none, even or odd: macOS has no mark or space parity, and
 * QSerialPort opens a port without them rather than failing. One or two
 * stop bits, as 1.5 is Windows-only.
 *
 * Flow control is none or RTS/CTS only. Software flow control is never
 * used: the guest's stream is binary ($1B graphics, GPLoad payloads), so
 * $11 and $13 are data, and Ctrl-Q and Ctrl-S are keys the guest must get.
 */

#pragma once

#include <QList>
#include <QSerialPort>
#include <QString>

class QSettings;

struct LineSettings {
	enum Flow { FlowNone, FlowRtsCts };

	qint32 baud = 9600;
	QSerialPort::DataBits data_bits = QSerialPort::Data8;
	QSerialPort::Parity parity = QSerialPort::NoParity;
	QSerialPort::StopBits stop_bits = QSerialPort::OneStop;
	Flow flow = FlowNone;

	bool operator==(const LineSettings &o) const
	{
		return baud == o.baud && data_bits == o.data_bits &&
		    parity == o.parity && stop_bits == o.stop_bits &&
		    flow == o.flow;
	}
	bool operator!=(const LineSettings &o) const { return !(*this == o); }

	/* Set them on `port`: before it is opened, they are applied as it
	 * opens; while it is open, at once. False if the port refused one. */
	bool applyTo(QSerialPort &port) const;

	/* As a terminal program writes them: "9600 8N1", "115200 7E2 RTS/CTS". */
	QString summary() const;

	/* The rates offered by name; any other may be typed. */
	static QList<qint32> standardRates();

	/* `port`'s settings in `s`, or the defaults if it has none. A value
	 * that cannot be set here is replaced by its default. */
	static LineSettings load(QSettings &s, const QString &port);
	/* Keep them for `port` in `s`. Values at their defaults are not
	 * stored, and a port left with none is forgotten. */
	void save(QSettings &s, const QString &port) const;
};
