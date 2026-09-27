/*
 * terminal.cpp
 *
 * See terminal.h.
 */

#include "terminal.h"

#include <QElapsedTimer>

#include "video.h"
#include "video_render.h"
#include "vid_link.h"
// Angle brackets: the firmware's register-window class, found through its
// include directory.
#include <registers.h>

/* The renderer's face pointer (video_render.cpp). Defined with external
 * linkage there but declared in no header, since on the board only the
 * guest's Font command sets it. NULL selects the built-in face. */
void vidterm_set_font(const uint8_t *data, uint32_t len);

/* The window the terminal opens for a text area smaller than the panel.
 * Any id but 0 (the root, the panel itself) would do. */
static const uint8_t AREA_WINDOW = 1;

Terminal::Terminal(QObject *parent)
	: QObject(parent), area_cols(TEXT_COLS), area_rows(TEXT_ROWS)
{
	reset();
}

void
Terminal::reset()
{
	// Bring the firmware up as the board does: renderer cleared, then the
	// link with READY and the protocol version staged.
	video_render_init();
	vidterm_refresh();
	registers::clear();
	vid_link_init();
	pending.clear();
	pending_pos = 0;

	// The link hands its console reset to the render side, which carries
	// it out on the next pass and selects the built-in face. Run that pass
	// now (the FIFO is empty, so it does nothing else), then put the
	// chosen face back.
	vid_link_render();
	applyFace();

	// Open the text area ahead of anything from the board, through the
	// same interpreter the board's own window commands use.
	feed(windowSetup());
}

void
Terminal::setTextArea(int cols, int rows)
{
	area_cols = qBound(1, cols, (int)TEXT_COLS);
	area_rows = qBound(1, rows, (int)TEXT_ROWS);
}

QByteArray
Terminal::windowSetup() const
{
	if (area_cols == TEXT_COLS && area_rows == TEXT_ROWS)
		return {};	/* the root is already the whole panel */

	// DWSet id, type, column, row, width, height, fg, bg (the renderer
	// uses the geometry; type 2 is the CoCo3's 80-column text and the
	// colours are its defaults), then make it both where text goes and
	// what is on screen.
	const uint8_t setup[] = {
		0x1B, 0x20, AREA_WINDOW, 0x02,
		0x00, 0x00,		/* the top left corner */
		(uint8_t)area_cols, (uint8_t)area_rows, 0x07, 0x00,
		0x1B, 0x21, AREA_WINDOW,
		0x1B, 0x26, AREA_WINDOW,
	};
	return QByteArray(reinterpret_cast<const char *>(setup), sizeof(setup));
}

void
Terminal::setFace(const QByteArray &f)
{
	face = f;
	applyFace();

	// Rewrite every text row unchanged: that marks each cell dirty, so the
	// next refresh draws the screen's text again in the new face.
	uint8_t row[TEXT_COLS * 2];
	for (int r = 0; r < TEXT_ROWS; r++) {
		vidterm_read_row(0, r, TEXT_COLS, row);
		vidterm_write_row(0, r, TEXT_COLS, row);
	}
	vidterm_refresh();
}

void
Terminal::applyFace()
{
	if (face.isEmpty())
		vidterm_set_font(nullptr, 0);
	else
		vidterm_set_font(reinterpret_cast<const uint8_t *>(
		    face.constData()), (uint32_t)face.size());
}

void
Terminal::feed(const QByteArray &bytes)
{
	// Compact once everything queued so far has been taken, so the
	// buffer does not grow for the life of the connection.
	if (pending_pos == pending.size()) {
		pending.clear();
		pending_pos = 0;
	}
	pending.append(bytes);
}

bool
Terminal::fill_fifo()
{
	bool moved = false;

	while (pending_pos < pending.size()) {
		uint8_t stat;

		// Reading Stat has no side effect, so no guest-read notice.
		registers::copy_out(&stat, VID_STAT_OFFSET, 1);
		if ((stat & VID_STAT_TDRE) == 0)
			break;

		// The write ISR's path for Data: store, then dispatch.
		const uint8_t b = (uint8_t)pending[pending_pos++];
		registers::write((uint16_t)VID_DATA_OFFSET) = b;
		vid_link_guest_write(VID_DATA_OFFSET, b);
		moved = true;

		// TDRE is restaged by vid_link_stage(), so it still reads set
		// here after the FIFO fills. Restage per byte to stay honest.
		vid_link_stage();
	}
	return moved;
}

bool
Terminal::pump(int budget_ms)
{
	QElapsedTimer t;
	bool worked = false;

	t.start();
	do {
		const bool fed = fill_fifo();
		const bool idle = vid_link_idle();

		// The board's order: render, cursor, then restage so the next
		// fill sees the room the render made.
		vid_link_render();
		vidterm_cursor_service();
		vid_link_stage();

		if (!fed && idle)
			break;
		worked = true;
	} while (t.elapsed() < budget_ms);
	return worked;
}

void
Terminal::frame()
{
	video_frames = video_frames + 1;
	vidterm_cursor_service();
}
