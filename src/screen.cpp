/*
 * screen.cpp
 *
 * See screen.h.
 */

#include "screen.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include "keyboard.h"
#include "screentext.h"
#include "video.h"
#include "video_render.h"

const QColor Screen::UNUSED(0x55, 0x55, 0x55);

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
	return QSize(FB_WIDTH, FB_HEIGHT);
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
	clearSelection();
}

QRect
Screen::panelRect() const
{
	QSize fit = image.size().scaled(size(), Qt::KeepAspectRatio);
	const int whole = qMin(width() / FB_WIDTH, height() / FB_HEIGHT);
	if (integer && whole >= 1)
		fit = image.size() * whole;

	return QRect(QPoint((width() - fit.width()) / 2,
	    (height() - fit.height()) / 2), fit);
}

QPoint
Screen::cellAt(const QPointF &pos) const
{
	const QRect panel = panelRect();
	const qreal x = (pos.x() - panel.left()) * FB_WIDTH / panel.width();
	const qreal y = (pos.y() - panel.top()) * FB_HEIGHT / panel.height();

	return QPoint(qBound(0, (int)(x / CHAR_W), area.width() / CHAR_W - 1),
	    qBound(0, (int)(y / CHAR_H), area.height() / CHAR_H - 1));
}

void
Screen::ordered(QPoint &from, QPoint &to) const
{
	const bool forward = anchor.y() < head.y() ||
	    (anchor.y() == head.y() && anchor.x() <= head.x());

	from = forward ? anchor : head;
	to = forward ? head : anchor;
}

QString
Screen::selectedText() const
{
	QPoint from, to;

	if (!selected)
		return {};
	ordered(from, to);
	return screentext_read(from.x(), from.y(), to.x(), to.y(), 0,
	    area.width() / CHAR_W - 1);
}

void
Screen::selectAll()
{
	anchor = QPoint(0, 0);
	head = QPoint(area.width() / CHAR_W - 1, area.height() / CHAR_H - 1);
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
	const QPoint cell = cellAt(ev->position());
	if (screentext_word(cell.x(), cell.y(), 0, area.width() / CHAR_W - 1,
	    &from, &to)) {
		anchor = QPoint(from, cell.y());
		head = QPoint(to, cell.y());
		selected = true;
		emit selectionMade();
	} else {
		selected = false;
	}
	update();
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

	const QRect target = panelRect();
	const QSize fit = target.size();
	p.setRenderHint(QPainter::SmoothPixmapTransform,
	    smooth && fit.width() % FB_WIDTH != 0);
	p.drawImage(target, image);

	// Drawn over the framebuffer at every paint, so nothing the board
	// sends -- a clear, graphics with the window's clip lifted -- can
	// paint the unused part of the panel.
	p.translate(target.topLeft());
	p.scale((qreal)fit.width() / FB_WIDTH, (qreal)fit.height() / FB_HEIGHT);
	p.fillRect(QRect(area.width(), 0, FB_WIDTH - area.width(), FB_HEIGHT),
	    UNUSED);
	p.fillRect(QRect(0, area.height(), area.width(),
	    FB_HEIGHT - area.height()), UNUSED);

	// The selection, in reverse video: each row from its first selected
	// cell to its last.
	if (selected) {
		QPoint from, to;
		const int right = area.width() / CHAR_W - 1;

		ordered(from, to);
		p.setCompositionMode(QPainter::CompositionMode_Difference);
		for (int row = from.y(); row <= to.y(); row++) {
			const int c0 = row == from.y() ? from.x() : 0;
			const int c1 = row == to.y() ? to.x() : right;
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
