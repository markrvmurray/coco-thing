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
	/* The colour of the panel outside the text area. */
	static const QColor UNUSED;

	explicit Screen(QWidget *parent = nullptr);

	QSize sizeHint() const override;

	/* Integer: scale by whole multiples only, so every pixel is the same
	 * size. Smooth: filter when the scale is not a whole multiple. */
	void setScaling(bool integer, bool smooth);

	/* The text area in the top left corner of the panel; the rest of the
	 * panel is out of use and shown grey. */
	void setTextArea(int cols, int rows);

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
	QSize area;		/* in panel pixels */
};
