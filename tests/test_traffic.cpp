/*
 * test_traffic.cpp
 *
 * The traffic log: what it keeps, and that it lays the bytes out exactly as
 * `hexdump -C -v` lays out a file of them, with each byte's direction.
 */

#include <cstdio>

#include <QCoreApplication>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>

#include "traffic.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static QTemporaryDir *tmp;

/* What the system's hexdump makes of these bytes. */
static QString
hexdump(const QByteArray &bytes)
{
	const QString path = tmp->filePath("bytes");
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
		return "(cannot write)";
	f.close();
	QProcess p;
	p.start("hexdump", { "-C", "-v", path });
	if (!p.waitForFinished(5000))
		return "(no hexdump)";
	return QString::fromLatin1(p.readAllStandardOutput());
}

static QByteArray
every_byte()
{
	QByteArray b;
	for (int i = 0; i < 256; i++)
		b.append((char)i);
	return b;
}

/* The text is hexdump's, byte for byte, whatever mix of directions made
 * it: full rows, a short last row, a single byte, every byte value. */
static void
test_matches_hexdump()
{
	const QList<QByteArray> cases = {
		"A",
		"hello from the board\r\n",
		QByteArray("\x1B\x20\x08\x00\x00\x28\x18\x00\x02\x02\x05\x20"
		    "\x1B\x21\x1B\x40", 16),
		every_byte(),
		every_byte() + "tail",
	};

	for (const QByteArray &bytes : cases) {
		TrafficLog t;
		/* Split across the two directions, so direction does not
		 * change the layout. */
		t.add(TrafficLog::FromBoard, bytes.left(bytes.size() / 3));
		t.add(TrafficLog::ToBoard, bytes.mid(bytes.size() / 3, 5));
		t.add(TrafficLog::FromBoard, bytes.mid(bytes.size() / 3 + 5));
		const QString want = hexdump(bytes);
		CHECK(t.text() == want);
		if (t.text() != want)
			fprintf(stderr, "got:\n%s\nwant:\n%s\n",
			    qPrintable(t.text()), qPrintable(want));
	}
	CHECK(TrafficLog().text().isEmpty());
}

/* Every character of a byte -- its two hex digits and its ASCII -- carries
 * that byte's direction; the offset, spacing and bars carry none. */
static void
test_directions()
{
	TrafficLog t;
	t.add(TrafficLog::ToBoard, "ab");
	t.add(TrafficLog::FromBoard, "c");

	const QList<TrafficLog::Line> l = t.lines();
	CHECK(l.size() == 2);			/* the row, then the end */
	if (l.size() != 2)
		return;
	const QString &s = l[0].text;
	const QByteArray &d = l[0].dirs;
	CHECK(s.startsWith("00000000  61 62 63 "));
	CHECK(s.endsWith("  |abc|"));
	CHECK(d.size() == s.size());
	for (int i = 0; i < 8; i++)
		CHECK(d[i] == 0);			/* the offset */
	CHECK(d[10] == TrafficLog::ToBoard && d[11] == TrafficLog::ToBoard);
	CHECK(d[12] == 0);			/* the space */
	CHECK(d[13] == TrafficLog::ToBoard && d[14] == TrafficLog::ToBoard);
	CHECK(d[16] == TrafficLog::FromBoard && d[17] == TrafficLog::FromBoard);
	const int bar = (int)s.indexOf('|');
	CHECK(d[bar] == 0 && d[(int)s.size() - 1] == 0);
	CHECK(d[bar + 1] == TrafficLog::ToBoard);	/* a */
	CHECK(d[bar + 2] == TrafficLog::ToBoard);	/* b */
	CHECK(d[bar + 3] == TrafficLog::FromBoard);	/* c */
	CHECK(l[1].text == "00000003" && l[1].dirs == QByteArray(8, 0));

}

/* The newest 1,024 bytes are kept. Offsets keep counting, so a row keeps
 * its offset as older bytes drop out, and a first row missing its start is
 * blank there. */
