/*
 * traffic.h
 *
 * The last bytes to cross the link, each with its direction, for the traffic
 * pane: what the board sent and what was sent to it, interleaved in the order
 * they crossed. Only the most recent CAPACITY bytes are kept.
 *
 * They are laid out as `hexdump -C` lays out a file: an 8-digit hex offset,
 * 16 bytes a row in two groups of eight, then the ASCII between bars, and a
 * last line giving the offset after the final byte. The offset counts every
 * byte since the log began or was cleared, so a row keeps its offset as older
 * bytes drop out; rows are aligned to 16, and a first row whose earlier bytes
 * have dropped out is blank there. As with `hexdump -C -v`, repeated rows are
 * not squeezed to `*`. Each character of a line says which way its byte went,
 * so a view can colour it.
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

class TrafficLog {
public:
	enum Dir { FromBoard = 1, ToBoard = 2 };

	/* One line of the dump: its text, and for each character the Dir of
	 * the byte it shows, or 0 for the offset, spacing and bars. */
	struct Line {
		QString text;
		QByteArray dirs;
	};

	static const int CAPACITY = 1024;
	static const int ROW_BYTES = 16;

	void add(Dir dir, const QByteArray &bytes);
	void clear();

	/* The bytes held, and the offset of the oldest of them. */
	int size() const { return (int)data.size(); }
	bool isEmpty() const { return data.isEmpty(); }
	quint64 firstOffset() const { return first; }

	/* Bumped on every change, so a view redraws only when there is
	 * something new. */
	quint64 revision() const { return rev; }

	/* The dump, line by line: the rows, then the end offset. Empty when
	 * nothing is held. */
	QList<Line> lines() const;

	/* The dump as plain text, `hexdump -C` exactly. */
	QString text() const;

	/* The bytes as runs that went the same way, each with the offset of
	 * its first byte, oldest first: what is saved. */
	struct Run {
		Dir dir;
		quint64 offset;
		QByteArray bytes;
	};
	QList<Run> runs() const;

	/* The runs as a Python module: `traffic`, a list of
	 * (direction, offset, data) tuples, direction "from" (sent by the
	 * board) or "to" (sent to it) and data a bytes object. */
	QString python() const;

	/* The runs as a C or C++ header: an array of bytes per run, and
	 * `traffic`, a table of struct traffic_run giving each run's
	 * direction, offset, length and bytes, `traffic_runs` long. */
	QString cHeader() const;

private:
	QByteArray data;
	QByteArray dirs;	/* one Dir per byte of data */
	quint64 first = 0;	/* the offset of data[0] */
	quint64 rev = 0;
};
