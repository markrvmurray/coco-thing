/*
 * mainwindow.h
 *
 * The window: the screen, the link to the board's USB CDC console, and the
 * frame timer that runs the firmware and repaints.
 */

#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QTimer>

#include "link.h"
#include "screen.h"
#include "terminal.h"

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(const QString &port_name, QWidget *parent = nullptr);

private slots:
	void choosePort();
	void disconnectPort();
	void resetScreen();
	void linkConnected(bool again);
	void linkLost();
	void sendKeys(const QByteArray &bytes);
	void tick();

private:
	void showState();

	Screen *screen;
	Terminal terminal;
	Link link;
	QTimer timer;
	QLabel *state;
};
