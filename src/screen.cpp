/*
 * screen.cpp
 *
 * See screen.h.
 */

#include "screen.h"

#include <QKeyEvent>
#include <QPainter>

#include "keyboard.h"
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
	update();
}

void
Screen::paintEvent(QPaintEvent *)
{
	QPainter p(this);

	p.fillRect(rect(), Qt::black);

	QSize fit = image.size().scaled(size(), Qt::KeepAspectRatio);
	const int whole = qMin(width() / FB_WIDTH, height() / FB_HEIGHT);
	if (integer && whole >= 1)
		fit = image.size() * whole;

	QRect target(QPoint((width() - fit.width()) / 2,
	    (height() - fit.height()) / 2), fit);
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
