/*
 * connectdialog.h
 *
 * The Connect dialog: the port, in an editable list so a path can be typed
 * (USim's console pty is never listed), and its line settings. Choosing or
 * typing a port shows the settings kept for it, or greys them when the port
 * takes none.
 */

#pragma once

#include <QDialog>

#include "linesettings.h"

class QComboBox;
class LineSettingsBox;

class ConnectDialog : public QDialog {
	Q_OBJECT

public:
	/* `names` to choose from (Link::choices), with `preferred` chosen.
	 * Each port's kept settings are read from the application's
	 * QSettings. */
	ConnectDialog(const QStringList &names, int preferred,
	    QWidget *parent = nullptr);

	/* The port chosen or typed, as Link::typedName makes it. */
	QString port() const;
	LineSettings lineSettings() const;

	/* Why a port takes no line settings, or empty if it takes them. */
	static QString noSettingsReason(const QString &port);

private slots:
	void portChanged();

private:
	QComboBox *ports;
	LineSettingsBox *line;
};
