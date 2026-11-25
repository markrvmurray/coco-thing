/*
 * prefsdialog.h
 *
 * The Preferences dialog: font, display and connection options, and the
 * line settings of the port in use. The font
 * preview is drawn with the face itself, at the cell's real 8x16 size
 * doubled, so it shows what the screen will.
 */

#pragma once

#include <QDialog>

#include "linesettings.h"
#include "prefs.h"

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class LineSettingsBox;

class PrefsDialog : public QDialog {
	Q_OBJECT

public:
	/* `port` is the one in use, with its settings `line`; empty if none
	 * is, which greys the line settings. */
	PrefsDialog(const Prefs &p, const QString &port = {},
	    const LineSettings &line = {}, QWidget *parent = nullptr);

	Prefs prefs() const;
	LineSettings lineSettings() const;

public slots:
	void accept() override;

private slots:
	void browseFont();
	void updatePreview();
	void exportFace();

private:
	QWidget *fontTab(const Prefs &p);
	QWidget *displayTab(const Prefs &p);
	QWidget *connectionTab(const Prefs &p, const QString &port,
	    const LineSettings &line);

	QRadioButton *font_builtin;
	QRadioButton *font_system;
	QRadioButton *font_file;
	QFontComboBox *family;
	QLineEdit *file;
	QCheckBox *keep_box;
	QLabel *preview;
	QLabel *font_error;
	QLabel *export_note;

	QComboBox *scaling;
	QCheckBox *smooth;
	QSpinBox *text_cols;
	QSpinBox *text_rows;
	QSpinBox *scrollback;

	QCheckBox *auto_reconnect;
	QCheckBox *clear_on_reconnect;
	QCheckBox *connect_at_start;
	LineSettingsBox *line_box;
};
