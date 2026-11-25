/*
 * linesettingsbox.h
 *
 * The controls for a port's line settings (linesettings.h), as Connect and
 * Preferences both show them. A port that takes no settings -- the loopback
 * plug, the Pico-Thing's own USB console -- has them shown greyed, with a
 * note saying why, rather than offered and silently ignored.
 */

#pragma once

#include <QWidget>

#include "linesettings.h"

class QComboBox;
class QLabel;

class LineSettingsBox : public QWidget {
	Q_OBJECT

public:
	explicit LineSettingsBox(QWidget *parent = nullptr);

	void setSettings(const LineSettings &l);
	/* What the controls show. A baud rate typed but not yet a number
	 * leaves the one last set. */
	LineSettings settings() const;

	/* Whether the port takes these settings; if not, `why` is shown in
	 * their place and the controls are greyed. */
	void setApplicable(bool applies, const QString &why = {});
	bool applicable() const { return applies; }

private:
	QComboBox *baud;
	QComboBox *data_bits;
	QComboBox *parity;
	QComboBox *stop_bits;
	QComboBox *flow;
	QLabel *note;
	LineSettings last;
	bool applies = true;
};
