/*
 * linesettings.cpp
 *
 * See linesettings.h.
 */

#include "linesettings.h"

#include <QSettings>
#include <QUrl>

bool
LineSettings::applyTo(QSerialPort &port) const
{
	bool ok = port.setBaudRate(baud);

	ok = port.setDataBits(data_bits) && ok;
	ok = port.setParity(parity) && ok;
	ok = port.setStopBits(stop_bits) && ok;
	// QSerialPort's NoFlowControl and HardwareControl both clear the
	// software flow control flags; only SoftwareControl sets them.
	ok = port.setFlowControl(flow == FlowRtsCts ?
	    QSerialPort::HardwareControl : QSerialPort::NoFlowControl) && ok;
	return ok;
}

QString
LineSettings::summary() const
{
	QChar p = 'N';

	if (parity == QSerialPort::EvenParity)
		p = 'E';
	else if (parity == QSerialPort::OddParity)
		p = 'O';
	QString s = QString("%1 %2%3%4").arg(baud).arg((int)data_bits).arg(p)
	    .arg(stop_bits == QSerialPort::TwoStop ? 2 : 1);
	if (flow == FlowRtsCts)
		s += " RTS/CTS";
	return s;
}

QList<qint32>
LineSettings::standardRates()
{
	return { 300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600,
	    115200, 230400, 460800, 921600 };
}

/* The settings group for `port`. A port may be named by a path, whose
 * slashes QSettings would take as nested groups, so the name is
 * percent-encoded into a single key. */
static QString
group(const QString &port)
{
	return "ports/" + QString::fromLatin1(QUrl::toPercentEncoding(port));
}

LineSettings
LineSettings::load(QSettings &s, const QString &port)
{
	LineSettings l;

	s.beginGroup(group(port));
	l.baud = s.value("baud", l.baud).toInt();
	l.data_bits = (QSerialPort::DataBits)s.value("data_bits",
	    (int)l.data_bits).toInt();
	l.parity = (QSerialPort::Parity)s.value("parity", (int)l.parity).toInt();
	l.stop_bits = (QSerialPort::StopBits)s.value("stop_bits",
	    (int)l.stop_bits).toInt();
	l.flow = (Flow)s.value("flow", (int)l.flow).toInt();
	s.endGroup();

	// Anything this version cannot set -- an edited file, or a value a
	// later version added -- falls back to its default.
	const LineSettings d;
	if (l.baud <= 0)
		l.baud = d.baud;
	if (l.data_bits < QSerialPort::Data5 || l.data_bits > QSerialPort::Data8)
		l.data_bits = d.data_bits;
	if (l.parity != QSerialPort::NoParity &&
	    l.parity != QSerialPort::EvenParity &&
	    l.parity != QSerialPort::OddParity)
		l.parity = d.parity;
	if (l.stop_bits != QSerialPort::OneStop &&
	    l.stop_bits != QSerialPort::TwoStop)
		l.stop_bits = d.stop_bits;
	if (l.flow != FlowNone && l.flow != FlowRtsCts)
		l.flow = d.flow;
	return l;
}

/* As Prefs::save: a value is stored only while it differs from the default,
 * so an unchanged one follows the default if a later version changes it. */
static void
put(QSettings &s, const char *key, int value, int fallback)
{
	if (value == fallback)
		s.remove(key);
	else
		s.setValue(key, value);
}

void
LineSettings::save(QSettings &s, const QString &port) const
{
	const LineSettings d;

	s.beginGroup(group(port));
	put(s, "baud", baud, d.baud);
	put(s, "data_bits", data_bits, d.data_bits);
	put(s, "parity", parity, d.parity);
	put(s, "stop_bits", stop_bits, d.stop_bits);
	put(s, "flow", flow, d.flow);
	s.endGroup();
}
