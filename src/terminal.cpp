/*
 * terminal.cpp
 *
 * See terminal.h.
 */

#include "terminal.h"

#include <cstring>

#include <QElapsedTimer>

#include "fw_platform.h"
#include "video.h"
#include "video_render.h"
#include "vid_link.h"
// Angle brackets: the firmware's register-window class, found through its
// include directory.
#include <registers.h>

/* The window the terminal opens for a text area smaller than the panel.
 * Any id but 0 (the root, the panel itself) would do. */
static const uint8_t AREA_WINDOW = 1;

/* The one Terminal, for the firmware's scroll hook. */
static Terminal *the_terminal;

Terminal::Terminal(QObject *parent)
	: QObject(parent), area_cols(TEXT_COLS), area_rows(TEXT_ROWS)
{
	the_terminal = this;
	fw_platform_set_scroll(scrollHook);
	reset();
}

Terminal::~Terminal()
{
	if (the_terminal == this) {
		fw_platform_set_scroll(nullptr);
		the_terminal = nullptr;
	}
	// The renderer keeps a pointer to the face, which is about to go.
	vidterm_set_default_font(nullptr, 0);
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
	// The chosen face is the renderer's default face, which a reset
	// leaves alone: the console's reset selects it again.

	// Open the text area ahead of anything from the board, through the
	// same interpreter the board's own window commands use.
	feed(windowSetup());
}

void
Terminal::setTextArea(int cols, int rows)
{
	area_cols = qBound(1, cols, (int)TEXT_COLS);
	area_rows = qBound(1, rows, (int)TEXT_ROWS);
	// Lines of the old width would not fit the new area.
	clearScrollback();
}

void
Terminal::scrollHook(int col, int row, int cols, int rows)
{
	if (the_terminal != nullptr)
		the_terminal->scrolling(col, row, cols, rows);
}

/* The console is about to scroll a rectangle up one row. Only the whole text
 * area scrolling is output running off the top; a rectangle starting lower
 * is a line being deleted below the top, and belongs to the screen, not the
 * history. (Deleting the top line itself looks exactly like a scroll, so it
 * is kept too.) */
void
Terminal::scrolling(int col, int row, int cols, int rows)
{
	if (col != 0 || row != 0 || cols != area_cols || rows != area_rows)
		return;

	scrolled++;
	if (history_limit == 0)
		return;

	// The text layer is drawn lazily; bring the departing row's pixels up
	// to date before taking them.
	vidterm_refresh();

	Line line;
	uint8_t cells[TEXT_COLS * 2];
	vidterm_read_row(0, 0, area_cols, cells);
	line.cells.resize(area_cols);
	for (int c = 0; c < area_cols; c++)
		line.cells[c] = (char)cells[c * 2];

	const int width = area_cols * CHAR_W;
	line.pixels.resize(width * CHAR_H);
	for (int y = 0; y < CHAR_H; y++)
		memcpy(line.pixels.data() + y * width, video_framebuffer[y], width);

	history.append(line);
	while (history.size() > history_limit)
		history.removeFirst();
}

void
Terminal::setScrollbackLimit(int lines)
{
	history_limit = qMax(0, lines);
	while (history.size() > history_limit)
		history.removeFirst();
}

void
Terminal::clearScrollback()
{
	history.clear();
}

QByteArray
Terminal::lineCells(qint64 n) const
{
	const qint64 first = scrolled - history.size();

	if (n >= scrolled) {
		const qint64 row = n - scrolled;
		if (row >= area_rows)
			return {};
		uint8_t cells[TEXT_COLS * 2];
		QByteArray out(area_cols, ' ');
		vidterm_read_row(0, (int)row, area_cols, cells);
		for (int c = 0; c < area_cols; c++)
			out[c] = (char)cells[c * 2];
		return out;
	}
	if (n < first)
		return {};
	return history[(int)(n - first)].cells;
}

QByteArray
Terminal::linePixels(qint64 n) const
{
	const qint64 first = scrolled - history.size();

	if (n < first || n >= scrolled)
		return {};
	return history[(int)(n - first)].pixels;
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
	// The default face, not the selected one: whenever the firmware goes
	// back to "the built-in face" -- a guest's Font with buffer 0, a
	// killed font buffer, a console reset -- it comes back to this. A
	// face the guest has selected stays selected.
	if (face.isEmpty())
		vidterm_set_default_font(nullptr, 0);
	else
		vidterm_set_default_font(reinterpret_cast<const uint8_t *>(
		    face.constData()), (uint32_t)face.size());

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

QByteArray
Terminal::takeInput()
{
	QByteArray out;

	for (;;) {
		uint8_t stat, data;

		vid_link_stage();
		registers::copy_out(&stat, VID_STAT_OFFSET, 1);
		if (((stat >> VID_STAT_VER_LSB) & 7u) != VID_STAT_VERSION ||
		    (stat & VID_STAT_RDRF) == 0)
			break;
		// Serve, then pop: the bus-read ISR's order.
		registers::copy_out(&data, VID_DATA_OFFSET, 1);
		vid_link_guest_read(VID_DATA_OFFSET);
		out.append((char)data);
	}
	return out;
}

void
Terminal::frame()
{
	video_frames = video_frames + 1;
	vidterm_cursor_service();
}
