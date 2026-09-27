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

signals:
	void keyBytes(const QByteArray &bytes);

protected:
	void paintEvent(QPaintEvent *ev) override;
	void keyPressEvent(QKeyEvent *ev) override;
	bool focusNextPrevChild(bool next) override;

private:
	QImage image;
};
