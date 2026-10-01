/*
 * test_keyboard.cpp
 *
 * Mac key events through the whole path -- Keyboard, the firmware's HID
 * cooker, its input FIFO, and Terminal::takeInput() reading it as the 6809
 * does -- against the bytes the cooker documents (hid_cook.cpp: the UK
 * layout, vtio's arrow columns, F1-F12 as $80-$8B, the keypad under Num
 * Lock, and its own repeat).
 */

#include <cstdio>

#include <Carbon/Carbon.h>
#include <QGuiApplication>
#include <QKeyEvent>

#include "keyboard.h"
#include "terminal.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static bool iso_keyboard;
static const quint32 NS_CAPS_LOCK = 1u << 16;

static Terminal *term;
static Keyboard *kbd;
static bool caps;		/* the Mac's Caps Lock state */

static QKeyEvent
event(QEvent::Type type, unsigned code, Qt::KeyboardModifiers mods,
    bool autorep = false)
{
	return QKeyEvent(type, Qt::Key_unknown, mods, 0, code,
	    caps ? NS_CAPS_LOCK : 0, QString(), autorep);
}

static void
down(unsigned code, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
	const QKeyEvent ev = event(QEvent::KeyPress, code, mods);
	kbd->press(&ev);
}

static void
up(unsigned code, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
	const QKeyEvent ev = event(QEvent::KeyRelease, code, mods);
	kbd->release(&ev);
}

/* One key pressed and released: the bytes it produced. */
static QByteArray
type(unsigned code, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
	down(code, mods);
	up(code, mods);
	return term->takeInput();
}

static QByteArray
bytes(std::initializer_list<int> b)
{
	QByteArray out;
	for (int v : b)
		out.append((char)v);
	return out;
}

static void
fresh()
{
	kbd->reset();
	caps = false;
	term->takeInput();
}

static void
test_letters_and_caps()
{
	fresh();
	CHECK(type(kVK_ANSI_A) == "a");
	CHECK(type(kVK_ANSI_A, Qt::ShiftModifier) == "A");

	/* The Mac's Caps Lock state is followed, not the key's events. */
	caps = true;
	CHECK(type(kVK_ANSI_Q) == "Q");
	CHECK(type(kVK_ANSI_Q, Qt::ShiftModifier) == "q");
	caps = false;
	CHECK(type(kVK_ANSI_Q) == "q");
}

static void
test_controls()
{
	fresh();
	CHECK(type(kVK_ANSI_C, Qt::ControlModifier) == bytes({ 0x03 }));
	CHECK(type(kVK_Return) == bytes({ 0x0D }));
	CHECK(type(kVK_ANSI_KeypadEnter) == bytes({ 0x0D }));
	CHECK(type(kVK_Delete) == bytes({ 0x08 }));
	CHECK(type(kVK_Escape) == bytes({ 0x1B }));
	CHECK(type(kVK_Tab) == bytes({ 0x09 }));
	CHECK(type(kVK_Space) == " ");
}

/* vtio's KeyTbl: plain, Shift, Ctrl, and Shift winning over Ctrl. */
static void
test_arrows()
{
	const struct { unsigned code; int plain, shift, ctrl; } arrows[] = {
		{ kVK_UpArrow,    0x0C, 0x1C, 0x13 },
		{ kVK_DownArrow,  0x0A, 0x1A, 0x12 },
		{ kVK_LeftArrow,  0x08, 0x18, 0x10 },
		{ kVK_RightArrow, 0x09, 0x19, 0x11 },
	};

	fresh();
	for (const auto &a : arrows) {
		CHECK(type(a.code) == bytes({ a.plain }));
		CHECK(type(a.code, Qt::ShiftModifier) == bytes({ a.shift }));
		CHECK(type(a.code, Qt::ControlModifier) == bytes({ a.ctrl }));
		CHECK(type(a.code, Qt::ShiftModifier | Qt::ControlModifier) ==
		    bytes({ a.shift }));
	}
}

static void
test_function_keys()
{
	const unsigned f[12] = { kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5,
	    kVK_F6, kVK_F7, kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12 };

	fresh();
	for (int i = 0; i < 12; i++)
		CHECK(type(f[i]) == bytes({ 0x80 + i }));
}

