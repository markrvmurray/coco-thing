/*
 * screen.cpp
 *
 * See screen.h.
 */

#include "screen.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "keyboard.h"
#include "screentext.h"
#include "terminal.h"
#include "video.h"
#include "video_render.h"

Screen::Screen(QWidget *parent)
	: QWidget(parent)
{
	// The image wraps the firmware's framebuffer in place, so a repaint
	// always shows what the renderer last drew; nothing is copied.
	image = QImage(&video_framebuffer[0][0], FB_WIDTH, FB_HEIGHT, FB_WIDTH,
	    QImage::Format_Indexed8);

	// RGB332 with the high bits replicated into the low ones, so 0xE0
	// reaches a full 0xFF channel, as the HSTX TMDS encoder treats it.
	QList<QRgb> lut(256);
	for (unsigned i = 0; i < 256; i++) {
		const unsigned r3 = (i >> 5) & 7, g3 = (i >> 2) & 7, b2 = i & 3;
		const unsigned r = (r3 << 5) | (r3 << 2) | (r3 >> 1);
		const unsigned g = (g3 << 5) | (g3 << 2) | (g3 >> 1);
		const unsigned b = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2;
		lut[i] = qRgb(r, g, b);
	}
	image.setColorTable(lut);

	area = QSize(FB_WIDTH, FB_HEIGHT);
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

QSize
Screen::sizeHint() const
{
	return area;
}

void
Screen::setScaling(bool i, bool s)
{
	integer = i;
	smooth = s;
	update();
}

void
Screen::setTextArea(int cols, int rows)
{
	area = QSize(cols * CHAR_W, rows * CHAR_H).boundedTo(image.size());
	updateGeometry();
	clearSelection();
}

QRect
Screen::areaRect() const
{
	QSize fit = area.scaled(size(), Qt::KeepAspectRatio);
	const int whole = qMin(width() / area.width(), height() / area.height());
	if (integer && whole >= 1)
		fit = area * whole;

	return QRect(QPoint(0, 0), fit);
}

QSize
Screen::fittedSize(int h) const
{
	if (integer)
		return area * qMax(1, h / area.height());
	return QSize(qRound((qreal)h * area.width() / area.height()), h);
}

void
Screen::setTerminal(Terminal *t)
{
	term = t;
	back = 0;
	seen = t != nullptr ? t->linesScrolled() : 0;
	clearSelection();
}

qint64
Screen::topLine() const
{
	if (term == nullptr)
		return 0;

	// Lines that scrolled off since we last looked: a view that is back
	// in the history moves back with them, so it keeps showing the same
	// lines; a live view stays live.
	const qint64 now = term->linesScrolled();
	if (back > 0)
		back += now - seen;
	seen = now;
	back = qMin(back, (qint64)term->historySize());
	return now - back;
}

qint64
Screen::scrolledBack() const
{
	topLine();
	return back;
}

void
Screen::scrollBy(qint64 lines)
{
	if (term == nullptr)
		return;
	topLine();
	back = qBound((qint64)0, back + lines, (qint64)term->historySize());
	update();
	emit viewChanged();
}

void
Screen::scrollToBottom()
{
	scrollBy(-scrolledBack());
}

void
Screen::scrollToTop()
{
	scrollBy(term != nullptr ? term->historySize() : 0);
}

int
Screen::areaCols() const
{
	return area.width() / CHAR_W;
}

int
Screen::areaRows() const
{
	return area.height() / CHAR_H;
}

int
Screen::pageRows() const
{
	return areaRows();
}

Screen::Pos
Screen::cellAt(const QPointF &pos) const
{
	const QRect shown = areaRect();
	const qreal x = (pos.x() - shown.left()) * area.width() / shown.width();
	const qreal y = (pos.y() - shown.top()) * area.height() / shown.height();
	Pos p;

	p.col = qBound(0, (int)(x / CHAR_W), areaCols() - 1);
	p.line = topLine() + qBound(0, (int)(y / CHAR_H), areaRows() - 1);
	return p;
}

void
Screen::ordered(Pos &from, Pos &to) const
{
	const bool forward = anchor.line < head.line ||
	    (anchor.line == head.line && anchor.col <= head.col);

	from = forward ? anchor : head;
	to = forward ? head : anchor;
}

QString
Screen::selectedText() const
{
	Pos from, to;
	QStringList lines;

	if (!selected || term == nullptr)
		return {};
	ordered(from, to);
	for (qint64 n = from.line; n <= to.line; n++) {
		const QByteArray cells = term->lineCells(n);

		if (cells.isEmpty())
			continue;	/* dropped from the history meanwhile */
		lines << screentext_cells(cells,
		    n == from.line ? from.col : 0,
		    n == to.line ? to.col : areaCols() - 1);
	}
	return lines.join('\n');
}

void
Screen::selectAll()
{
	if (term == nullptr)
		return;
	// Everything held: the history and the live screen.
	anchor.col = 0;
	anchor.line = term->linesScrolled() - term->historySize();
	head.col = areaCols() - 1;
	head.line = term->linesScrolled() + areaRows() - 1;
	selected = true;
	update();
}

void
Screen::clearSelection()
{
	selected = false;
	dragging = false;
	update();
}

void
Screen::mousePressEvent(QMouseEvent *ev)
{
	if (ev->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(ev);
		return;
	}
	// A click starts afresh; only a drag makes a selection.
	anchor = head = cellAt(ev->position());
	selected = false;
	dragging = true;
	update();
}

void
Screen::mouseMoveEvent(QMouseEvent *ev)
{
	if (!dragging)
		return;
	head = cellAt(ev->position());
	selected = head != anchor || selected;
	update();
}

void
Screen::mouseDoubleClickEvent(QMouseEvent *ev)
{
	int from, to;

	if (ev->button() != Qt::LeftButton) {
		QWidget::mouseDoubleClickEvent(ev);
		return;
	}
	// The first click's press already started afresh; a blank cell
	// leaves nothing selected.
	dragging = false;
	const Pos cell = cellAt(ev->position());
	const QByteArray cells = term != nullptr ? term->lineCells(cell.line) :
	    QByteArray();
	if (screentext_word(cells, cell.col, 0, areaCols() - 1, &from, &to)) {
		anchor.col = from;
		head.col = to;
		anchor.line = head.line = cell.line;
		selected = true;
		emit selectionMade();
	} else {
		selected = false;
	}
	update();
}

void
Screen::wheelEvent(QWheelEvent *ev)
{
	// A trackpad reports pixels, a wheel notches of 1/8 degree (120 a
	// notch, three lines a notch, as elsewhere). Up scrolls back.
	const qreal row_px = (qreal)areaRect().height() / areaRows();

	if (!ev->pixelDelta().isNull())
		wheel_rest += ev->pixelDelta().y() / row_px;
	else
		wheel_rest += ev->angleDelta().y() / 120.0 * 3;

	const qint64 lines = (qint64)wheel_rest;
	wheel_rest -= lines;
	if (lines != 0)
		scrollBy(lines);
	ev->accept();
}

void
Screen::mouseReleaseEvent(QMouseEvent *ev)
{
	if (ev->button() != Qt::LeftButton || !dragging)
		return;
	dragging = false;
	if (selected)
		emit selectionMade();
}

void
Screen::paintEvent(QPaintEvent *)
{
	QPainter p(this);

	p.fillRect(rect(), Qt::black);

	const QRect target = areaRect();
	const QSize fit = target.size();
	p.setRenderHint(QPainter::SmoothPixmapTransform,
	    smooth && fit.width() % area.width() != 0);

	// Everything below is drawn in panel pixels, clipped to the text area:
	// the rest of the panel is not shown.
	p.translate(target.topLeft());
	p.scale((qreal)fit.width() / area.width(),
	    (qreal)fit.height() / area.height());
	p.setClipRect(QRect(QPoint(0, 0), area));
	p.drawImage(QRect(QPoint(0, 0), area), image, QRect(QPoint(0, 0), area));

	// Scrolled back: the text area shows lines from the history, as they
	// looked when they scrolled off, above the top of the live screen.
	const qint64 top = topLine();
	const qint64 live = term != nullptr ? term->linesScrolled() : 0;
	if (back > 0) {
		for (int row = 0; row < areaRows(); row++) {
			const qint64 n = top + row;
			const QRect dst(0, row * CHAR_H, area.width(), CHAR_H);

			if (n >= live) {
				p.drawImage(dst, image, QRect(0,
				    (int)(n - live) * CHAR_H, area.width(),
				    CHAR_H));
				continue;
			}
			const QByteArray px = term->linePixels(n);
			if (px.size() != area.width() * CHAR_H) {
				p.fillRect(dst, Qt::black);
				continue;
			}
			QImage strip((const uchar *)px.constData(),
			    area.width(), CHAR_H, area.width(),
			    QImage::Format_Indexed8);
			strip.setColorTable(image.colorTable());
			p.drawImage(dst, strip);
		}
	}

	// The selection, in reverse video: each row from its first selected
	// cell to its last.
	if (selected) {
		Pos from, to;
		const int right = areaCols() - 1;

		ordered(from, to);
		p.setCompositionMode(QPainter::CompositionMode_Difference);
		for (int row = 0; row < areaRows(); row++) {
			const qint64 n = top + row;
			if (n < from.line || n > to.line)
				continue;
			const int c0 = n == from.line ? from.col : 0;
			const int c1 = n == to.line ? to.col : right;
			p.fillRect(QRect(c0 * CHAR_W, row * CHAR_H,
			    (c1 - c0 + 1) * CHAR_W, CHAR_H), Qt::white);
		}
	}
}

void
Screen::keyPressEvent(QKeyEvent *ev)
{
	if (keyboard == nullptr || !keyboard->press(ev)) {
		QWidget::keyPressEvent(ev);
		return;
	}
	emit keysTyped();
}

void
Screen::keyReleaseEvent(QKeyEvent *ev)
{
	if (keyboard == nullptr || !keyboard->release(ev)) {
		QWidget::keyReleaseEvent(ev);
		return;
	}
	emit keysTyped();
}

void
Screen::focusOutEvent(QFocusEvent *ev)
{
	// Keys released while another window has the keyboard never reach
	// us; let go of them all now so none keeps repeating.
	if (keyboard != nullptr) {
		keyboard->releaseAll();
		emit keysTyped();
	}
	QWidget::focusOutEvent(ev);
}

bool
Screen::focusNextPrevChild(bool)
{
	// Tab and Shift-Tab belong to the guest, not to focus traversal.
	return false;
}
