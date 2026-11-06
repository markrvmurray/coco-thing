/*
 * prefs.cpp
 *
 * See prefs.h.
 */

#include "prefs.h"

#include <QFileInfo>
#include <QFont>
#include <QSettings>

#include "fontface.h"

Prefs
Prefs::load()
{
	QSettings s;
	Prefs p;

	p.font_source = (FontSource)s.value("font/source", p.font_source).toInt();
	p.font_family = s.value("font/family").toString();
	p.font_file = s.value("font/file").toString();
	p.keep_box_glyphs = s.value("font/keep_box", p.keep_box_glyphs).toBool();
	p.scaling = (Scaling)s.value("display/scaling", p.scaling).toInt();
	p.smooth = s.value("display/smooth", p.smooth).toBool();
	p.text_cols = s.value("display/text_cols", p.text_cols).toInt();
	p.text_rows = s.value("display/text_rows", p.text_rows).toInt();
	p.scrollback_lines = s.value("display/scrollback_lines",
	    p.scrollback_lines).toInt();
	p.auto_reconnect = s.value("connection/auto_reconnect",
	    p.auto_reconnect).toBool();
	p.clear_on_reconnect = s.value("connection/clear_on_reconnect",
	    p.clear_on_reconnect).toBool();
	p.connect_at_start = s.value("connection/connect_at_start",
	    p.connect_at_start).toBool();
	return p;
}

void
Prefs::save() const
{
	QSettings s;

	s.setValue("font/source", font_source);
	s.setValue("font/family", font_family);
	s.setValue("font/file", font_file);
	s.setValue("font/keep_box", keep_box_glyphs);
	s.setValue("display/scaling", scaling);
	s.setValue("display/smooth", smooth);
	s.setValue("display/text_cols", text_cols);
	s.setValue("display/text_rows", text_rows);
	s.setValue("display/scrollback_lines", scrollback_lines);
	s.setValue("connection/auto_reconnect", auto_reconnect);
	s.setValue("connection/clear_on_reconnect", clear_on_reconnect);
	s.setValue("connection/connect_at_start", connect_at_start);
}

FaceHeading
Prefs::faceHeading() const
{
	const QString spleen_link =
	    "https://github.com/fcambus/spleen (BSD 2-Clause).";
	const QStringList name_note = {
		"",
		"The file and array names are generic, so this replaces the",
		"built-in face as it stands; font_8x16_name says which font it is.",
	};

	switch (font_source) {
	case FontSystem:
		return {
			font_family,
			font_family + " 8x16 bitmap font",
			QStringList{
				"Drawn by coco-thing from the system font \"" +
				    font_family + "\",",
				"at the largest size that fits the renderer's 8x16 "
				    "cell. The",
				"font's own licence governs these glyphs.",
				"",
				keep_box_glyphs ? "Glyphs it lacks, and the "
				    "box-drawing and block glyphs $B0-$DF," :
				    "Glyphs it lacks",
				"are Spleen 2.2.0's: " + spleen_link,
			} + name_note,
		};
	case FontFile: {
		const QFileInfo f(font_file);
		return {
			f.completeBaseName(),
			f.completeBaseName() + " 8x16 bitmap font",
			QStringList{
				"Converted by coco-thing from the font file \"" +
				    f.fileName() + "\".",
				"Glyphs it could not place are Spleen 2.2.0's:",
				spleen_link,
			} + name_note,
		};
	}
	case FontBuiltin:
		break;
	}
	return fontface_spleen_heading();
}

QByteArray
Prefs::face(QString *error) const
{
	switch (font_source) {
	case FontSystem:
		if (font_family.isEmpty())
			return {};
		return fontface_render(QFont(font_family), keep_box_glyphs);
	case FontFile:
		if (font_file.isEmpty())
			return {};
		return fontface_load(font_file, error);
	case FontBuiltin:
		break;
	}
	return {};
}
