/*
 * mainwindow.cpp
 *
 * See mainwindow.h.
 */

#include "mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenuBar>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QtMath>

#include "fontface.h"
#include "fw_platform.h"
#include "prefsdialog.h"
#include "screentext.h"

/* One frame at the board's ~60 Hz, and the share of it the firmware may
 * spend interpreting before the paint. */
static const int FRAME_MS = 16;
static const int PUMP_BUDGET_MS = 10;

/* Pasted bytes sent each frame: about 1000 a second. USB CDC would deliver a
 * paste all at once, but the board's ACIA driver (sc6850) has a receive
 * buffer of under a page and no flow control on this link, so a large paste
 * arriving faster than the program reading it would overrun the buffer and
 * lose characters without a word. This is faster than anyone types and slow
 * enough for the shell and editors to keep up. */
static const int PASTE_PER_FRAME = 16;

/* A menu shortcut that leaves every Ctrl chord to the guest. main() keeps
 * Qt from swapping Control and Command on macOS, so there "Ctrl" in a key
 * sequence is the real Control key; menus go on Command (Qt's Meta) instead.
 * Elsewhere, Ctrl+Shift keeps them off the guest's control codes. */
static QKeySequence
shortcut(Qt::Key key)
{
#ifdef Q_OS_MACOS
	return QKeySequence(Qt::META | key);
#else
	return QKeySequence(Qt::CTRL | Qt::SHIFT | key);
#endif
}

