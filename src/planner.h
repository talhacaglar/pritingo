#pragma once

#include <QList>
#include <QString>
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

} // namespace Planner
