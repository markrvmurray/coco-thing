/*
 * screen.h
 *
 * Shows the firmware's 1024x480 RGB332 framebuffer, scaled to the widget
 * with the aspect ratio kept, and hands its key events to a Keyboard. The
 * mouse selects text in the text area, as in any terminal: a drag selects
 * from cell to cell, running to the end of each row between, and a
 * double-click selects the word under the pointer.
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

	bool hasSelection() const { return selected; }
	/* The selected text, as screentext_read() gives it. */
	QString selectedText() const;
	void selectAll();
	void clearSelection();

signals:
	/* The keyboard took an event; its bytes are waiting to be read. */
	void keysTyped();

	/* A drag or double-click finished with text selected. */
	void selectionMade();

protected:
	void paintEvent(QPaintEvent *ev) override;
	void keyPressEvent(QKeyEvent *ev) override;
	void keyReleaseEvent(QKeyEvent *ev) override;
	void focusOutEvent(QFocusEvent *ev) override;
	bool focusNextPrevChild(bool next) override;
	void mousePressEvent(QMouseEvent *ev) override;
	void mouseMoveEvent(QMouseEvent *ev) override;
	void mouseReleaseEvent(QMouseEvent *ev) override;
	void mouseDoubleClickEvent(QMouseEvent *ev) override;

private:
	QImage image;
	bool integer = false;
	bool smooth = true;
	QSize area;		/* in panel pixels */
	Keyboard *keyboard = nullptr;

	/* Where the panel is drawn in the widget, and the text-area cell under
	 * a widget position (clamped into the area). */
	QRect panelRect() const;
	QPoint cellAt(const QPointF &pos) const;
	/* The selection's ends in reading order. */
	void ordered(QPoint &from, QPoint &to) const;

	bool selected = false;	/* anchor..head is a selection */
	bool dragging = false;
	QPoint anchor, head;	/* cells: x = column, y = row */
};