MainWindow::MainWindow(const QString &port_name, QWidget *parent)
	: QMainWindow(parent)
{
	setWindowTitle("coco-thing");

	// The screen, with the scroll-back's scroll bar beside it, framed by
	// a tab widget's pane: its one tab names the port. The tab bar never
	// takes the keyboard, so Tab and the arrows stay with the board.
	QWidget *page = new QWidget;
	QHBoxLayout *row = new QHBoxLayout(page);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(0);
	screen = new Screen(page);
	scroll_bar = new QScrollBar(Qt::Vertical, page);
	scroll_bar->setObjectName("screenScrollBar");
	row->addWidget(screen, 1);
	row->addWidget(scroll_bar);
	tabs = new QTabWidget(this);
	tabs->addTab(page, QString());
	tabs->setFocusPolicy(Qt::NoFocus);
	tabs->tabBar()->setFocusPolicy(Qt::NoFocus);
	// The tab's title is drawn in the screen's own face, so it is a
	// picture in the tab rather than its text.
	tab_label = new QLabel;
	tab_label->setObjectName("tabTitle");
	// The traffic pane goes beside the screen, hidden until the Debug
	// menu shows it; the screen keeps any space the window gains.
	traffic = new TrafficPane;
	traffic->hide();
	QSplitter *split = new QSplitter(Qt::Horizontal, this);
	split->addWidget(tabs);
	split->addWidget(traffic);
	split->setStretchFactor(0, 1);
	split->setStretchFactor(1, 0);
	split->setChildrenCollapsible(false);
	setCentralWidget(split);
	screen->setFocus();

	screen->setTerminal(&terminal);
	screen->setKeyboard(&keyboard);
	connect(screen, &Screen::viewChanged, this, &MainWindow::syncScrollBar);
	// The bar's value counts down from the live screen at its maximum.
	connect(scroll_bar, &QScrollBar::valueChanged, this, [this](int v) {
		screen->scrollBy((scroll_bar->maximum() - v) -
		    screen->scrolledBack());
	});
	fw_platform_set_bell([] { QApplication::beep(); });
	connect(screen, &Screen::keysTyped, this, &MainWindow::sendKeys);
	clock.start();

	QMenu *conn = menuBar()->addMenu("&Connection");
	conn->addAction("&Connect…", shortcut(Qt::Key_K), this,
	    &MainWindow::choosePort);
	conn->addAction("&Disconnect", this, &MainWindow::disconnectPort);
	conn->addSeparator();
	conn->addAction("&Reset Screen", shortcut(Qt::Key_R), this,
	    &MainWindow::resetScreen);
	conn->addSeparator();
	// On macOS the role moves this into the application menu.
	QAction *prefs_action = conn->addAction("&Preferences…",
	    shortcut(Qt::Key_Comma), this, &MainWindow::editPrefs);
	prefs_action->setMenuRole(QAction::PreferencesRole);

	// The board's screen cannot be edited from here, so Cut copies.
	QMenu *edit = menuBar()->addMenu("&Edit");
	edit->addAction("Cu&t", shortcut(Qt::Key_X), this, &MainWindow::cut);
	edit->addAction("&Copy", shortcut(Qt::Key_C), this, &MainWindow::copy);
	edit->addAction("&Paste", shortcut(Qt::Key_V), this,
	    &MainWindow::paste);
	edit->addSeparator();
	edit->addAction("Select &All", shortcut(Qt::Key_A), this,
	    &MainWindow::selectAll);
	QMenu *view = menuBar()->addMenu("&View");
	view->addAction("Scroll Up a Page", shortcut(Qt::Key_PageUp), this,
	    [this] { screen->scrollBy(screen->pageRows()); });
	view->addAction("Scroll Down a Page", shortcut(Qt::Key_PageDown),
	    this, [this] { screen->scrollBy(-screen->pageRows()); });
	view->addAction("Scroll to Top", shortcut(Qt::Key_Home), this,
	    [this] { screen->scrollToTop(); });
	view->addAction("Scroll to Bottom", shortcut(Qt::Key_End), this,
	    [this] { screen->scrollToBottom(); });
	view->addSeparator();
	view->addAction("Fit Window to Text", shortcut(Qt::Key_0), this,
	    &MainWindow::fitWindow);
	view->addSeparator();
	view->addAction("Clear Scroll-back", this, [this] {
		screen->clearSelection();
		terminal.clearScrollback();
		screen->scrollToBottom();
	});

	QMenu *debug = menuBar()->addMenu("&Debug");
	QAction *show_traffic = debug->addAction("Show &Traffic");
	show_traffic->setCheckable(true);
	connect(show_traffic, &QAction::toggled, this,
	    &MainWindow::showTraffic);

	// Selecting copies, as the pasteboard's "primary selection".
	connect(screen, &Screen::selectionMade, this, &MainWindow::copy);

	state = new QLabel(this);
	state->setObjectName("linkState");
	statusBar()->addPermanentWidget(state, 1);

	connect(&link, &Link::received, &terminal, &Terminal::feed);
	// Kept whether or not the pane is showing, so opening it after
	// something odd shows what led up to it.
	connect(&link, &Link::received, this, [this](const QByteArray &b) {
		traffic->log().add(TrafficLog::FromBoard, b);
	});
	connect(&link, &Link::sent, this, [this](const QByteArray &b) {
		traffic->log().add(TrafficLog::ToBoard, b);
	});
	connect(&link, &Link::connected, this, &MainWindow::linkConnected);
	connect(&link, &Link::lost, this, &MainWindow::linkLost);
	connect(&link, &Link::stateChanged, this, &MainWindow::showState);
	connect(&timer, &QTimer::timeout, this, &MainWindow::tick);
	timer.start(FRAME_MS);

	QSettings settings;
	restoreGeometry(settings.value("window/geometry").toByteArray());

	prefs = Prefs::load();
	applyPrefs();
	// Once shown and laid out: a saved size from another text area or
	// scaling would leave spare space beside the text.
	QTimer::singleShot(0, this, &MainWindow::fitWindow);

	QString name = port_name;
	if (name.isEmpty() && prefs.connect_at_start)
		name = settings.value("connection/last_port").toString();
	if (!name.isEmpty())
		link.open(name);
	showState();
}

MainWindow::~MainWindow()
{
	// The members (the link, the keyboard, the terminal) are destroyed
	// before ~QWidget hides the child widgets, and hiding the screen takes
	// its focus, which releases the keys and signals sendKeys(). Cut the
	// screen loose first, so nothing reaches a member that has gone.
	screen->setKeyboard(nullptr);
	disconnect(screen, nullptr, this, nullptr);
}

/* The tab's title in the screen's face at its own size, 8x16 a character:
 * smaller than the screen, which scales it up. Each face pixel becomes
 * whole device pixels, so it stays sharp on a Retina display. Coloured as
 * the tab bar's text, on nothing. */
void
MainWindow::renderTab()
{
	const QColor fg = tabs->tabBar()->palette().color(QPalette::WindowText);
	const QImage img = fontface_sample(face, tab_text, fg.rgba(),
	    qRgba(0, 0, 0, 0));
	const int k = qMax(1, qCeil(devicePixelRatioF()));
	QPixmap pm = QPixmap::fromImage(img.scaled(img.size() * k,
	    Qt::IgnoreAspectRatio, Qt::FastTransformation));

	pm.setDevicePixelRatio(k);
	tab_label->setPixmap(pm);
	tab_label->setAccessibleName(tab_text);
	tab_label->adjustSize();
	// Set afresh so the bar measures the tab again: setting the same
	// widget a second time would hide it.
	tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
	tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, tab_label);
}

