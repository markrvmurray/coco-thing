/*
 * keymap.cpp
 *
 * See keymap.h. Qt reports the Control key as Qt::ControlModifier because
 * main() sets Qt::AA_MacDontSwapCtrlAndMeta; Command stays with the menus.
 */

#include "keymap.h"

#include <cstdint>

#include <QKeyEvent>

/* vtio KeyTbl columns: plain, Shift, Ctrl. Shift wins when both are held,
 * as in vtio and the firmware's cooker. */
static const uint8_t arrow_bytes[4][3] = {
	{ 0x0C, 0x1C, 0x13 },	/* up */
	{ 0x0A, 0x1A, 0x12 },	/* down */
	{ 0x08, 0x18, 0x10 },	/* left */
	{ 0x09, 0x19, 0x11 },	/* right */
};

static const uint8_t KEY_FN_BASE = 0x80;

static QByteArray
one(uint8_t c)
{
	return QByteArray(1, (char)c);
}

QByteArray
keymap_translate(const QKeyEvent *ev)
{
	const Qt::KeyboardModifiers mod = ev->modifiers();
	const bool shift = (mod & Qt::ShiftModifier) != 0;
	const bool ctrl = (mod & Qt::ControlModifier) != 0;
	const int key = ev->key();

	int dir = -1;
	switch (key) {
	case Qt::Key_Up:	dir = 0; break;
	case Qt::Key_Down:	dir = 1; break;
	case Qt::Key_Left:	dir = 2; break;
	case Qt::Key_Right:	dir = 3; break;
	default:		break;
	}
	if (dir >= 0)
		return one(arrow_bytes[dir][shift ? 1 : ctrl ? 2 : 0]);

	// Function keys go to the guest to spend as it likes, and do not
	// repeat: a held F-key cycling windows is not a feature.
	if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
		if (ev->isAutoRepeat())
			return {};
		return one((uint8_t)(KEY_FN_BASE + (key - Qt::Key_F1)));
	}

	switch (key) {
	case Qt::Key_Return:
	case Qt::Key_Enter:	return one(0x0D);	/* the OS-9 line terminator */
	case Qt::Key_Backspace:	return one(0x08);
	case Qt::Key_Tab:	return one(0x09);
	case Qt::Key_Escape:	return one(0x1B);
	default:		break;
	}

	// Ctrl chords fold onto the C0 controls: letters and @[\]^_.
	if (ctrl) {
		if (key >= Qt::Key_A && key <= Qt::Key_Z)
			return one((uint8_t)(key - Qt::Key_A + 1));
		if (key >= Qt::Key_At && key <= Qt::Key_Underscore)
			return one((uint8_t)(key & 0x1F));
		return {};
	}

	// Printables: what the host keyboard layout typed, narrowed to the
	// guest's character set.
	QByteArray out;
	for (const QChar ch : ev->text()) {
		const char16_t u = ch.unicode();
		if (u == u'£')
			out.append((char)0x9C);		/* pound */
		else if (u == u'¬')
			out.append((char)0xAA);		/* not */
		else if (u >= 0x20 && u < 0x7F)
			out.append((char)u);
	}
	return out;
}
