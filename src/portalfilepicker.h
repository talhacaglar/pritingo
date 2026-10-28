#pragma once

#include <QVariantMap>

#include "filepicker.h"

// Asks xdg-desktop-portal for a file, so the dialog is the desktop's own and
// sandboxes would work. The answer arrives as a Response signal on the
// request object the call returns.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    using FilePicker::FilePicker;
    void open(const QString &title, const QStringList &patterns) override;

private slots:
    void response(uint code, const QVariantMap &results);

private:
    QString m_request;
};