static void
test_capacity()
{
	TrafficLog t;

	t.add(TrafficLog::ToBoard, QByteArray(10, 'k'));
	t.add(TrafficLog::FromBoard, QByteArray(TrafficLog::CAPACITY - 4, 'b'));
	CHECK(t.size() == TrafficLog::CAPACITY);
	CHECK(t.firstOffset() == 6);

	const QList<TrafficLog::Line> l = t.lines();
	CHECK(!l.isEmpty());
	if (!l.isEmpty()) {
		CHECK(l[0].text.startsWith("00000000  " + QString(6 * 3, ' ') +
		    "6b 6b  6b 6b 62 62"));
		CHECK(l[0].text.endsWith("  |      kkkkbbbbbb|"));
		CHECK(l.last().text == "00000406");
	}

	/* One byte more pushes the oldest out; the rows stay put. */
	t.add(TrafficLog::FromBoard, "z");
	CHECK(t.size() == TrafficLog::CAPACITY && t.firstOffset() == 7);
	CHECK(t.lines()[1].text.startsWith("00000010  "));

	/* A burst bigger than the log keeps its own tail. */
	TrafficLog u;
	u.add(TrafficLog::FromBoard, QByteArray(3000, 'x') + "end");
	CHECK(u.size() == TrafficLog::CAPACITY);
	CHECK(u.lines()[u.lines().size() - 2].text.contains("xend"));
	CHECK(u.lines().last().text == "00000bbb");

	/* Clearing starts the count again. */
	u.clear();
	u.add(TrafficLog::ToBoard, "q");
	CHECK(u.firstOffset() == 0 && u.lines().last().text == "00000001");
}

static void
test_revision()
{
	TrafficLog t;
	const quint64 r0 = t.revision();

	t.add(TrafficLog::ToBoard, "dir\r");
	CHECK(t.revision() != r0);
	const quint64 r1 = t.revision();
	t.add(TrafficLog::FromBoard, QByteArray());
	CHECK(t.revision() == r1);		/* nothing crossed */
	t.clear();
	CHECK(t.isEmpty() && t.revision() != r1);
}

/* Runs: consecutive bytes that went the same way, with their offsets. */
static void
test_runs()
{
	TrafficLog t;
	t.add(TrafficLog::ToBoard, "ab");
	t.add(TrafficLog::ToBoard, "c");
	t.add(TrafficLog::FromBoard, "xyz");
	t.add(TrafficLog::ToBoard, "\r");

	const QList<TrafficLog::Run> r = t.runs();
	CHECK(r.size() == 3);
	if (r.size() == 3) {
		CHECK(r[0].dir == TrafficLog::ToBoard && r[0].offset == 0 &&
		    r[0].bytes == "abc");
		CHECK(r[1].dir == TrafficLog::FromBoard && r[1].offset == 3 &&
		    r[1].bytes == "xyz");
		CHECK(r[2].dir == TrafficLog::ToBoard && r[2].offset == 6 &&
		    r[2].bytes == "\r");
	}
}

/* Traffic with every byte value, runs longer than a row, both directions,
 * and offsets that no longer start at 0. */
static TrafficLog
sample()
{
	TrafficLog t;
	t.add(TrafficLog::FromBoard, QByteArray(TrafficLog::CAPACITY, 'z'));
	t.add(TrafficLog::ToBoard, "dir\r");
	t.add(TrafficLog::FromBoard, every_byte());
	t.add(TrafficLog::ToBoard, QByteArray("\"\\'\0", 4));
	t.add(TrafficLog::FromBoard, "\x1B\x21");
	return t;
}

/* What a program reading the saved file should print for it: a line per
 * run, its direction, its offset in hex and its bytes in hex. */
static QString
expected(const TrafficLog &t)
{
	QString out;
	for (const TrafficLog::Run &r : t.runs())
		out += QString("%1 %2 %3\n")
		    .arg(r.dir == TrafficLog::FromBoard ? "from" : "to")
		    .arg(r.offset, 0, 16)
		    .arg(QString::fromLatin1(r.bytes.toHex()));
	return out;
}

