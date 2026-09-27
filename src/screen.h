/*
 * screen.h
 *
 * Shows the firmware's 1024x480 RGB332 framebuffer, scaled to the widget
 * with the aspect ratio kept, and turns key presses into guest bytes.
 */

#pragma once

#include <QImage>
#include <QWidget>

class Screen : public QWidget {
	Q_OBJECT

public:
	explicit Screen(QWidget *parent = nullptr);

	QSize sizeHint() const override;

	/* Integer: scale by whole multiples only, so every pixel is the same
	 * size. Smooth: filter when the scale is not a whole multiple. */
	void setScaling(bool integer, bool smooth);

signals:
	void keyBytes(const QByteArray &bytes);

protected:
	void paintEvent(QPaintEvent *ev) override;
	void keyPressEvent(QKeyEvent *ev) override;
	bool focusNextPrevChild(bool next) override;

private:
	QImage image;
	bool integer = false;
	bool smooth = true;
};
