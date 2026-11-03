#include "planner.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace Planner {

QList<int> parseRange(const QString &text, int pageCount, bool *ok) {
    if (ok)
        *ok = true;
    QList<int> pages;
    QSet<int> seen;
    auto add = [&](int page) {
        if (!seen.contains(page)) {
            seen.insert(page);
            pages << page;
        }
    };

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        for (int i = 1; i <= pageCount; ++i)
            add(i);
        return pages;
    }

    static const QRegularExpression part(R"(^\s*(\d*)\s*(-?)\s*(\d*)\s*$)");
    for (const QString &chunk : trimmed.split(QRegularExpression("[,;]"), Qt::SkipEmptyParts)) {
        const auto match = part.match(chunk);
        const bool dash = !match.captured(2).isEmpty();
        if (!match.hasMatch() || (match.captured(1).isEmpty() && match.captured(3).isEmpty())) {
            if (ok)
                *ok = false;
            continue;
        }
        const int first = match.captured(1).isEmpty() ? 1 : match.captured(1).toInt();
        const int last = !dash ? first : match.captured(3).isEmpty() ? pageCount : match.captured(3).toInt();
        if (first < 1 || last < 1 || first > pageCount || last > pageCount) {
            if (ok)
                *ok = false;
            continue;
        }
        // A range written backwards ("5-2") prints backwards, as written.
        const int step = first <= last ? 1 : -1;
        for (int page = first;; page += step) {
            add(page);
            if (page == last)
                break;
        }
    }
    return pages;
}

QString formatRange(QList<int> pages) {
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    QStringList parts;
    for (int i = 0; i < pages.size();) {
        int j = i;
        while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1)
            ++j;
        parts << (j == i ? QString::number(pages[i])
                         : QStringLiteral("%1-%2").arg(pages[i]).arg(pages[j]));
        i = j + 1;
    }
    return parts.join(',');
}

QList<Sheet> sheets(const QList<int> &pages, bool duplex, int copies) {
    QList<Sheet> result;
    for (int copy = 0; copy < std::max(1, copies); ++copy) {
        for (int i = 0; i < pages.size(); i += duplex ? 2 : 1) {
            Sheet sheet;
            sheet.front = pages[i];
            if (duplex && i + 1 < pages.size())
                sheet.back = pages[i + 1];
            result << sheet;
        }
    }
    return result;
}

QList<int> fronts(const QList<Sheet> &sheets) {
    QList<int> pages;
    for (const auto &sheet : sheets)
        pages << sheet.front;
    return pages;
}

QList<int> backs(const QList<Sheet> &sheets, bool reversed) {
    QList<int> pages;
    for (const auto &sheet : sheets)
        if (sheet.back)
            pages << sheet.back;
    if (reversed)
        std::reverse(pages.begin(), pages.end());
    return pages;
}

int sheetsWithBacks(const QList<Sheet> &sheets) {
    return std::count_if(sheets.begin(), sheets.end(), [](const Sheet &s) { return s.back != 0; });
}

QString driverOptions(const QVariantMap &s) {
    QStringList options;
    options << QStringLiteral("CNGrayscale=%1").arg(s.value("color").toBool() ? "False" : "True");
    options << QStringLiteral("PageSize=%1").arg(s.value("paperSize", "A4").toString());
    options << QStringLiteral("MediaType=%1").arg(s.value("media", "plain").toString());
    options << QStringLiteral("CNQuality=%1").arg(s.value("quality", 3).toInt());
    options << QStringLiteral("CNGamma=%1").arg(s.value("brightness", "1.8").toString());
    options << QStringLiteral("CNDensity=%1").arg(s.value("density", 0).toInt());
    options << QStringLiteral("CNContrast=%1").arg(s.value("contrast", 0).toInt());
    options << QStringLiteral("CNBalanceC=%1").arg(s.value("cyan", 0).toInt());
    options << QStringLiteral("CNBalanceM=%1").arg(s.value("magenta", 0).toInt());
    options << QStringLiteral("CNBalanceY=%1").arg(s.value("yellow", 0).toInt());
    options << QStringLiteral("CNInkCartridgeSettings=%1").arg(s.value("cartridges", "bkcolor").toString());
    return options.join(' ');
}

int rotation(double width, double height, int pageRotation, bool autoRotate, bool flipBack) {
    // The page's /Rotate swaps how it shows, so compare the shown sides.
    const bool turned = ((pageRotation % 180) + 180) % 180 == 90;
    const bool landscape = turned ? height > width : width > height;
    int degrees = autoRotate && landscape ? 90 : 0;
    if (flipBack)
        degrees += 180;
    return degrees % 360;
}

Paper paper(const QString &name) {
    // left, bottom, right and top as the PPD writes them, from the bottom left.
    struct Entry {
        double width, height, left, bottom, right, top;
    };
    static const QHash<QString, Entry> entries{
        {"A4", {595, 842, 9.64, 47.34, 585.64, 833.39}},
        {"A5", {420, 595, 9.64, 47.34, 409.89, 586.77}},
        {"B5", {516, 729, 9.64, 47.34, 506.27, 720.00}},
        {"Letter", {612, 792, 18.14, 47.34, 594.14, 783.50}},
        {"Legal", {612, 1008, 18.14, 47.34, 594.14, 999.50}},
        {"4X6", {288, 432, 9.64, 47.34, 278.36, 423.50}},
        {"5X7", {360, 504, 9.64, 47.34, 350.36, 495.50}},
        {"envelop10p", {297, 684, 15.87, 92.13, 281.11, 661.32}},
        {"envelopdlp", {312, 624, 15.87, 92.13, 295.94, 600.94}},
    };
    const Entry e = entries.value(name, entries.value("A4"));
    return {QSizeF(e.width, e.height), QRectF(e.left, e.height - e.top, e.right - e.left, e.top - e.bottom)};
}