static QString
run(const QString &program, const QStringList &args, int *status)
{
	QProcess p;
	p.setProcessChannelMode(QProcess::MergedChannels);
	p.start(program, args);
	if (!p.waitForFinished(60000)) {
		*status = -1;
		return "(did not finish)";
	}
	*status = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
	return QString::fromLocal8Bit(p.readAll());
}

static bool
write_file(const QString &path, const QString &text)
{
	QFile f(path);
	return f.open(QIODevice::WriteOnly) &&
	    f.write(text.toUtf8()) == text.toUtf8().size();
}

/* The Python form, run by Python: each entry is the run it stands for. */
static void
test_python()
{
	const TrafficLog t = sample();
	const QString mod = tmp->filePath("coco_traffic.py");
	CHECK(write_file(mod, t.python()));
	const QString script = tmp->filePath("read_traffic.py");
	CHECK(write_file(script,
	    "import sys\n"
	    "ns = {}\n"
	    "exec(open(sys.argv[1]).read(), ns)\n"
	    "for d, off, data in ns['traffic']:\n"
	    "    assert isinstance(data, bytes)\n"
	    "    print(d, format(off, 'x'), data.hex())\n"));
	int status = 0;
	const QString out = run("python3", { "-I", script, mod }, &status);
	CHECK(status == 0);
	CHECK(out == expected(t));
	if (out != expected(t))
		fprintf(stderr, "python said:\n%s\n", qPrintable(out.left(800)));

	/* Nothing logged is still a module, with an empty list. */
	CHECK(write_file(mod, TrafficLog().python()));
	CHECK(run("python3", { "-I", script, mod }, &status).isEmpty() &&
	    status == 0);
}

/* The header, compiled as C and as C++ with warnings as errors, then run:
 * each entry of the table is the run it stands for. */
static void
test_c_header()
{
	const TrafficLog t = sample();
	CHECK(write_file(tmp->filePath("coco_traffic.h"), t.cHeader()));
	const QString main_src = tmp->filePath("main.c");
	CHECK(write_file(main_src,
	    "#include <stdio.h>\n"
	    "#include \"coco_traffic.h\"\n"
	    "int main(void)\n"
	    "{\n"
	    "\tfor (size_t i = 0; i < traffic_runs; i++) {\n"
	    "\t\tconst struct traffic_run *r = &traffic[i];\n"
	    "\t\tprintf(\"%s %x \", r->dir == TRAFFIC_FROM_PT ? \"from\"\n"
	    "\t\t    : \"to\", (unsigned)r->offset);\n"
	    "\t\tfor (size_t j = 0; j < r->len; j++)\n"
	    "\t\t\tprintf(\"%02x\", r->data[j]);\n"
	    "\t\tprintf(\"\\n\");\n"
	    "\t}\n"
	    "\treturn 0;\n"
	    "}\n"));

	const struct { const char *compiler; QStringList lang; } builds[] = {
		{ TEST_CC, { "-x", "c", "-std=c99" } },
		{ TEST_CXX, { "-x", "c++", "-std=c++17" } },
	};
	for (const auto &b : builds) {
		const QString exe = tmp->filePath("read_traffic");
		int status = 0;
		const QString msg = run(b.compiler, QStringList() << b.lang <<
		    "-Wall" << "-Wextra" << "-Werror" << "-o" << exe << main_src,
		    &status);
		CHECK(status == 0);
		if (status != 0) {
			fprintf(stderr, "%s:\n%s\n", b.compiler,
			    qPrintable(msg.left(800)));
			continue;
		}
		const QString out = run(exe, {}, &status);
		CHECK(status == 0 && out == expected(t));
	}
}

int
main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	tmp = &dir;

	test_matches_hexdump();
	test_directions();
	test_capacity();
	test_revision();
	test_runs();
	test_python();
	test_c_header();

	if (failures == 0)
		printf("test_traffic: all passed\n");
	return failures == 0 ? 0 : 1;
}
