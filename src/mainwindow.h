/*
 * mainwindow.h
 *
 * The window: the screen, the link to the board's USB CDC console, and the
 * frame timer that runs the firmware and repaints.
 */

#pragma once

#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QScrollBar>
#include <QTimer>

#include "keyboard.h"
#include "link.h"
#include "prefs.h"
#include "screen.h"
#include "terminal.h"

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(const QString &port_name, QWidget *parent = nullptr);
	~MainWindow() override;

protected:
	void closeEvent(QCloseEvent *ev) override;

private slots:
	void choosePort();
	void editPrefs();
	void disconnectPort();
	void resetScreen();
	void linkConnected(bool again);
	void linkLost();
	void sendKeys();
	void tick();
	void copy();
	void cut();
	void paste();
	void selectAll();
	void syncScrollBar();

private:
	void showState();
	void applyPrefs();

	Screen *screen;
	QScrollBar *scroll_bar;
	Terminal terminal;
	Link link;
	Keyboard keyboard;
	QElapsedTimer clock;	/* the keyboard's repeat clock */
	QTimer timer;
	QLabel *state;
	Prefs prefs;
	QByteArray pasting;	/* pasted bytes still to send */
};
