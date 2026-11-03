#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// The pure part of printing: which pages go on which sheet, in what order each
// pass sends them, and the option string the Canon driver reads. Kept apart
// from the processes so the tests can pin down the order without a printer.
namespace Planner {

struct Sheet {
    int front = 0;
    int back = 0; // 0 when the back stays blank
};

// "1-3, 5, 8-" style ranges, 1-based. An empty string means every page.
// Returns the pages in the order written, without duplicates; sets ok to
// false when a part does not parse or points past the last page.
QList<int> parseRange(const QString &text, int pageCount, bool *ok = nullptr);

// The shortest range string for a set of pages ("1-3,5"), sorted.
QString formatRange(QList<int> pages);

// Sheets for the chosen pages, repeated per copy. One-sided puts one page on
// each sheet; two-sided pairs them, so an odd count leaves the last back blank.
// Each copy starts on a fresh sheet so copies can be handed out separately.
QList<Sheet> sheets(const QList<int> &pages, bool duplex, int copies);

// The pages the first pass prints, in feed order.
QList<int> fronts(const QList<Sheet> &sheets);

// The pages the second pass prints onto the backs. reversed sends them last
// sheet first, for printers that stack the first pass the other way round.
QList<int> backs(const QList<Sheet> &sheets, bool reversed);

// The sheets that go back into the tray for the second pass.
int sheetsWithBacks(const QList<Sheet> &sheets);

// The pstocanonij option string ("CNGrayscale=True PageSize=A4 ...") for a
// settings map with the keys the backend keeps.
QString driverOptions(const QVariantMap &settings);

// Degrees to turn a page so it lands upright on portrait paper: a landscape
// page turns 90 when autoRotate is on, and a back turns another 180 when the
// user flips the stack end over end.
int rotation(double width, double height, int pageRotation, bool autoRotate, bool flipBack);

// A sheet in points, portrait, with the part the printer can reach. Rectangles
// here count from the sheet's top left, the way the window draws it.
struct Paper {
    QSizeF size;
    QRectF printable;
};

// The driver's page sizes and printable areas (its PPD's ImageableArea).
Paper paper(const QString &name);

// Where a page lands: its whole box on the sheet, which may reach past the
// sheet's edges, the scale from its own size, and whether anything on it falls
// outside what the printer can reach.
struct Placement {
    double scale = 1;
    QRectF page;
    bool cut = false;
};

// Where the marks are on a rendered page, as fractions of its size: empty for
// a blank page, the whole page when the render is missing.
QRectF inkBox(const QImage &render);

// A box of fractions as it lies once its page has turned clockwise.
QRectF turned(const QRectF &box, int degrees);

// Lays a page, already turned the way it prints, on a sheet. content is
// inkBox's answer for it. The modes:
//   "fit"    fills the sheet like any page, then shrinks only if that would
//            lose marks, so a page with normal margins stays at its real size
//   "actual" its real size, centered, whatever that cuts
//   "fill"   the marks as large as the printable area allows, margins dropped
//   "custom" percent of its real size, centered, whatever that cuts
Placement place(const QSizeF &page, const QRectF &content, const Paper &paper, const QString &mode, int percent);

// Ghostscript's arguments for rewriting a one-page PDF onto a sheet of its own
// with the page where the placement says.
QStringList placeArguments(const Placement &placement, const QSizeF &paper, const QString &input, const QString &output);

} // namespace Planner
