/*
 * traffic.cpp
 *
 * See traffic.h.
 */

#include "traffic.h"

#include <QStringList>

void
TrafficLog::add(Dir dir, const QByteArray &bytes)
{
	if (bytes.isEmpty())
		return;
	data += bytes;
	dirs += QByteArray(bytes.size(), (char)dir);
	if (data.size() > CAPACITY) {
		const qsizetype drop = data.size() - CAPACITY;
		data.remove(0, drop);
		dirs.remove(0, drop);
		first += (quint64)drop;
	}
	rev++;
}

void
TrafficLog::clear()
{
	data.clear();
	dirs.clear();
	first = 0;
	rev++;
}

static QString
offset(quint64 at)
{
	return QString("%1").arg(at, 8, 16, QChar('0'));
}

/* Append `s`, every character of it shown as direction `d`. */
static void
put(TrafficLog::Line &l, const QString &s, char d = 0)
{
	l.text += s;
	l.dirs += QByteArray(s.size(), d);
}

QList<TrafficLog::Line>
TrafficLog::lines() const
{
	QList<Line> out;

	if (data.isEmpty())
		return out;
	const quint64 end = first + (quint64)data.size();
	for (quint64 row = first - first % ROW_BYTES; row < end;
	    row += ROW_BYTES) {
		Line l;
		QString ascii;
		QByteArray ascii_dirs;

		put(l, offset(row) + " ");
		for (int i = 0; i < ROW_BYTES; i++) {
			const quint64 at = row + (quint64)i;
			put(l, i == ROW_BYTES / 2 ? "  " : " ");
			if (at < first || at >= end) {
				put(l, "  ");
				// Leading blanks keep the ASCII in its
				// columns; at the end hexdump just stops.
				if (at < first) {
					ascii += ' ';
					ascii_dirs += '\0';
				}
				continue;
			}
			const uint8_t b = (uint8_t)data[(qsizetype)(at - first)];
			const char d = dirs[(qsizetype)(at - first)];
			put(l, QString("%1").arg(b, 2, 16, QChar('0')), d);
			ascii += b >= 0x20 && b < 0x7F ? QChar(b) : QChar('.');
			ascii_dirs += d;
		}
		put(l, "  |");
		l.text += ascii;
		l.dirs += ascii_dirs;
		put(l, "|");
		out.append(l);
	}
	Line last;
	put(last, offset(end));
	out.append(last);
	return out;
}

QString
TrafficLog::text() const
{
	QString out;

	for (const Line &l : lines())
		out += l.text + "\n";
	return out;
}

QList<TrafficLog::Run>
TrafficLog::runs() const
{
	QList<Run> out;

	for (qsizetype i = 0; i < data.size(); i++) {
		const Dir d = (Dir)dirs[i];
		if (out.isEmpty() || out.last().dir != d)
			out.append({ d, first + (quint64)i, QByteArray() });
		out.last().bytes += data[i];
	}
	return out;
}

/* A run's bytes as rows of hex, ROW_BYTES a row: each byte written as
 * `before` it, its two digits, then `after` it; each row as `open` it, its
 * bytes and `close`, the rows joined by newlines. */
static QString
hex_rows(const QByteArray &bytes, const QString &before, const QString &after,
    const QString &open, const QString &close)
{
	QStringList rows;

	for (qsizetype i = 0; i < bytes.size(); i += TrafficLog::ROW_BYTES) {
		QStringList cells;
		for (qsizetype j = i; j < bytes.size() &&
		    j < i + TrafficLog::ROW_BYTES; j++)
			cells << before + QString("%1").arg((uint8_t)bytes[j], 2,
			    16, QChar('0')) + after;
		rows << open + cells.join(' ') + close;
	}
	return rows.join('\n');
}

QString
TrafficLog::python() const
{
	QString out =
	    "# coco-thing link traffic: the bytes in the order they crossed.\n"
	    "# Each entry is (direction, offset, data): direction is \"from\"\n"
	    "# for bytes the board sent and \"to\" for bytes sent to it; offset\n"
	    "# counts the bytes before it, both ways, since the log began or\n"
	    "# was cleared.\n"
	    "\n"
	    "traffic = [\n";

	for (const Run &r : runs()) {
		// Adjacent string literals join, so a long run wraps.
		out += QString("    (\"%1\", 0x%2, bytes.fromhex(\n")
		    .arg(r.dir == FromBoard ? "from" : "to")
		    .arg(r.offset, 8, 16, QChar('0'));
		out += hex_rows(r.bytes, "", "", "        \"", " \"");
		out += ")),\n";
	}
	out += "]\n";
	return out;
}

QString
TrafficLog::cHeader() const
{
	const QList<Run> all = runs();
	QString out =
	    "/*\n"
	    " * coco-thing link traffic: the bytes in the order they crossed,\n"
	    " * as runs that went the same way. TRAFFIC_FROM_PT runs were sent\n"
	    " * by the board, TRAFFIC_TO_PT runs to it. A run's offset counts\n"
	    " * the bytes before it, both ways, since the log began or was\n"
	    " * cleared. Valid C and C++.\n"
	    " */\n"
	    "\n"
	    "#pragma once\n"
	    "\n"
	    "#include <stddef.h>\n"
	    "#include <stdint.h>\n"
	    "\n"
	    "enum traffic_dir { TRAFFIC_FROM_PT = 1, TRAFFIC_TO_PT = 2 };\n"
	    "\n"
	    "struct traffic_run {\n"
	    "\tenum traffic_dir dir;\n"
	    "\tuint32_t offset;\n"
	    "\tsize_t len;\n"
	    "\tconst uint8_t *data;\n"
	    "};\n";

	for (qsizetype i = 0; i < all.size(); i++)
		out += QString("\nstatic const uint8_t traffic_run%1[] = {\n")
		    .arg(i) + hex_rows(all[i].bytes, "0x", ",", "\t", "") +
		    "\n};\n";
	out += "\nstatic const struct traffic_run traffic[] = {\n";
	for (qsizetype i = 0; i < all.size(); i++)
		out += QString("\t{ %1, 0x%2, sizeof traffic_run%3, "
		    "traffic_run%3 },\n")
		    .arg(all[i].dir == FromBoard ? "TRAFFIC_FROM_PT"
		      : "TRAFFIC_TO_PT")
		    .arg(all[i].offset, 8, 16, QChar('0'))
		    .arg(i);
	if (all.isEmpty())
		out += "\t{ TRAFFIC_FROM_PT, 0, 0, NULL },\n";
	out += "};\n\n";
	out += QString("static const size_t traffic_runs = %1;\n")
	    .arg(all.size());
	return out;
}
