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
 */

#pragma once

#include <QByteArray>
#include <QObject>

class Terminal : public QObject {
	Q_OBJECT

public:
	explicit Terminal(QObject *parent = nullptr);

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

	/* Bytes received but not yet accepted by the command FIFO. */
	qsizetype backlog() const { return pending.size() - pending_pos; }

private:
	bool fill_fifo();
	void applyFace();
	QByteArray windowSetup() const;

	QByteArray pending;
	qsizetype pending_pos = 0;
	QByteArray face;	/* the renderer points into this */
	int area_cols;
	int area_rows;
};
