/*
 * screen.cpp
 *
 * See screen.h.
 */

#include "screen.h"

#include <QKeyEvent>
#include <QPainter>

#include "keymap.h"
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

	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

QSize
Screen::sizeHint() const
{
	return QSize(FB_WIDTH, FB_HEIGHT);
}

void
Screen::paintEvent(QPaintEvent *)
{
	QPainter p(this);

	p.fillRect(rect(), Qt::black);

	QSize fit = image.size().scaled(size(), Qt::KeepAspectRatio);
	QRect target(QPoint((width() - fit.width()) / 2,
	    (height() - fit.height()) / 2), fit);
	p.setRenderHint(QPainter::SmoothPixmapTransform,
	    fit.width() % FB_WIDTH != 0);
	p.drawImage(target, image);
}

void
Screen::keyPressEvent(QKeyEvent *ev)
{
	const QByteArray bytes = keymap_translate(ev);

	if (bytes.isEmpty()) {
		QWidget::keyPressEvent(ev);
		return;
	}
	emit keyBytes(bytes);
}

bool
Screen::focusNextPrevChild(bool)
{
	// Tab and Shift-Tab belong to the guest, not to focus traversal.
	return false;
}