QRectF inkBox(const QImage &render) {
    if (render.isNull())
        return QRectF(0, 0, 1, 1);
    // Anything lighter is paper, or the grain of a scanned one. The thinnest
    // line still leaves a pixel darker than this at thumbnail size.
    constexpr int PaperLevel = 245;
    const QImage image = render.convertToFormat(QImage::Format_RGB32);
    int left = image.width(), right = -1, top = image.height(), bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (std::min({qRed(line[x]), qGreen(line[x]), qBlue(line[x])}) >= PaperLevel)
                continue;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    if (right < 0)
        return {};
    // A pixel of room each way: at this size a pixel is nearly two points, and
    // the mark may reach anywhere inside the last one.
    const QRectF pixels(left - 1, top - 1, right - left + 3, bottom - top + 3);
    return QRectF(pixels.x() / image.width(), pixels.y() / image.height(), pixels.width() / image.width(),
                  pixels.height() / image.height())
        .intersected(QRectF(0, 0, 1, 1));
}

QRectF turned(const QRectF &box, int degrees) {
    if (box.isEmpty())
        return box;
    switch (((degrees % 360) + 360) % 360) {
    case 90:
        return QRectF(1 - box.bottom(), box.x(), box.height(), box.width());
    case 180:
        return QRectF(1 - box.right(), 1 - box.bottom(), box.width(), box.height());
    case 270:
        return QRectF(box.y(), 1 - box.right(), box.height(), box.width());
    default:
        return box;
    }
}

Placement place(const QSizeF &page, const QRectF &content, const Paper &paper, const QString &mode, int percent) {
    const QRectF area = paper.printable;
    const bool blank = content.isEmpty();
    const QRectF fractions = blank ? QRectF(0, 0, 1, 1) : content;
    // The marks at the page's real size, from its own top left.
    const QRectF ink(fractions.x() * page.width(), fractions.y() * page.height(), fractions.width() * page.width(),
                     fractions.height() * page.height());
    auto onSheet = [&ink](const QPointF &origin, double scale) {
        return QRectF(origin + ink.topLeft() * scale, ink.size() * scale);
    };
    auto centered = [&page, &paper](double scale) {
        return QPointF((paper.size.width() - page.width() * scale) / 2,
                       (paper.size.height() - page.height() * scale) / 2);
    };

    double scale = 1;
    QPointF origin;
    if (mode == "actual" || mode == "custom") {
        scale = mode == "custom" ? std::clamp(percent, 25, 200) / 100.0 : 1;
        origin = centered(scale);
    } else if (mode == "fill") {
        scale = std::min(area.width() / ink.width(), area.height() / ink.height());
        origin = area.center() - (ink.center() * scale);
    } else {
        scale = std::min(paper.size.width() / page.width(), paper.size.height() / page.height());
        if (!blank) {
            const QSizeF marks = ink.size() * scale;
            scale *= std::min({1.0, area.width() / marks.width(), area.height() / marks.height()});
        }
        origin = centered(scale);
        // Shrinking leaves the marks in the page's middle, while the printer's
        // reach is lopsided, so they move only as far as they have to.
        if (!blank) {
            const QRectF marks = onSheet(origin, scale);
            if (marks.left() < area.left())
                origin.rx() += area.left() - marks.left();
            else if (marks.right() > area.right())
                origin.rx() -= marks.right() - area.right();
            if (marks.top() < area.top())
                origin.ry() += area.top() - marks.top();
            else if (marks.bottom() > area.bottom())
                origin.ry() -= marks.bottom() - area.bottom();
        }
    }

    Placement placement;
    placement.scale = scale;
    placement.page = QRectF(origin, page * scale);
    // Half a point of slack, so rounding never warns about a page that fits.
    placement.cut = !blank && !area.adjusted(-0.5, -0.5, 0.5, 0.5).contains(onSheet(origin, scale));
    return placement;
}

QStringList placeArguments(const Placement &placement, const QSizeF &paper, const QString &input, const QString &output) {
    // PostScript counts from the bottom left. Every page of a PDF starts with
    // BeginPage, whose argument (the page count) it has to take off the stack.
    const QString move = QStringLiteral("<</BeginPage{pop %1 %2 translate %3 %3 scale}>> setpagedevice")
                             .arg(placement.page.x(), 0, 'f', 3)
                             .arg(paper.height() - placement.page.bottom(), 0, 'f', 3)
                             .arg(placement.scale, 0, 'f', 5);
    return {"-q", "-dBATCH", "-dNOPAUSE", "-dSAFER", "-sDEVICE=pdfwrite",
            // pdfwrite would otherwise turn a page by the way its text runs,
            // and recompress pictures, which shows on a photo.
            "-dAutoRotatePages=/None", "-dAutoFilterColorImages=false", "-dColorImageFilter=/FlateEncode",
            "-dAutoFilterGrayImages=false", "-dGrayImageFilter=/FlateEncode",
            // pdfinfo's sizes, which everything here goes by, are the crop box.
            "-dUseCropBox", "-dFIXEDMEDIA",
            QStringLiteral("-dDEVICEWIDTHPOINTS=%1").arg(int(paper.width())),
            QStringLiteral("-dDEVICEHEIGHTPOINTS=%1").arg(int(paper.height())),
            "-sOutputFile=" + output, "-c", move, "-f", input};
}

} // namespace Planner
