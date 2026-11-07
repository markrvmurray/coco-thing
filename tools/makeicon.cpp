/*
 * makeicon.cpp
 *
 * Draws coco-thing's icon, the Pico-Thing's "?!" logo: the two characters in
 * yellow Comic Sans on a green disc that fades to transparent towards every
 * edge. Writes every size a macOS .iconset holds, each drawn at that size
 * rather than scaled, so the small ones stay sharp; iconutil then packs them
 * into the .icns the bundle carries (the `icon` target in CMakeLists.txt).
 *
 *	makeicon <font.ttf> <out.iconset>
 */

#include <cstdio>

#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

static const QColor GREEN(0x1E, 0x8C, 0x3A);
static const QColor YELLOW(0xFF, 0xD6, 0x0A);
static const char TEXT[] = "?!";

static QImage
draw(int size, const QString &family)
{
	QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);

	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);

	// The disc: solid green across the middle, fading to nothing at the
	// edges, the same way in every direction.
	const QPointF centre(size / 2.0, size / 2.0);
	QRadialGradient fade(centre, size / 2.0);
	QColor clear = GREEN;
	clear.setAlpha(0);
	fade.setColorAt(0.0, GREEN);
	fade.setColorAt(0.55, GREEN);
	fade.setColorAt(1.0, clear);
	p.fillRect(img.rect(), fade);

	// The logo, as an outline so it can be centred on its ink rather
	// than on the font's line box, and sized to fill the solid middle.
	QFont font(family);
	font.setPixelSize(256);
	QPainterPath path;
	path.addText(0, 0, font, TEXT);
	const QRectF ink = path.boundingRect();
	const qreal scale = qMin(0.62 * size / ink.height(),
	    0.70 * size / ink.width());
	p.translate(centre);
	p.scale(scale, scale);
	p.translate(-ink.center());
	p.fillPath(path, YELLOW);
	p.end();
	return img;
}

int
main(int argc, char *argv[])
{
	QGuiApplication app(argc, argv);

	if (argc != 3) {
		fprintf(stderr, "usage: makeicon <font.ttf> <out.iconset>\n");
		return 2;
	}
	const int id = QFontDatabase::addApplicationFont(argv[1]);
	if (id < 0) {
		fprintf(stderr, "makeicon: cannot load %s\n", argv[1]);
		return 1;
	}
	const QString family = QFontDatabase::applicationFontFamilies(id)
	    .value(0);

	QDir out(argv[2]);
	if (!out.mkpath(".")) {
		fprintf(stderr, "makeicon: cannot make %s\n", argv[2]);
		return 1;
	}
	// The sizes and names iconutil expects.
	for (int base : { 16, 32, 128, 256, 512 }) {
		for (int scale : { 1, 2 }) {
			const QString name = scale == 1
			    ? QString("icon_%1x%1.png").arg(base)
			    : QString("icon_%1x%1@2x.png").arg(base);
			if (!draw(base * scale, family).save(out.filePath(name))) {
				fprintf(stderr, "makeicon: cannot write %s\n",
				    qPrintable(name));
				return 1;
			}
		}
	}
	return 0;
}
