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

Terminal::Terminal(QObject *parent)
	: QObject(parent)
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
