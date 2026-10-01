/*
 * keyboard.cpp
 *
 * See keyboard.h.
 */

#include "keyboard.h"

#include <QKeyEvent>

#include "hid_cook.h"
#include "mackeys.h"

/* HID boot-keyboard report: modifiers, reserved, six key slots. The modifier
 * bits for the left-hand keys, which is all the cooker distinguishes. */
static const uint8_t MOD_CTRL = 0x01;
static const uint8_t MOD_SHIFT = 0x02;
static const uint8_t MOD_ALT = 0x04;
static const unsigned KEY_SLOTS = 6;
static const uint8_t USAGE_CAPS_LOCK = 0x39;

/* NSEventModifierFlagCapsLock, as Qt passes it in nativeModifiers(): the
 * lock's state, which macOS keeps. */
static const quint32 NS_CAPS_LOCK = 1u << 16;

Keyboard::Keyboard(std::function<bool()> is_iso)
	: iso(is_iso ? std::move(is_iso) : std::function<bool()>(mackeys_keyboard_is_iso))
{
}

uint8_t
Keyboard::modifiers(const QKeyEvent *ev) const
{
	const Qt::KeyboardModifiers m = ev->modifiers();
	uint8_t bits = 0;

	// main() keeps Control as ControlModifier; Command (Meta) never
	// reaches the cooker.
	if (m & Qt::ControlModifier)
		bits |= MOD_CTRL;
	if (m & Qt::ShiftModifier)
		bits |= MOD_SHIFT;
	if (m & Qt::AltModifier)
		bits |= MOD_ALT;
	return bits;
}

void
Keyboard::send(uint8_t m)
{
	uint8_t report[HID_KBD_REPORT_LEN] = { m, 0 };

	for (int i = 0; i < held.size() && i < (int)KEY_SLOTS; i++)
		report[2 + i] = held[i];
	hid_cook_keyboard(report, sizeof(report));
}

/* macOS keeps Caps Lock's state itself and tells us it with every event;
 * the cooker keeps its own, toggled by the key's usage. Tap the key in a
 * report whenever the two disagree, so they cannot drift. */
void
Keyboard::syncCapsLock(const QKeyEvent *ev)
{
	const bool mac = (ev->nativeModifiers() & NS_CAPS_LOCK) != 0;

	if (mac == hid_cook_caps_lock())
		return;
	held.append(USAGE_CAPS_LOCK);
	send(mods);
	held.removeAll(USAGE_CAPS_LOCK);
	send(mods);
}

bool
Keyboard::press(const QKeyEvent *ev)
{
	if (ev->isAutoRepeat())
		return true;		/* the cooker repeats */
	if (ev->modifiers() & Qt::MetaModifier)
		return false;		/* Command: the Mac's */

	syncCapsLock(ev);
	mods = modifiers(ev);

	const unsigned code = ev->nativeVirtualKey();
	const uint8_t usage = code == MACKEY_CAPS_LOCK ? 0 :
	    mackeys_usage(code, iso());

	if (usage != 0 && !held.contains(usage)) {
		// A seventh key is dropped rather than overflowing the report,
		// as a boot keyboard would.
		if (held.size() < (int)KEY_SLOTS)
			held.append(usage);
	}
	// Modifier keys have no usage but still change the report, and so
	// what a repeating key produces.
	send(mods);
	return true;
}

bool
Keyboard::release(const QKeyEvent *ev)
{
	if (ev->isAutoRepeat())
		return true;

	mods = modifiers(ev);

	const uint8_t usage = mackeys_usage(ev->nativeVirtualKey(), iso());
	held.removeAll(usage);
	send(mods);
	return true;
}

void
Keyboard::releaseAll()
{
	held.clear();
	mods = 0;
	send(mods);
}

void
Keyboard::tick(uint32_t now_ms)
{
	hid_cook_tick(now_ms);
}

void
Keyboard::reset()
{
	held.clear();
	mods = 0;
	hid_cook_reset();
}
