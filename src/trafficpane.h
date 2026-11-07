/*
 * trafficpane.h
 *
 * The traffic pane: the link's recent bytes as `hexdump -C` shows a file,
 * each byte green if the board sent it and red if it was sent to the board,
 * in a fixed-width font. Save… writes the bytes, with their directions, as a
 * Python module or a C/C++ header, for a program to use (see traffic.h).
 */

#pragma once

#include <QPlainTextEdit>
#include <QWidget>

#include "traffic.h"

class TrafficPane : public QWidget {
	Q_OBJECT

public:
	explicit TrafficPane(QWidget *parent = nullptr);

	TrafficLog &log() { return traffic; }

	/* Redraw if anything has crossed since the last call; the frame
	 * timer calls this, so a burst redraws once, not per byte. */
	void refresh();

	/* Save to `path`: the C/C++ header if it ends .h, otherwise the
	 * Python module. False, with *error set, on failure. */
	bool saveTo(const QString &path, QString *error) const;

	/* The width that shows a whole line of the dump. */
	int lineWidth() const;

	/* The text as shown, for tests. */
	QString text() const { return view->toPlainText(); }

protected:
	void changeEvent(QEvent *ev) override;

private slots:
	void save();

private:
	void redraw();

	TrafficLog traffic;
	QPlainTextEdit *view;
	quint64 drawn = ~0ull;	/* the log's revision on screen */
};
