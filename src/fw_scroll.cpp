/*
 * fw_scroll.cpp
 *
 * The console's scrolls, intercepted so the line leaving the top can be kept
 * for scroll-back. The firmware has no hook for this, and its sources are
 * compiled unchanged, so the interception is at the link: CMake compiles
 * video_render.cpp with vidterm_scroll_region and vidterm_scroll_up renamed
 * to fw_*, and these definitions take their names. The console
 * (vid_console.cpp) is the only caller of either, so every scroll it makes
 * comes through here, and the screen is still unscrolled when the hook runs.
 *
 * If the renaming is ever lost, the link fails with both definitions
 * present, rather than scroll-back silently going quiet.
 */

#include "fw_platform.h"
#include "video.h"

extern "C" void fw_vidterm_scroll_region(int col, int row, int cols,
    int rows, uint8_t fill_ch, uint8_t fill_attr);
extern "C" void fw_vidterm_scroll_up(uint8_t fill_ch, uint8_t fill_attr);

static void (*scroll_hook)(int col, int row, int cols, int rows);

void
fw_platform_set_scroll(void (*hook)(int col, int row, int cols, int rows))
{
	scroll_hook = hook;
}

extern "C" void
vidterm_scroll_region(int col, int row, int cols, int rows, uint8_t fill_ch,
    uint8_t fill_attr)
{
	if (scroll_hook != nullptr && cols > 0 && rows > 0)
		scroll_hook(col, row, cols, rows);
	fw_vidterm_scroll_region(col, row, cols, rows, fill_ch, fill_attr);
}

extern "C" void
vidterm_scroll_up(uint8_t fill_ch, uint8_t fill_attr)
{
	if (scroll_hook != nullptr)
		scroll_hook(0, 0, TEXT_COLS, TEXT_ROWS);
	fw_vidterm_scroll_up(fill_ch, fill_attr);
}
