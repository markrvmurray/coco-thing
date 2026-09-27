/*
 * test_screen.cpp
 *
 * The panel outside the text area is shown grey, whatever the framebuffer
 * holds there, and exactly up to the area's edge at any scale.
 */

#include <cstdio>
#include <cstring>

#include <QApplication>
#include <QImage>

#include "screen.h"
#include "video.h"
#include "video_render.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static const QRgb RED = qRgb(255, 0, 0);	/* RGB332 $E0 */

static QImage
shot(Screen &s, int scale)
{
	s.resize(FB_WIDTH * scale, FB_HEIGHT * scale);
	return s.grab().toImage();
}

static bool
grey(const QImage &img, int x, int y)
{
	return img.pixel(x, y) == Screen::UNUSED.rgb();
}

int
main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	Screen s;

	/* Every framebuffer pixel red, so anything grey came from the screen. */
	memset(video_framebuffer, 0xE0, sizeof(video_framebuffer));
	s.setScaling(true, false);

	/* The whole panel: nothing grey. */
	QImage img = shot(s, 1);
	CHECK(img.pixel(900, 100) == RED);
	CHECK(img.pixel(1023, 479) == RED);

	/* 80x25: 640x400 pixels in the top left corner, the rest grey. */
	s.setTextArea(80, 25);
	for (int k = 1; k <= 2; k++) {
		img = shot(s, k);
		CHECK(img.pixel(0, 0) == RED);
		CHECK(img.pixel(639 * k, 399 * k) == RED);
		CHECK(grey(img, 640 * k, 0));		/* right of it */
		CHECK(grey(img, 0, 400 * k));		/* below it */
		CHECK(grey(img, 1023 * k, 479 * k));
	}

	if (failures == 0)
		printf("test_screen: all passed\n");
	return failures == 0 ? 0 : 1;
}
