/*
 * connectdialog.cpp
 *
 * See connectdialog.h.
 */

#include "connectdialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QVBoxLayout>

#include "link.h"
#include "linesettingsbox.h"

/* The chosen port is shown white on this deep blue: the macOS style's own
 * colours put black text on light blue, which was hard to read. */
static const QColor SELECTED_BACK(0x1E, 0x5A, 0xC8);
static const QColor SELECTED_TEXT(Qt::white);

static void
white_on_blue(QWidget *w)
{
	QPalette p = w->palette();

	for (QPalette::ColorGroup g : { QPalette::Active, QPalette::Inactive }) {
		p.setColor(g, QPalette::Highlight, SELECTED_BACK);
		p.setColor(g, QPalette::HighlightedText, SELECTED_TEXT);
	}
	w->setPalette(p);
}

ConnectDialog::ConnectDialog(const QStringList &names, int preferred,
    QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle("Connect");

	ports = new QComboBox;
	ports->setObjectName("port");
	ports->addItems(names);
	// Editable, because a pty -- USim's console, for one -- is never
	// listed: its path, or a symlink to it, can be typed instead.
	ports->setEditable(true);
	ports->setInsertPolicy(QComboBox::NoInsert);
	if (preferred >= 0 && preferred < names.size())
		ports->setCurrentIndex(preferred);
	// The box, the line in it, and the list that drops down.
	white_on_blue(ports);
	white_on_blue(ports->lineEdit());
	white_on_blue(ports->view());

	line = new LineSettingsBox;
	QGroupBox *group = new QGroupBox("Line settings");
	QVBoxLayout *g = new QVBoxLayout(group);
	g->addWidget(line);

	QDialogButtonBox *buttons = new QDialogButtonBox(
	    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QVBoxLayout *v = new QVBoxLayout(this);
	v->addWidget(new QLabel("Serial port, or the path of a device or pty:"));
	v->addWidget(ports);
	v->addWidget(group);
	v->addWidget(buttons);

	connect(ports, &QComboBox::currentTextChanged, this,
	    &ConnectDialog::portChanged);
	portChanged();
}

QString
ConnectDialog::noSettingsReason(const QString &port)
{
	if (port == Link::LOOPBACK)
		return "The loopback plug has no line: everything written "
		    "comes straight back.";
	if (Link::ignoresLineSettings(port))
		return "The Pico-Thing's own USB serial port takes no line "
		    "settings.";
	return {};
}

void
ConnectDialog::portChanged()
{
	const QString name = port();
	QSettings s;

	line->setSettings(LineSettings::load(s, name));
	const QString why = noSettingsReason(name);
	line->setApplicable(why.isEmpty() && !name.isEmpty(), why);
}

QString
ConnectDialog::port() const
{
	return Link::typedName(ports->currentText());
}

LineSettings
ConnectDialog::lineSettings() const
{
	return line->settings();
}
