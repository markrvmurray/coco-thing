/*
 * trafficpane.cpp
 *
 * See trafficpane.h.
 */

#include "trafficpane.h"

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtMath>
#include <QVBoxLayout>

TrafficPane::TrafficPane(QWidget *parent)
	: QWidget(parent)
{
	QVBoxLayout *col = new QVBoxLayout(this);
	col->setContentsMargins(0, 4, 0, 0);
	col->setSpacing(4);

	view = new QPlainTextEdit(this);
	view->setReadOnly(true);
	view->setLineWrapMode(QPlainTextEdit::NoWrap);
	// The keyboard belongs to the board: clicking here selects text to
	// copy, but typing still goes to the screen.
	view->setFocusPolicy(Qt::ClickFocus);
	QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	font.setPointSizeF(qMax(8.0, font.pointSizeF() * 1.2));
	view->setFont(font);
	col->addWidget(view, 1);

	QHBoxLayout *buttons = new QHBoxLayout;
	buttons->addStretch(1);
	QPushButton *clear = new QPushButton("Clear", this);
	QPushButton *save = new QPushButton("Save…", this);
	clear->setFocusPolicy(Qt::NoFocus);
	save->setFocusPolicy(Qt::NoFocus);
	buttons->addWidget(clear);
	buttons->addWidget(save);
	col->addLayout(buttons);

	connect(clear, &QPushButton::clicked, this, [this] {
		traffic.clear();
		refresh();
	});
	connect(save, &QPushButton::clicked, this, &TrafficPane::save);
	redraw();
}

void
TrafficPane::refresh()
{
	if (traffic.revision() != drawn)
		redraw();
}

/* Green from the board, red to it: darker on a light background and lighter
 * on a dark one, so both read. */
void
TrafficPane::redraw()
{
	const bool dark = palette().color(QPalette::Base).lightness() < 128;
	QTextCharFormat from, to;
	from.setForeground(dark ? QColor(0x5F, 0xD7, 0x5F)
	    : QColor(0x00, 0x7A, 0x00));
	to.setForeground(dark ? QColor(0xFF, 0x6B, 0x6B)
	    : QColor(0xC0, 0x00, 0x00));

	// Follow new traffic only while the view is at the bottom, so the
	// user can scroll up and read while more arrives.
	QScrollBar *bar = view->verticalScrollBar();
	const bool following = bar->value() == bar->maximum();
	const int kept = bar->value();

	view->clear();
	QTextCursor c(view->document());
	const QTextCharFormat plain;
	bool first = true;
	for (const TrafficLog::Line &l : traffic.lines()) {
		if (!first)
			c.insertBlock();
		first = false;
		// One run per stretch of the same direction.
		for (qsizetype i = 0; i < l.text.size();) {
			qsizetype j = i + 1;
			while (j < l.text.size() && l.dirs[j] == l.dirs[i])
				j++;
			const char d = l.dirs[i];
			c.insertText(l.text.mid(i, j - i),
			    d == TrafficLog::FromBoard ? from
			    : d == TrafficLog::ToBoard ? to : plain);
			i = j;
		}
	}
	bar->setValue(following ? bar->maximum() : kept);
	drawn = traffic.revision();
}

int
TrafficPane::lineWidth() const
{
	// A full row, the longest line the dump has, laid out as the view
	// lays out its text, plus the view's frame and scroll bar.
	QTextDocument doc;
	doc.setDefaultFont(view->font());
	doc.setDocumentMargin(view->document()->documentMargin());
	doc.setPlainText("00000000  " + QString(16 * 3 + 1, '0') + "  |" +
	    QString(16, '0') + "|");
	return qCeil(doc.idealWidth()) + 2 * view->frameWidth() +
	    view->verticalScrollBar()->sizeHint().width() + 4;
}

void
TrafficPane::changeEvent(QEvent *ev)
{
	QWidget::changeEvent(ev);
	if (ev->type() == QEvent::PaletteChange)
		redraw();
}

/* A path ending .h gets the C header, anything else the Python. */
bool
TrafficPane::saveTo(const QString &path, QString *error) const
{
	QFile f(path);

	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		*error = f.errorString();
		return false;
	}
	const QByteArray text = (path.endsWith(".h", Qt::CaseInsensitive)
	    ? traffic.cHeader() : traffic.python()).toUtf8();
	if (f.write(text) != text.size()) {
		*error = f.errorString();
		return false;
	}
	return true;
}

void
TrafficPane::save()
{
	static const QString PYTHON = "Python (*.py)",
	    HEADER = "C or C++ header (*.h)";
	QString filter = PYTHON;
	QString path = QFileDialog::getSaveFileName(this, "Save Traffic",
	    QDir::home().filePath("coco_traffic.py"),
	    PYTHON + ";;" + HEADER, &filter);
	if (path.isEmpty())
		return;
	// The type chosen decides the form, whatever the name typed.
	const QString ext = filter == HEADER ? ".h" : ".py";
	if (!path.endsWith(ext, Qt::CaseInsensitive))
		path += ext;
	QString error;
	if (!saveTo(path, &error))
		QMessageBox::warning(this, "Save Traffic",
		    QString("Cannot save %1: %2").arg(path, error));
}