/* A light/dark switch or a move to a screen of another pixel density:
 * draw the tab's title again to match. */
void
MainWindow::changeEvent(QEvent *ev)
{
	QMainWindow::changeEvent(ev);
	if (ev->type() == QEvent::PaletteChange ||
	    ev->type() == QEvent::DevicePixelRatioChange)
		renderTab();
}

void
MainWindow::closeEvent(QCloseEvent *ev)
{
	QSettings().setValue("window/geometry", saveGeometry());
	QMainWindow::closeEvent(ev);
}

void
MainWindow::editPrefs()
{
	PrefsDialog dialog(prefs, this);

	if (dialog.exec() != QDialog::Accepted)
		return;
	prefs = dialog.prefs();
	prefs.save();
	applyPrefs();
}

void
MainWindow::applyPrefs()
{
	QString error;
	const QByteArray face = prefs.face(&error);

	// A font file that has gone away falls back to the built-in face
	// rather than leaving the screen blank.
	if (!error.isEmpty())
		statusBar()->showMessage(QString("Font: %1; using the built-in "
		    "face").arg(error));
	terminal.setFace(face);
	this->face = face;
	renderTab();

	// A new text area means a new window, which starts clear.
	bool reshaped = false;
	if (prefs.text_cols != terminal.textCols() ||
	    prefs.text_rows != terminal.textRows()) {
		terminal.setTextArea(prefs.text_cols, prefs.text_rows);
		terminal.reset();
		screen->setTextArea(terminal.textCols(), terminal.textRows());
		reshaped = true;
	}

	terminal.setScrollbackLimit(prefs.scrollback_lines);
	screen->scrollBy(0);		/* re-clamp to what is left */
	reshaped = reshaped || (prefs.scaling == Prefs::ScaleInteger) !=
	    screen_integer;
	screen_integer = prefs.scaling == Prefs::ScaleInteger;
	screen->setScaling(screen_integer, prefs.smooth);
	link.setAutoReconnect(prefs.auto_reconnect);
	screen->update();
	if (reshaped && isVisible())
		fitWindow();
}

/* Resize the window so the screen has no spare space beside or below the
 * text: keep its height and give it the width the text's shape wants. The
 * screen takes all of a size change (the bars and the frame are fixed), so
 * the window changes by the same amount. A maximised or full-screen window
 * is the system's to size. */
void
MainWindow::fitWindow()
{
	if (isMaximized() || isFullScreen())
		return;
	const QSize want = screen->fittedSize(screen->height());
	resize(size() + (want - screen->size()));
}

void
MainWindow::choosePort()
{
	QStringList available;
	int preferred = 0;

	for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
		available << info.portName();
	const QStringList names = Link::choices(available,
	    QSettings().value("connection/last_port").toString(), &preferred);

	// Editable, because a pty -- USim's console, for one -- is never
	// listed: its path, or a symlink to it, can be typed instead.
	bool ok = false;
	const QString name = Link::typedName(QInputDialog::getItem(this,
	    "Connect", "Serial port, or the path of a device or pty:", names,
	    preferred, true, &ok));
	if (!ok || name.isEmpty())
		return;
	link.open(name);
	if (link.state() == Link::Closed)
		statusBar()->showMessage(QString("Cannot open %1: %2")
		    .arg(name, link.lastError()));
}

void
MainWindow::disconnectPort()
{
	link.close();
}

void
MainWindow::resetScreen()
{
	screen->clearSelection();
	terminal.reset();
	screen->update();
}

void
MainWindow::linkConnected(bool again)
{
	QSettings().setValue("connection/last_port", link.portName());

	// The board came back from a reset. The video Pico clears its
	// console on the host's !RESET, so this one does too by default, and
	// anything still queued from before the reset is stale.
	if (again && prefs.clear_on_reconnect)
		resetScreen();
	statusBar()->clearMessage();
}

void
MainWindow::linkLost()
{
	// The screen is left as it was, so the last output stays readable.
	statusBar()->showMessage(link.state() == Link::Waiting
	    ? QString("%1 lost (%2); waiting for it")
	      .arg(link.portName(), link.lastError())
	    : QString("%1 lost (%2)").arg(link.portName(), link.lastError()));
}

