/*
 * screen.h
 *
 * Shows the text area -- the part of the firmware's 1024x480 RGB332
 * framebuffer the board's programs can draw in, in its top left corner --
 * scaled to the widget with the aspect ratio kept and anchored at the top
 * left (any spare space is at the right or the bottom, as in any terminal),
 * and hands its key events to a Keyboard. The rest of the panel is out of
 * use and not shown. The mouse selects text in the text area, as in any
 * terminal: a drag selects from cell to cell, running to the end of each row
 * between, and a double-click selects the word under the pointer.
 *
 * The view can be scrolled back into the Terminal's history (the wheel or
 * trackpad, or scrollBy()). History rows are drawn from the pixels kept when
 * they scrolled off; the view stays on the same lines while new output
 * arrives, until it is scrolled back to the bottom. Selections are held in
 * the Terminal's absolute line numbers, so they stay on their text too.
 */

#pragma once

#include <QImage>
#include <QWidget>

class Keyboard;
class Terminal;

class Screen : public QWidget {
	Q_OBJECT

public:
	explicit Screen(QWidget *parent = nullptr);

	QSize sizeHint() const override;

	/* The size this screen would be at `height` with no spare space: the
	 * text area's shape, at a whole multiple of it under whole-multiple
	 * scaling. What the window resizes to, to fit. */
	QSize fittedSize(int height) const;

	/* Integer: scale by whole multiples only, so every pixel is the same
	 * size. Smooth: filter when the scale is not a whole multiple. */
	void setScaling(bool integer, bool smooth);

	/* The text area, in the top left corner of the panel: what is shown. */
	void setTextArea(int cols, int rows);

	/* Where scroll-back and the text of lines come from. */
	void setTerminal(Terminal *t);

	/* Lines the view is scrolled back from the live screen: 0 is live. */
	qint64 scrolledBack() const;
	/* Scroll back (positive) or forward, clamped to the history. */
	void scrollBy(qint64 lines);
	void scrollToBottom();
	void scrollToTop();
	/* Rows in the text area: a page. */
	int pageRows() const;

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

	/* The view scrolled, or the history it scrolls through changed. */
	void viewChanged();

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
	void wheelEvent(QWheelEvent *ev) override;

private:
	QImage image;
	bool integer = false;
	bool smooth = true;
	QSize area;		/* in panel pixels */
	Keyboard *keyboard = nullptr;

	/* A cell: its column and the Terminal's absolute line number. */
	struct Pos {
		int col = 0;
		qint64 line = 0;
		bool operator==(const Pos &o) const
		{ return col == o.col && line == o.line; }
		bool operator!=(const Pos &o) const { return !(*this == o); }
	};

	/* Where the text area is drawn in the widget, and the cell under a
	 * widget position (clamped into the area). */
	QRect areaRect() const;
	Pos cellAt(const QPointF &pos) const;
	/* The selection's ends in reading order. */
	void ordered(Pos &from, Pos &to) const;
	/* The absolute line at the top of the view, after taking in any lines
	 * that have scrolled off since last asked. */
	qint64 topLine() const;
	int areaCols() const;
	int areaRows() const;

	Terminal *term = nullptr;
	mutable qint64 back = 0;	/* lines scrolled back */
	mutable qint64 seen = 0;	/* linesScrolled() when last taken in */
	qreal wheel_rest = 0;		/* part of a line the wheel has moved */

	bool selected = false;	/* anchor..head is a selection */
	bool dragging = false;
	Pos anchor, head;
};
