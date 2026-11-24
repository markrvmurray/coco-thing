/*
 * test_link.cpp
 *
 * The link's reconnect behaviour, with no board attached. A pseudo-terminal
 * stands in for the board's CDC port, reached through a symlink standing in
 * for its device node: removing the link and closing the pty is the board
 * resetting, and pointing the link at a fresh pty is it enumerating again
 * under the same name.
 *
 * The same pty and symlink are exactly how USim's `usim09pt --console-pty`
 * publishes the emulator's console, so those tests cover it too, and a few
 * more follow its ways: a raw 8-bit line, and a new run replacing the link
 * by renaming over it rather than leaving a gap.
 */

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <poll.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>

#include "link.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static const int POLL_MS = 20;
static const int WAIT_MS = 2000;

static bool
wait_for(const std::function<bool()> &cond, int ms = WAIT_MS)
{
	QElapsedTimer t;

	t.start();
	while (!cond()) {
		if (t.elapsed() > ms)
			return false;
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return true;
}

/* Let the event loop run for a while with nothing expected to happen. */
static void
idle(int ms)
{
	wait_for([] { return false; }, ms);
}

/* The board: a pty whose slave is reachable at `node`. */
class Board {
public:
	explicit Board(const QString &node) : node(node) {}
	~Board() { unplug(); }

	void plug()
	{
		master = open_pty();
		if (symlink(ptsname(master), node.toLocal8Bit().constData()) != 0) {
			perror("symlink");
			exit(2);
		}
	}

	/* USim's next run: a new pty, published by renaming a fresh link over
	 * the old one, so the node never goes missing; and the old pty gone,
	 * as the last run has exited (or was killed, leaving its link). */
	void rerun()
	{
		const int next = open_pty();
		const QByteArray tmp = (node + ".new").toLocal8Bit();

		if (symlink(ptsname(next), tmp.constData()) != 0 ||
		    rename(tmp.constData(), node.toLocal8Bit().constData()) != 0) {
			perror("rerun");
			exit(2);
		}
		hangup();
		master = next;
	}

	void unplug()
	{
		unlink(node.toLocal8Bit().constData());
		if (master >= 0)
			::close(master);
		master = -1;
	}

	/* Close the far end but leave the node in place. */
	void hangup()
	{
		if (master >= 0)
			::close(master);
		master = -1;
	}

	void send(const char *s) { (void)::write(master, s, strlen(s)); }
	void send(const QByteArray &b)
	{
		(void)::write(master, b.constData(), (size_t)b.size());
	}

	/* What the host has written, waiting up to `ms` for it. */
	QByteArray take(int ms)
	{
		QByteArray out;
		struct pollfd p = { master, POLLIN, 0 };

		while (::poll(&p, 1, ms) > 0 && (p.revents & POLLIN)) {
			char buf[256];
			const ssize_t n = ::read(master, buf, sizeof(buf));
			if (n <= 0)
				break;
			out.append(buf, n);
			ms = 0;
		}
		return out;
	}

private:
	static int open_pty()
	{
		const int fd = posix_openpt(O_RDWR | O_NOCTTY);
		if (fd < 0 || grantpt(fd) != 0 || unlockpt(fd) != 0) {
			perror("pty");
			exit(2);
		}
		/* Non-blocking, so a test of broken code fails rather than
		 * hangs: writing to a master whose slave is not open blocks. */
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
		/* Raw 8-bit, as USim's PtySerial sets its pty: no echo, no
		 * CR/LF translation, no flow control or signal characters. */
		struct termios t;
		if (tcgetattr(fd, &t) == 0) {
			cfmakeraw(&t);
			(void)tcsetattr(fd, TCSANOW, &t);
		}
		return fd;
	}

	QString node;
	int master = -1;
};

struct Events {
	QByteArray rx;
	QByteArray tx;		/* what sent() reported */
	int connects = 0;
	int reconnects = 0;
	int losses = 0;
};

static void
watch(Link &link, Events &ev)
{
	QObject::connect(&link, &Link::received,
	    [&ev](const QByteArray &b) { ev.rx += b; });
	QObject::connect(&link, &Link::connected, [&ev](bool again) {
		ev.connects++;
		if (again)
			ev.reconnects++;
	});
	QObject::connect(&link, &Link::lost, [&ev] { ev.losses++; });
	QObject::connect(&link, &Link::sent,
	    [&ev](const QByteArray &b) { ev.tx += b; });
}

/* Bytes flow both ways over an open link. */
static void
check_traffic(Link &link, Board &board, Events &ev, const char *tag)
{
	ev.rx.clear();
	board.send(tag);
	CHECK(wait_for([&] { return ev.rx == tag; }));

	/* The port flushes from the event loop, so keep it running while the
	 * board collects. */
	const QByteArray keys = QByteArray("k") + tag;
	QByteArray got;
	ev.tx.clear();
	link.write(keys);
	CHECK(ev.tx == keys);		/* reported as it goes */
	CHECK(wait_for([&] { got += board.take(0); return got == keys; }));
}

/* The board resets while connected: the link notices, waits, and reopens the
 * same name when it comes back, reporting it as a reconnection. */
static void
test_reconnect_after_reset(const QString &node)
{
	Board board(node);
	Link link;
	Events ev;

	watch(link, ev);
	link.setPollInterval(POLL_MS);
	board.plug();
	link.open(node);
	CHECK(link.state() == Link::Open);
	CHECK(ev.connects == 1 && ev.reconnects == 0);
	check_traffic(link, board, ev, "before");

	board.unplug();
	CHECK(wait_for([&] { return link.state() == Link::Waiting; }));
	CHECK(ev.losses == 1);

	/* Gone for a while: the link keeps waiting and does not flap. */
	idle(10 * POLL_MS);
	CHECK(link.state() == Link::Waiting);
	CHECK(ev.losses == 1);

	board.plug();
	CHECK(wait_for([&] { return link.state() == Link::Open; }));
	CHECK(ev.connects == 2 && ev.reconnects == 1);
	check_traffic(link, board, ev, "after");

	link.close();
	board.unplug();
}

/* Started before the board: the link waits, then connects when it appears --
 * a first connection, not a reconnection. */
static void
test_waits_for_absent_board(const QString &node)
{
	Board board(node);
	Link link;
	Events ev;

	watch(link, ev);
	link.setPollInterval(POLL_MS);
	link.open(node);
	CHECK(link.state() == Link::Waiting);
	idle(5 * POLL_MS);
	CHECK(ev.connects == 0);

	board.plug();
	CHECK(wait_for([&] { return link.state() == Link::Open; }));
	CHECK(ev.connects == 1 && ev.reconnects == 0);
	check_traffic(link, board, ev, "late");

	link.close();
	board.unplug();
}

/* The two ways a loss is noticed each cover for the other on a real reset,
 * so each is tested alone. First the device node vanishing while the port
 * itself reports nothing: only the poll can see it. */
static void
test_loss_seen_by_vanished_node(const QString &node)
{
	Board board(node);
	Link link;
	Events ev;

	watch(link, ev);
	link.setPollInterval(POLL_MS);
	board.plug();
	link.open(node);
	CHECK(link.state() == Link::Open);

	unlink(node.toLocal8Bit().constData());	/* the pty stays alive */
	CHECK(wait_for([&] { return link.state() == Link::Waiting; }));
	CHECK(ev.losses == 1);

	link.close();
	board.unplug();
}

/* Then the other end going away while the node remains: only the port's
 * ResourceError can see it. */
static void
test_loss_seen_by_port_error(const QString &node)
{
	Board board(node);
	Link link;
	Events ev;

	watch(link, ev);
	link.setPollInterval(POLL_MS);
	board.plug();
	link.open(node);
	CHECK(link.state() == Link::Open);

	board.hangup();				/* the node stays */
	CHECK(wait_for([&] { return link.state() == Link::Waiting; }));
	CHECK(ev.losses == 1);

	link.close();
	board.unplug();
}

/* With auto-reconnect off, a loss closes the link, and so does opening a
 * port that is not there. */
static void
test_no_auto_reconnect(const QString &node)
{
	Board board(node);
	Link link;
	Events ev;

	watch(link, ev);
	link.setPollInterval(POLL_MS);
	link.setAutoReconnect(false);

	link.open(node);			/* not there yet */
	CHECK(link.state() == Link::Closed);
	CHECK(!link.lastError().isEmpty());

	board.plug();
	link.open(node);
	CHECK(link.state() == Link::Open);

	board.unplug();
	CHECK(wait_for([&] { return link.state() == Link::Closed; }));
	CHECK(ev.losses == 1);

	board.plug();
	idle(10 * POLL_MS);
	CHECK(link.state() == Link::Closed);
	CHECK(ev.connects == 1);
	board.unplug();
}

/* The virtual loopback plug: open at once with no device, everything
 * written comes back -- in order, and only from the event loop, never
 * inside the write -- and it is never lost or polled for. */
static void
test_loopback()
{
	Link link;
	Events ev;

	watch(link, ev);
	link.setPollInterval(POLL_MS);
	link.open(Link::LOOPBACK);
	CHECK(link.state() == Link::Open);
	CHECK(ev.connects == 1 && ev.reconnects == 0);
	CHECK(link.portName() == Link::LOOPBACK);

	link.write("abc");
	link.write(QByteArray("\x1B\x40\x00", 3));	/* binary passes too */
	CHECK(ev.tx == QByteArray("abc\x1B\x40\x00", 6));
	CHECK(ev.rx.isEmpty());				/* not inside write */
	CHECK(wait_for([&] { return ev.rx.size() == 6; }));
	CHECK(ev.rx == QByteArray("abc\x1B\x40\x00", 6));

	idle(10 * POLL_MS);				/* nothing to lose */
	CHECK(link.state() == Link::Open);
	CHECK(ev.losses == 0);

	/* Closed, it echoes nothing, not even what was queued. */
	ev.rx.clear();
	link.write("late");
	link.close();
	ev.tx.clear();
	link.write("after");
	CHECK(ev.tx.isEmpty());		/* not open: nothing sent or said */
	idle(5 * POLL_MS);
	CHECK(ev.rx.isEmpty());
	CHECK(link.state() == Link::Closed);

	/* Reopened, it echoes what is written now -- nothing left over from
	 * before the close, and nothing stuck behind it. */
	link.open(Link::LOOPBACK);
	link.write("again");
	CHECK(wait_for([&] { return !ev.rx.isEmpty(); }));
	idle(5 * POLL_MS);
	CHECK(ev.rx == "again");
	link.close();
}

/* Every byte value crosses unchanged both ways: the guest's stream is
 * binary ($1B graphics, GPLoad payloads), and CR, LF, XON, XOFF, the
 * interrupt characters and bit-7 keys are all data. Opening the port also
 * asserts DTR, which a pty cannot; that must not count as an error. */
static void
test_binary_both_ways(const QString &node)
{
	Board board(node);
	Link link;
	Events ev;
	QByteArray all;

	for (int i = 0; i < 256; i++)
		all += (char)i;
	watch(link, ev);
	link.setPollInterval(POLL_MS);
	board.plug();
	link.open(node);
	CHECK(link.state() == Link::Open);
	CHECK(link.lastError().isEmpty());

	board.send(all);
	CHECK(wait_for([&] { return ev.rx.size() >= all.size(); }));
	CHECK(ev.rx == all);

	QByteArray got;
	link.write(all);
	CHECK(wait_for([&] {
		got += board.take(0);
		return got.size() >= all.size();
	}));
	CHECK(got == all);

	idle(5 * POLL_MS);
	CHECK(link.state() == Link::Open);
	CHECK(ev.losses == 0);
	link.close();
}

/* The emulator quits, and a later run publishes a different pty at the same
 * path: the link waits through the gap and reopens the new one. */
static void
test_emulator_runs_again(const QString &node)
{
	Board board(node);
	Link link;
	Events ev;

	watch(link, ev);
	link.setPollInterval(POLL_MS);
	board.plug();
	link.open(node);
	check_traffic(link, board, ev, "first run");

	board.unplug();				/* `quit`: link and pty go */
	CHECK(wait_for([&] { return link.state() == Link::Waiting; }));
	board.rerun();
	CHECK(wait_for([&] { return link.state() == Link::Open; }));
	CHECK(ev.losses == 1 && ev.reconnects == 1);
	check_traffic(link, board, ev, "second run");
	link.close();
}

/* A new run renames its link over the old one with no gap, while the old
 * pty is gone: the path never stops existing, so only the old port's error
 * shows the loss, and the link must then open the new pty, not cling to
 * the old. */
static void
test_emulator_replaces_link(const QString &node)
{
	Board board(node);
	Link link;
	Events ev;

	watch(link, ev);
	link.setPollInterval(POLL_MS);
	board.plug();
	link.open(node);
	check_traffic(link, board, ev, "old run");

	board.rerun();
	CHECK(wait_for([&] { return ev.reconnects == 1; }));
	CHECK(ev.losses == 1);
	CHECK(link.state() == Link::Open);
	check_traffic(link, board, ev, "new run");
	link.close();
}

/* What Connect offers: the loopback plug first, the call-out devices but
 * not their tty.* twins, and the last port even when it is not listed (a
 * typed pty path, or a board not plugged in), which is then offered first;
 * with no last port, the board's usbmodem is. */
static void
test_choices()
{
	const QStringList ports = { "tty.usbmodem1101", "cu.usbmodem1101",
	    "tty.Bluetooth", "cu.Bluetooth", "cu.usbserial-A1" };
	int pick = -1;

	CHECK(Link::choices(ports, "", &pick) == QStringList({ Link::LOOPBACK,
	    "cu.usbmodem1101", "cu.Bluetooth", "cu.usbserial-A1" }));
	CHECK(pick == 1);

	CHECK(Link::choices({ "cu.Bluetooth" }, "", &pick) ==
	    QStringList({ Link::LOOPBACK, "cu.Bluetooth" }));
	CHECK(pick == 0);

	CHECK(Link::choices(ports, "cu.usbserial-A1", &pick).size() == 4);
	CHECK(pick == 3);

	const QStringList with_pty = Link::choices(ports, "/tmp/usim-console",
	    &pick);
	CHECK(with_pty.size() == 5 && with_pty.last() == "/tmp/usim-console");
	CHECK(pick == 4);

	CHECK(Link::choices({}, Link::LOOPBACK, &pick) ==
	    QStringList({ Link::LOOPBACK }));
	CHECK(pick == 0);
}

static void
test_typed_name()
{
	CHECK(Link::typedName("  /tmp/usim-console \n") == "/tmp/usim-console");
	CHECK(Link::typedName("cu.usbmodem1101") == "cu.usbmodem1101");
	CHECK(Link::typedName("~/usim-console") ==
	    QDir::homePath() + "/usim-console");
	CHECK(Link::typedName("~") == QDir::homePath());
	CHECK(Link::typedName("~other/x") == "~other/x");
	CHECK(Link::typedName("   ").isEmpty());
}

/* A port another program has open (QSerialPort's lock is held -- another
 * coco-thing, say) is waited for, with the reason given, and taken as soon
 * as it is let go. */
static void
test_port_in_use(const QString &node)
{
	Board board(node);
	Link first, second;
	Events ev;

	watch(second, ev);
	first.setPollInterval(POLL_MS);
	second.setPollInterval(POLL_MS);
	board.plug();
	first.open(node);
	CHECK(first.state() == Link::Open);

	second.open(node);
	CHECK(second.state() == Link::Waiting);
	CHECK(second.lastError().contains("lock"));
	idle(5 * POLL_MS);
	CHECK(second.state() == Link::Waiting && ev.connects == 0);

	first.close();
	CHECK(wait_for([&] { return second.state() == Link::Open; }));
	CHECK(ev.connects == 1 && second.lastError().isEmpty());
	check_traffic(second, board, ev, "freed");
	second.close();
}

/* A user's Disconnect is final: the board coming back does not reopen it. */
static void
test_close_stops_reconnecting(const QString &node)
{
	Board board(node);
	Link link;
	Events ev;

	watch(link, ev);
	link.setPollInterval(POLL_MS);
	board.plug();
	link.open(node);
	CHECK(link.state() == Link::Open);

	link.close();
	CHECK(link.state() == Link::Closed);
	board.unplug();
	board.plug();
	idle(10 * POLL_MS);
	CHECK(link.state() == Link::Closed);
	CHECK(ev.connects == 1 && ev.losses == 0);
	board.unplug();
}

int
main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;

	if (!dir.isValid()) {
		fprintf(stderr, "no temporary directory\n");
		return 2;
	}
	const QString node = dir.filePath("board");

	test_reconnect_after_reset(node);
	test_waits_for_absent_board(node);
	test_loss_seen_by_vanished_node(node);
	test_loss_seen_by_port_error(node);
	test_close_stops_reconnecting(node);
	test_no_auto_reconnect(node);
	test_loopback();
	test_binary_both_ways(node);
	test_emulator_runs_again(node);
	test_emulator_replaces_link(node);
	test_port_in_use(node);
	test_choices();
	test_typed_name();

	if (failures == 0)
		printf("test_link: all passed\n");
	return failures == 0 ? 0 : 1;
}
