/*
 * fontface.h
 *
 * Text faces for the firmware renderer. A face is the layout the firmware
 * and the protocol's Font command agree on: 256 glyphs, CP437 order, 16 bytes
 * each, one byte per row of the 8x16 cell with bit 7 the leftmost pixel --
 * 4096 bytes in all. The cell size is fixed by the renderer (128x30 text on
 * the 1024x480 panel), so a face changes the glyphs' shapes, not their size.
 *
 * An empty QByteArray means the firmware's built-in face (Spleen 8x16).
 */

#pragma once

#include <QByteArray>
#include <QFont>
#include <QImage>
#include <QString>
#include <QStringList>

static const int FONTFACE_GLYPHS = 256;
static const int FONTFACE_ROWS = 16;
static const int FONTFACE_BYTES = FONTFACE_GLYPHS * FONTFACE_ROWS;

/* The Unicode character each CP437 code draws; 0 for code 0. */
char32_t fontface_cp437(int code);

/* The built-in face, as bytes. */
QByteArray fontface_builtin();

/* A face drawn from a system font, sized to fit the cell. Glyphs the font
 * lacks come from the built-in face, and so do the box-drawing and block
 * glyphs ($B0-$DF) when keep_box is set, since an outline font rarely draws
 * them to meet the cell edges. */
QByteArray fontface_render(const QFont &font, bool keep_box);

/* A face from a file: a raw 4096-byte face, or a PSF1/PSF2 console font with
 * 8x16 glyphs. A PSF font's Unicode table, when it has one, is used to put
 * each glyph at its CP437 code; codes it cannot place come from the built-in
 * face. Empty, with *error set, if the file is not one of these. */
QByteArray fontface_load(const QString &path, QString *error);
QByteArray fontface_parse(const QByteArray &file, QString *error);

/* What an exported face says about itself. `name` goes into the header as
 * the string font_8x16_name ("Spleen 2.2.0", "Menlo"); `title` names the
 * font in the comment's first line ("Spleen 8x16 bitmap font"); `credits`
 * are the comment's lines after it, saying where the glyphs came from. */
struct FaceHeading {
	QString name;
	QString title;
	QStringList credits;
};

/* Spleen's heading, as the firmware's header has it. */
FaceHeading fontface_spleen_heading();

/* A face as the C header the firmware builds its built-in face from
 * (pico-thing-video's include/font_8x16.h), so it can replace that file as
 * it stands. The names are generic -- font_8x16.h, the array font_8x16 --
 * whatever the font, and font_8x16_name says which font it is. Given the
 * built-in face and Spleen's heading, the result is that file byte for
 * byte. */
QByteArray fontface_header(const QByteArray &face,
    const FaceHeading &heading);

/* Write a face to `path`: as that header, or else as a raw 4096-byte face
 * (what Font file loads, and what the protocol's Font buffers hold). */
bool fontface_export(const QByteArray &face, const QString &path,
    bool header, const FaceHeading &heading, QString *error);

/* Text drawn with a face, one pixel per bit: fg on bg, either of which may
 * be transparent (the image has an alpha channel). */
QImage fontface_sample(const QByteArray &face, const QString &text,
    QRgb fg, QRgb bg);
