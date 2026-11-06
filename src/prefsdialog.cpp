/*
 * prefsdialog.cpp
 *
 * See prefsdialog.h.
 */

#include "prefsdialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "fontface.h"

static const char *PREVIEW_TEXT[] = {
	"OS-9 Level Two  $1B graphics",
	"ABCDEFGHIJ abcdefghij 0123456789",
	"┌──┐ ░▒▓█ £¬ {}[]|~",
	"└──┘ ☺♥♦♣♠ éüç αßπ",	/* CP437 has ß, used for beta */
};

PrefsDialog::PrefsDialog(const Prefs &p, QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle("Preferences");

	QTabWidget *tabs = new QTabWidget(this);
	tabs->addTab(fontTab(p), "Font");
	tabs->addTab(displayTab(p), "Display");
	tabs->addTab(connectionTab(p), "Connection");

	QDialogButtonBox *buttons = new QDialogButtonBox(
	    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &PrefsDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &PrefsDialog::reject);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(tabs);
	layout->addWidget(buttons);

	updatePreview();
}

QWidget *
PrefsDialog::fontTab(const Prefs &p)
{
	QWidget *tab = new QWidget;

	font_builtin = new QRadioButton("Built-in (Spleen 8×16, as on the "
	    "DVI console)");
	font_system = new QRadioButton("System font:");
	font_file = new QRadioButton("Font file:");
	QButtonGroup *group = new QButtonGroup(tab);
	group->addButton(font_builtin);
	group->addButton(font_system);
	group->addButton(font_file);

	family = new QFontComboBox;
	family->setFontFilters(QFontComboBox::MonospacedFonts);
	if (!p.font_family.isEmpty())
		family->setCurrentFont(QFont(p.font_family));

	file = new QLineEdit(p.font_file);
	file->setPlaceholderText("raw 4096-byte face, or PSF 8×16");
	QPushButton *browse = new QPushButton("Browse…");

	keep_box = new QCheckBox("Keep the built-in box-drawing and block "
	    "glyphs");
	keep_box->setChecked(p.keep_box_glyphs);
	keep_box->setToolTip("An outline font rarely draws $B0-$DF to meet the "
	    "cell edges, so lines and boxes break up.");

	switch (p.font_source) {
	case Prefs::FontBuiltin: font_builtin->setChecked(true); break;
	case Prefs::FontSystem: font_system->setChecked(true); break;
	case Prefs::FontFile: font_file->setChecked(true); break;
	}

	preview = new QLabel;
	preview->setAlignment(Qt::AlignCenter);
	preview->setFrameShape(QFrame::StyledPanel);
	font_error = new QLabel;
	font_error->setWordWrap(true);
	font_error->setStyleSheet("color: #c0392b");

	// The selected face, saved as the firmware's own header (so it can
	// replace the built-in Spleen) or as a raw face.
	QPushButton *export_button = new QPushButton("Export\u2026");
	export_button->setToolTip("Save this face as font_8x16.h, the "
	    "header the firmware builds its built-in face from, or as a raw "
	    "4096-byte face.");
	export_note = new QLabel;
	export_note->setWordWrap(true);

	QLabel *note = new QLabel("Text cells are fixed at 8×16 pixels by "
	    "the renderer, so a font changes the shapes of the characters, not "
	    "their size. A program can still select its own font with the "
	    "$1B 3A Font command.");
	note->setWordWrap(true);

	QGridLayout *g = new QGridLayout(tab);
	g->addWidget(font_builtin, 0, 0, 1, 3);
	g->addWidget(font_system, 1, 0);
	g->addWidget(family, 1, 1, 1, 2);
	g->addWidget(font_file, 2, 0);
	g->addWidget(file, 2, 1);
	g->addWidget(browse, 2, 2);
	g->addWidget(keep_box, 3, 1, 1, 2);
	g->addWidget(preview, 4, 0, 1, 3);
	g->addWidget(export_note, 5, 0, 1, 2);
	g->addWidget(export_button, 5, 2);
	g->addWidget(font_error, 6, 0, 1, 3);
	g->addWidget(note, 7, 0, 1, 3);
	g->setRowStretch(8, 1);
	g->setColumnStretch(1, 1);

	connect(browse, &QPushButton::clicked, this, &PrefsDialog::browseFont);
	connect(export_button, &QPushButton::clicked, this,
	    &PrefsDialog::exportFace);
	connect(group, &QButtonGroup::buttonToggled, this,
	    &PrefsDialog::updatePreview);
	connect(family, &QFontComboBox::currentFontChanged, this,
	    &PrefsDialog::updatePreview);
	connect(file, &QLineEdit::editingFinished, this,
	    &PrefsDialog::updatePreview);
	connect(keep_box, &QCheckBox::toggled, this,
	    &PrefsDialog::updatePreview);
	return tab;
}

