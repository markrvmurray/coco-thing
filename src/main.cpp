/*
 * main.cpp
 *
 * coco-thing: a terminal for the Pico-Thing's USB CDC console that speaks
 * the video Pico's CoCo3/OS-9 control codes and $1B graphics.
 *
 *   coco-thing [port]      e.g. coco-thing cu.usbmodem1101
 */

#include <QApplication>

#include "mainwindow.h"

int
main(int argc, char *argv[])
{
	// The Control key must reach the guest as Ctrl, not as Command.
	QApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
	QApplication app(argc, argv);

	const QStringList args = QApplication::arguments();
	MainWindow w(args.size() > 1 ? args[1] : QString());
	w.show();
	return app.exec();
}
