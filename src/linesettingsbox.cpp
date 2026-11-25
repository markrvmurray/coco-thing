/*
 * linesettingsbox.cpp
 *
 * See linesettingsbox.h.
 */

#include "linesettingsbox.h"

#include <QComboBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

/* The fastest rate a typed one may ask for. QSerialPort sets any rate on
 * macOS, and a USB-serial adapter decides what it can do. */
static const int MAX_BAUD = 4000000;

static QComboBox *
choice(const QList<QPair<QString, int>> &items)
{
	QComboBox *box = new QComboBox;

	for (const auto &item : items)
		box->addItem(item.first, item.second);
	return box;
}

static void
pick(QComboBox *box, int value)
{
	const int i = box->findData(value);

	if (i >= 0)
		box->setCurrentIndex(i);
}

LineSettingsBox::LineSettingsBox(QWidget *parent)
	: QWidget(parent)
{
	baud = new QComboBox;
	baud->setObjectName("baud");
	baud->setEditable(true);
	baud->setInsertPolicy(QComboBox::NoInsert);
	for (qint32 rate : LineSettings::standardRates())
		baud->addItem(QString::number(rate), rate);
	baud->setValidator(new QIntValidator(1, MAX_BAUD, baud));
	baud->setToolTip("A standard rate, or type any other");

	data_bits = choice({ { "8", QSerialPort::Data8 },
	    { "7", QSerialPort::Data7 }, { "6", QSerialPort::Data6 },
	    { "5", QSerialPort::Data5 } });
	data_bits->setObjectName("dataBits");
	parity = choice({ { "None", QSerialPort::NoParity },
	    { "Even", QSerialPort::EvenParity },
	    { "Odd", QSerialPort::OddParity } });
	parity->setObjectName("parity");
	stop_bits = choice({ { "1", QSerialPort::OneStop },
	    { "2", QSerialPort::TwoStop } });
	stop_bits->setObjectName("stopBits");
	flow = choice({ { "None", LineSettings::FlowNone },
	    { "RTS/CTS", LineSettings::FlowRtsCts } });
	flow->setObjectName("flow");

	note = new QLabel;
	note->setObjectName("lineNote");
	note->setWordWrap(true);
	note->hide();

	QFormLayout *form = new QFormLayout;
	form->addRow("Baud rate:", baud);
	form->addRow("Data bits:", data_bits);
	form->addRow("Parity:", parity);
	form->addRow("Stop bits:", stop_bits);
	form->addRow("Flow control:", flow);

	QVBoxLayout *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->addLayout(form);
	v->addWidget(note);

	setSettings(LineSettings());
}

void
LineSettingsBox::setSettings(const LineSettings &l)
{
	last = l;
	baud->setEditText(QString::number(l.baud));
	pick(data_bits, l.data_bits);
	pick(parity, l.parity);
	pick(stop_bits, l.stop_bits);
	pick(flow, l.flow);
}

LineSettings
LineSettingsBox::settings() const
{
	LineSettings l = last;
	bool ok = false;
	const int rate = baud->currentText().toInt(&ok);

	if (ok && rate > 0 && rate <= MAX_BAUD)
		l.baud = rate;
	l.data_bits = (QSerialPort::DataBits)data_bits->currentData().toInt();
	l.parity = (QSerialPort::Parity)parity->currentData().toInt();
	l.stop_bits = (QSerialPort::StopBits)stop_bits->currentData().toInt();
	l.flow = (LineSettings::Flow)flow->currentData().toInt();
	return l;
}

void
LineSettingsBox::setApplicable(bool on, const QString &why)
{
	applies = on;
	for (QWidget *w : { (QWidget *)baud, (QWidget *)data_bits,
	    (QWidget *)parity, (QWidget *)stop_bits, (QWidget *)flow })
		w->setEnabled(on);
	note->setText(why);
	note->setVisible(!on && !why.isEmpty());
}
