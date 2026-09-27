/*
 * mainwindow.h
 *
 * The window: the screen, the serial link to the board's USB CDC console,
 * and the frame timer that runs the firmware and repaints.
 */

#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QSerialPort>
#include <QTimer>

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
	void readSerial();
	void serialError(QSerialPort::SerialPortError error);
	void sendKeys(const QByteArray &bytes);
	void tick();

private:
	void openPort(const QString &name);
	void showState();

	Screen *screen;
	Terminal terminal;
	QSerialPort serial;
	QTimer timer;
	QLabel *state;
};