/* The UK layout, by position: what the keys do on a UK PC keyboard. */
static void
test_uk_layout()
{
	fresh();
	CHECK(type(kVK_ANSI_2, Qt::ShiftModifier) == "\"");
	CHECK(type(kVK_ANSI_3, Qt::ShiftModifier) == bytes({ 0x9C }));	/* £ */
	CHECK(type(kVK_ANSI_Quote, Qt::ShiftModifier) == "@");
	CHECK(type(kVK_ANSI_Backslash) == "#");
	CHECK(type(kVK_ANSI_Backslash, Qt::ShiftModifier) == "~");

	/* The key left of 1 and the key right of left Shift. */
	iso_keyboard = true;
	CHECK(type(kVK_ISO_Section) == "`");
	CHECK(type(kVK_ISO_Section, Qt::ShiftModifier) == bytes({ 0xAA }));	/* ¬ */
	CHECK(type(kVK_ANSI_Grave) == "\\");
	CHECK(type(kVK_ANSI_Grave, Qt::ShiftModifier) == "|");
	iso_keyboard = false;
	CHECK(type(kVK_ANSI_Grave) == "`");
}

/* Num Lock on (the default): digits. The Mac's Clear key is in Num Lock's
 * place and toggles it; off, the keypad is the arrows. */
static void
test_keypad()
{
	fresh();
	CHECK(type(kVK_ANSI_Keypad8) == "8");
	CHECK(type(kVK_ANSI_Keypad8, Qt::KeypadModifier) == "8");
	CHECK(type(kVK_ANSI_KeypadClear).isEmpty());
	CHECK(type(kVK_ANSI_Keypad8) == bytes({ 0x0C }));
	CHECK(type(kVK_ANSI_Keypad4) == bytes({ 0x08 }));
	CHECK(type(kVK_ANSI_KeypadClear).isEmpty());
	CHECK(type(kVK_ANSI_Keypad8) == "8");
}

/* Command chords belong to the Mac; the Mac's own repeat is ignored. */
static void
test_command_and_os_repeat()
{
	fresh();
	CHECK(type(kVK_ANSI_Q, Qt::MetaModifier).isEmpty());

	down(kVK_ANSI_A);
	CHECK(term->takeInput() == "a");
	const QKeyEvent rep = event(QEvent::KeyPress, kVK_ANSI_A,
	    Qt::NoModifier, true);
	kbd->press(&rep);
	CHECK(term->takeInput().isEmpty());

	/* An OS repeat's release is not the key coming up: the cooker must
	 * still be repeating it. */
	const QKeyEvent rep_up = event(QEvent::KeyRelease, kVK_ANSI_A,
	    Qt::NoModifier, true);
	kbd->release(&rep_up);
	kbd->tick(5000);
	kbd->tick(5600);
	CHECK(term->takeInput() == "a");
	up(kVK_ANSI_A);
}

/* The cooker's repeat: 500 ms, then every 50 ms; held back while the last
 * byte is unread; stopped by release, by releaseAll, and never for F-keys. */
static void
test_repeat()
{
	uint32_t t = 1000;

	fresh();
	down(kVK_ANSI_A);
	CHECK(term->takeInput() == "a");
	kbd->tick(t);			/* arms the delay */
	kbd->tick(t += 400);
	CHECK(term->takeInput().isEmpty());
	kbd->tick(t += 150);		/* 550 ms: due */
	CHECK(term->takeInput() == "a");

	/* Unread input holds the repeat back, and it does not catch up. */
	kbd->tick(t += 60);
	kbd->tick(t += 60);
	kbd->tick(t += 60);
	CHECK(term->takeInput() == "a");

	up(kVK_ANSI_A);
	kbd->tick(t += 1000);
	CHECK(term->takeInput().isEmpty());

	down(kVK_ANSI_B);
	CHECK(term->takeInput() == "b");
	kbd->tick(t += 10);
	kbd->releaseAll();
	kbd->tick(t += 1000);
	CHECK(term->takeInput().isEmpty());

	down(kVK_F1);
	CHECK(term->takeInput() == bytes({ 0x80 }));
	kbd->tick(t += 10);
	kbd->tick(t += 1000);
	CHECK(term->takeInput().isEmpty());
	up(kVK_F1);
}

/* A second key while one is held produces only itself. */
static void
test_rollover()
{
	fresh();
	down(kVK_ANSI_A);
	down(kVK_ANSI_S);
	CHECK(term->takeInput() == "as");
	up(kVK_ANSI_A);
	up(kVK_ANSI_S);
	CHECK(term->takeInput().isEmpty());
}

int
main(int argc, char *argv[])
{
	QGuiApplication app(argc, argv);
	Terminal t;
	Keyboard k([] { return iso_keyboard; });

	term = &t;
	kbd = &k;

	test_letters_and_caps();
	test_controls();
	test_arrows();
	test_function_keys();
	test_uk_layout();
	test_keypad();
	test_command_and_os_repeat();
	test_repeat();
	test_rollover();

	if (failures == 0)
		printf("test_keyboard: all passed\n");
	return failures == 0 ? 0 : 1;
}
