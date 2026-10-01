/*
 * screen.h
 *
 * Shows the firmware's 1024x480 RGB332 framebuffer, scaled to the widget
 * with the aspect ratio kept, and hands its key events to a Keyboard.
 */

#pragma once

#include <QImage>
#include <QWidget>

class Keyboard;

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

	/* Where key presses and releases go. */
	void setKeyboard(Keyboard *k) { keyboard = k; }

signals:
	/* The keyboard took an event; its bytes are waiting to be read. */
	void keysTyped();

protected:
	void paintEvent(QPaintEvent *ev) override;
	void keyPressEvent(QKeyEvent *ev) override;
	void keyReleaseEvent(QKeyEvent *ev) override;
	void focusOutEvent(QFocusEvent *ev) override;
	bool focusNextPrevChild(bool next) override;

private:
	QImage image;
	bool integer = false;
	bool smooth = true;
	QSize area;		/* in panel pixels */
	Keyboard *keyboard = nullptr;
};