void
MainWindow::sendKeys()
{
	// Read out even when nothing is connected, so keys typed then are
	// dropped rather than sent later. Typing returns the view to the
	// live screen, as in any terminal.
	const QByteArray bytes = terminal.takeInput();
	if (bytes.isEmpty())
		return;
	screen->scrollToBottom();
	link.write(bytes);
}

void
MainWindow::tick()
{
	keyboard.tick((uint32_t)clock.elapsed());
	sendKeys();
	if (!pasting.isEmpty()) {
		if (link.state() == Link::Open)
			link.write(pasting.left(PASTE_PER_FRAME));
		pasting.remove(0, PASTE_PER_FRAME);
	}
	terminal.pump(PUMP_BUDGET_MS);
	terminal.frame();
	syncScrollBar();
	screen->update();
	if (traffic->isVisible())
		traffic->refresh();
	showState();
}

/* Show or hide the traffic pane. The window widens or narrows by the pane's
 * width, so the screen keeps its size and its text stays as it was. */
void
MainWindow::showTraffic(bool on)
{
	if (on == traffic->isVisible())
		return;
	QSplitter *split = static_cast<QSplitter *>(centralWidget());
	const bool grow = !isMaximized() && !isFullScreen();
	// The pane and the splitter's handle beside it.
	const int extra = split->handleWidth() +
	    (on ? traffic->lineWidth() : traffic->width());
	const int keep = tabs->width();

	traffic->setVisible(on);
	if (on) {
		traffic->refresh();
		if (grow)
			resize(width() + extra, height());
		// The pane gets a whole line. If the window could not grow
		// that far (the display is too narrow), the screen gives up
		// the difference and scales down, rather than the pane
		// cutting its lines off.
		const int pane = extra - split->handleWidth();
		split->setSizes({ qMax(0, qMin(keep,
		    split->width() - split->handleWidth() - pane)), pane });
	} else if (grow) {
		resize(width() - extra, height());
	}
}

void
MainWindow::copy()
{
	if (screen->hasSelection())
		QApplication::clipboard()->setText(screen->selectedText());
}

void
MainWindow::cut()
{
	copy();
	screen->clearSelection();
}

void
MainWindow::paste()
{
	if (link.state() != Link::Open) {
		statusBar()->showMessage("Not connected: nothing pasted");
		return;
	}
	pasting += screentext_paste(QApplication::clipboard()->text());
	screen->scrollToBottom();
}

void
MainWindow::selectAll()
{
	screen->selectAll();
	copy();
}

/* The bar spans the history plus a page; at its maximum the view is live. */
void
MainWindow::syncScrollBar()
{
	const QSignalBlocker quiet(scroll_bar);
	const int held = terminal.historySize();

	scroll_bar->setRange(0, held);
	scroll_bar->setPageStep(screen->pageRows());
	scroll_bar->setValue(held - (int)screen->scrolledBack());
}

void
MainWindow::showState()
{
	QString s;

	switch (link.state()) {
	case Link::Closed:
		s = "Not connected";
		break;
	case Link::Waiting:
		// Why: usually that it is not there yet, but it may be in use
		// by another program, which holds its lock.
		s = QString("Waiting for %1").arg(link.portName());
		if (!link.lastError().isEmpty())
			s += ": " + link.lastError();
		break;
	case Link::Open:
		s = QString("Connected: %1").arg(link.portName());
		break;
	}
	if (terminal.backlog() > 0)
		s += QString("   backlog %1 B").arg(terminal.backlog());
	if (!pasting.isEmpty())
		s += QString("   pasting %1 B").arg(pasting.size());
	if (screen->scrolledBack() > 0)
		s += QString("   scrolled back %1 of %2 lines")
		    .arg(screen->scrolledBack()).arg(terminal.historySize());
	state->setText(s);

	// The tab names the port, by its device name (a full path is in the
	// tool tip), and says when it is not connected.
	const QString port = QFileInfo(link.portName()).fileName();
	QString tab;
	switch (link.state()) {
	case Link::Closed:
		tab = port.isEmpty() ? QString("Not connected") :
		    QString("%1 (closed)").arg(port);
		break;
	case Link::Waiting:
		tab = QString("%1 (waiting)").arg(port);
		break;
	case Link::Open:
		tab = port;
		break;
	}
	if (tab != tab_text) {
		tab_text = tab;
		tabs->setTabToolTip(0, link.portName().isEmpty() ? tab :
		    link.portName());
		renderTab();
	}
}
