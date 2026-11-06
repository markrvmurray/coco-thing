/*
 * prefsdialog.h
 *
 * The Preferences dialog: font, display and connection options. The font
 * preview is drawn with the face itself, at the cell's real 8x16 size
 * doubled, so it shows what the screen will.
 */

#pragma once

#include <QDialog>

#include "prefs.h"

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

class PrefsDialog : public QDialog {
	Q_OBJECT

public:
	PrefsDialog(const Prefs &p, QWidget *parent = nullptr);

	Prefs prefs() const;

public slots:
	void accept() override;

private slots:
	void browseFont();
	void updatePreview();
	void exportFace();

private:
	QWidget *fontTab(const Prefs &p);
	QWidget *displayTab(const Prefs &p);
	QWidget *connectionTab(const Prefs &p);

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
};
