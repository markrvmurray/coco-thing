/*
 * prefs.h
 *
 * The user's preferences, kept with QSettings.
 */

#pragma once

#include <QByteArray>
#include <QString>

struct Prefs {
	/* Text face: the firmware's own, a system font drawn into the cell,
	 * or a face loaded from a file (see fontface.h). */
	enum FontSource { FontBuiltin, FontSystem, FontFile };
	FontSource font_source = FontBuiltin;
	QString font_family;
	QString font_file;
	bool keep_box_glyphs = true;

	/* Fit: fill the window, keeping the aspect ratio. Integer: the largest
	 * whole multiple of 1024x480 that fits, so every pixel is the same
	 * size (falls back to Fit in a window smaller than 1x). */
	enum Scaling { ScaleFit, ScaleInteger };
	Scaling scaling = ScaleFit;
	bool smooth = true;

	/* The text area: match the Term descriptor's IT.COL x IT.ROW, which is
	 * what SS.ScSiz tells programs (see Terminal::setTextArea). */
	int text_cols = 80;
	int text_rows = 25;

	/* Lines kept after they scroll off the top; 0 keeps none. Each holds
	 * its pixels, about 10 KB at 80 columns, hence the dialog's ceiling. */
	int scrollback_lines = 500;

	bool auto_reconnect = true;
	bool clear_on_reconnect = true;
	bool connect_at_start = true;

	static Prefs load();
	void save() const;

	/* The face these preferences select: empty for the built-in one.
	 * On failure, empty with *error set. */
	QByteArray face(QString *error) const;
};
