/*
 * keyboard.h
 *
 * The Mac keyboard as a USB keyboard. Key presses and releases become the
 * boot-keyboard reports a USB keyboard sends -- a modifier byte and up to six
 * held keys, by physical position -- and go to the video Pico firmware's own
 * HID cooker (hid_cook.cpp, compiled unchanged). Its bytes land in the
 * firmware's input FIFO, where Terminal::takeInput() reads them as the 6809
 * would. So the keys produce exactly what they produce on the DVI console:
 * the UK layout, vtio's arrow columns, F1-F12 as $80-$8B, Num Lock on the
 * keypad (the Mac's Clear key is in its place), and the cooker's own
 * auto-repeat, which waits while a key is still unread.
 *
 * Because keys go by position, the Mac's keyboard layout setting plays no
 * part; the result is what the same key does on a UK PC keyboard.
 */

#pragma once

#include <cstdint>
#include <functional>

#include <QList>

class QKeyEvent;

class Keyboard {
public:
	/* `is_iso` says whether the keyboard is ISO-shaped (see mackeys.h);
	 * the default asks the system. */
	explicit Keyboard(std::function<bool()> is_iso = {});

	/* Feed a key event. Returns true if it was taken (the caller should not
	 * pass it on). The Mac's own auto-repeat is ignored -- the cooker
	 * repeats -- and so is anything typed with Command held, which belongs
	 * to the Mac. */
	bool press(const QKeyEvent *ev);
	bool release(const QKeyEvent *ev);

	/* Every key up, as when the window loses the keyboard: a key released
	 * elsewhere must not keep repeating here. */
	void releaseAll();

	/* The cooker's repeat clock, in milliseconds. */
	void tick(uint32_t now_ms);

	/* Forget everything, the cooker's lock states included. */
	void reset();

private:
	void send(uint8_t mods);
	void syncCapsLock(const QKeyEvent *ev);
	uint8_t modifiers(const QKeyEvent *ev) const;

	std::function<bool()> iso;
	QList<uint8_t> held;	/* usages, oldest first, at most six */
	uint8_t mods = 0;
};
