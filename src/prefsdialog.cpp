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
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "fontface.h"

static const char *PREVIEW_TEXT[] = {
	"OS-9 Level Two  $1B graphics",
	"ABCDEFGHIJ abcdefghij 0123456789",
	"┌──┐ ░▒▓█ £¬ {}[]|~",
	"└──┘ ☺♥♦♣♠ éüç αβπ",
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
	g->addWidget(font_error, 5, 0, 1, 3);
	g->addWidget(note, 6, 0, 1, 3);
	g->setRowStretch(7, 1);
	g->setColumnStretch(1, 1);

	connect(browse, &QPushButton::clicked, this, &PrefsDialog::browseFont);
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

	QFormLayout *f = new QFormLayout(tab);
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
