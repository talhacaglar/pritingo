#include "planner.h"

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

} // namespace Planner
