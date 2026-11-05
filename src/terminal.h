/*
 * terminal.h
 *
 * The video Pico's firmware, run on the host with the serial link standing in
 * for the 6809. Received bytes are written to VID.Data exactly as the guest
 * would write them: one at a time, and only while TDRE says the next write is
 * accepted, because a write into a full FIFO is dropped and the stream is
 * framed. What the FIFO cannot take yet waits here.
 *
 * The firmware keeps its state in globals, so there is one Terminal.
 *
 * Scroll-back: each line the console scrolls off the top of the text area is
 * kept -- its character codes, for copying, and its pixels as the panel showed
 * them, for display -- up to a limit. Lines are numbered for good: line N is
 * the Nth ever to scroll off, and the live screen's row r is line
 * linesScrolled() + r, so a position in the history stays put as new lines
 * arrive and old ones are dropped.
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>

class Terminal : public QObject {
	Q_OBJECT

public:
	explicit Terminal(QObject *parent = nullptr);
	~Terminal() override;

	/* Queue bytes received from the board for the interpreter. */
	void feed(const QByteArray &bytes);

	/* Run the firmware for up to budget_ms: fill the command FIFO, drain
	 * and render. Returns true if anything was consumed or drawn. */
	bool pump(int budget_ms);

	/* Advance the blink clock by one frame; on the board this is vsync. */
	void frame();

	/* Power-on state: cleared screen, link up, backlog discarded. The
	 * chosen face (below) survives it. */
	void reset();

	/* The text area the board's programs are told about: the Term
	 * descriptor's IT.COL x IT.ROW, which SS.ScSiz reports over serial.
	 * Programs such as minted draw rows of exactly that many characters
	 * back to back and rely on the terminal wrapping at its right edge,
	 * so the terminal must wrap there too. Anything smaller than the
	 * panel (128x30) becomes a window in its top left corner. Takes
	 * effect at the next reset(). */
	void setTextArea(int cols, int rows);
	int textCols() const { return area_cols; }
	int textRows() const { return area_rows; }

	/* Draw text with `face` (the layout in fontface.h); empty for the
	 * firmware's built-in face. What is on screen is redrawn with it. */
	void setFace(const QByteArray &face);

	/* Everything waiting in the firmware's input FIFO -- the keyboard's
	 * cooked bytes -- read out as the 6809 reads it on the DVI console:
	 * VID.Stat checked for the protocol version and RDRF, then VID.Data,
	 * whose read pops the FIFO. */
	QByteArray takeInput();

	/* Scroll-back. The limit is in lines; 0 keeps none. Lowering it drops
	 * the oldest. */
	void setScrollbackLimit(int lines);
	void clearScrollback();
	qint64 linesScrolled() const { return scrolled; }
	/* Lines held: they are numbered linesScrolled() - historySize() up to
	 * linesScrolled() - 1. */
	int historySize() const { return history.size(); }
	/* Line n's character codes, area-wide: from the history or, from
	 * linesScrolled() on, the live screen. Empty if no longer held. */
	QByteArray lineCells(qint64 n) const;
	/* A held line's pixels: 16 rows of the area's width in pixels,
	 * RGB332, top row first. Empty if n is not in the history. */
	QByteArray linePixels(qint64 n) const;

	/* Bytes received but not yet accepted by the command FIFO. */
	qsizetype backlog() const { return pending.size() - pending_pos; }

private:
	bool fill_fifo();
	void applyFace();
	QByteArray windowSetup() const;
	void scrolling(int col, int row, int cols, int rows);
	static void scrollHook(int col, int row, int cols, int rows);

	struct Line {
		QByteArray cells;
		QByteArray pixels;
	};

	QByteArray pending;
	qsizetype pending_pos = 0;
	QByteArray face;	/* the renderer points into this */
	int area_cols;
	int area_rows;
	QList<Line> history;	/* oldest first */
	int history_limit = 0;
	qint64 scrolled = 0;	/* lines ever scrolled off */
};
