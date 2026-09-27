/*
 * test_link.cpp
 *
 * The link's reconnect behaviour, with no board attached. A pseudo-terminal
 * stands in for the board's CDC port, reached through a symlink standing in
 * for its device node: removing the link and closing the pty is the board
 * resetting, and pointing the link at a fresh pty is it enumerating again
 * under the same name.
 */

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <poll.h>
#include <unistd.h>

#include <QCoreApplication>
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
		master = posix_openpt(O_RDWR | O_NOCTTY);
		if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
			perror("pty");
			exit(2);
		}
		/* Non-blocking, so a test of broken code fails rather than
		 * hangs: writing to a master whose slave is not open blocks. */
		fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
		if (symlink(ptsname(master), node.toLocal8Bit().constData()) != 0) {
			perror("symlink");
			exit(2);
		}
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
	QString node;
	int master = -1;
};

struct Events {
	QByteArray rx;
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
	link.write(keys);
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

	if (failures == 0)
		printf("test_link: all passed\n");
	return failures == 0 ? 0 : 1;
}