QWidget *
PrefsDialog::displayTab(const Prefs &p)
{
	QWidget *tab = new QWidget;

	scaling = new QComboBox;
	scaling->addItem("Fit to the window", Prefs::ScaleFit);
	scaling->addItem("Whole multiples only (sharp pixels)",
	    Prefs::ScaleInteger);
	scaling->setCurrentIndex(scaling->findData(p.scaling));

	smooth = new QCheckBox("Smooth the picture when scaling by a fraction");
	smooth->setChecked(p.smooth);

	text_cols = new QSpinBox;
	text_cols->setRange(20, 128);
	text_cols->setValue(p.text_cols);
	text_rows = new QSpinBox;
	text_rows->setRange(4, 30);
	text_rows->setValue(p.text_rows);
	QHBoxLayout *area = new QHBoxLayout;
	area->addWidget(text_cols);
	area->addWidget(new QLabel("columns \u00D7"));
	area->addWidget(text_rows);
	area->addWidget(new QLabel("rows"));
	area->addStretch(1);

	QLabel *note = new QLabel("Set the text area to the console "
	    "descriptor's size (IT.COL \u00D7 IT.ROW; 80 \u00D7 25 for the "
	    "Pico-Thing's Term). Programs are told that size, and some, such as "
	    "minted, send each screen row at exactly that width and rely on the "
	    "terminal wrapping there. Only the text area is shown, scaled to the "
	    "window; 128 \u00D7 30 is the whole panel. A change clears the "
	    "screen and the scroll-back.");
	note->setWordWrap(true);

	scrollback = new QSpinBox;
	scrollback->setRange(0, 5000);
	scrollback->setSingleStep(100);
	scrollback->setSuffix(" lines");
	scrollback->setSpecialValueText("off");
	scrollback->setValue(p.scrollback_lines);

	QFormLayout *f = new QFormLayout(tab);
	f->addRow("Text area:", area);
	f->addRow("", note);
	f->addRow("Scroll-back:", scrollback);
	f->addRow("Scaling:", scaling);
	f->addRow("", smooth);
	return tab;
}

QWidget *
PrefsDialog::connectionTab(const Prefs &p)
{
	QWidget *tab = new QWidget;

	auto_reconnect = new QCheckBox("Reconnect automatically when the board "
	    "resets");
	auto_reconnect->setChecked(p.auto_reconnect);
	clear_on_reconnect = new QCheckBox("Clear the screen when the board "
	    "reconnects");
	clear_on_reconnect->setChecked(p.clear_on_reconnect);
	connect_at_start = new QCheckBox("Connect to the last port at start-up");
	connect_at_start->setChecked(p.connect_at_start);

	QVBoxLayout *v = new QVBoxLayout(tab);
	v->addWidget(auto_reconnect);
	v->addWidget(clear_on_reconnect);
	v->addWidget(connect_at_start);
	v->addStretch(1);
	return tab;
}

Prefs
PrefsDialog::prefs() const
{
	Prefs p;

	p.font_source = font_system->isChecked() ? Prefs::FontSystem :
	    font_file->isChecked() ? Prefs::FontFile : Prefs::FontBuiltin;
	p.font_family = family->currentFont().family();
	p.font_file = file->text();
	p.keep_box_glyphs = keep_box->isChecked();
	p.scaling = (Prefs::Scaling)scaling->currentData().toInt();
	p.smooth = smooth->isChecked();
	p.text_cols = text_cols->value();
	p.text_rows = text_rows->value();
	p.scrollback_lines = scrollback->value();
	p.auto_reconnect = auto_reconnect->isChecked();
	p.clear_on_reconnect = clear_on_reconnect->isChecked();
	p.connect_at_start = connect_at_start->isChecked();
	return p;
}

void
PrefsDialog::browseFont()
{
	const QString path = QFileDialog::getOpenFileName(this, "Font File",
	    file->text(), "Fonts (*.psf *.psfu *.bin *.fnt);;"
	    "All files (*)");

	if (path.isEmpty())
		return;
	file->setText(path);
	font_file->setChecked(true);
	updatePreview();
}

void
PrefsDialog::updatePreview()
{
	const Prefs p = prefs();
	QString error;
	const QByteArray face = p.face(&error);

	family->setEnabled(font_system->isChecked());
	keep_box->setEnabled(font_system->isChecked());
	file->setEnabled(font_file->isChecked());
	font_error->setText(error);

	// Green on black, the console's own colours, at twice the real size.
	QImage sheet(8 * 32, 16 * 4, QImage::Format_RGB32);
	sheet.fill(qRgb(0, 0, 0));
	for (int i = 0; i < 4; i++) {
		const QImage line = fontface_sample(face,
		    QString::fromUtf8(PREVIEW_TEXT[i]), qRgb(0, 255, 0),
		    qRgb(0, 0, 0));
		for (int y = 0; y < line.height(); y++)
			for (int x = 0; x < line.width() && x < sheet.width(); x++)
				sheet.setPixel(x, i * 16 + y, line.pixel(x, y));
	}
	preview->setPixmap(QPixmap::fromImage(sheet.scaled(sheet.size() * 2)));
}

void
PrefsDialog::exportFace()
{
	const Prefs p = prefs();
	QString error;
	const QByteArray face = p.face(&error);

	if (!error.isEmpty()) {
		font_error->setText(error);
		return;
	}

	const QString header_type = "C header, as the firmware's built-in face "
	    "(*.h)";
	const QString raw_type = "Raw 4096-byte face (*.fnt)";
	QString type = header_type;
	const QString path = QFileDialog::getSaveFileName(this, "Export Font",
	    QDir::home().filePath("font_8x16.h"),
	    header_type + ";;" + raw_type, &type);
	if (path.isEmpty())
		return;
	const bool header = type != raw_type &&
	    !path.endsWith(".fnt", Qt::CaseInsensitive);

	if (!fontface_export(face, path, header, p.faceHeading(), &error)) {
		font_error->setText(QString("Could not export: %1").arg(error));
		return;
	}
	font_error->clear();
	export_note->setText(QString("Exported to %1").arg(path));
}

void
PrefsDialog::accept()
{
	QString error;

	if (prefs().font_source == Prefs::FontFile &&
	    prefs().face(&error).isEmpty()) {
		font_error->setText(error.isEmpty()
		    ? QString("Choose a font file, or another font source.")
		    : error);
		return;
	}
	QDialog::accept();
}
