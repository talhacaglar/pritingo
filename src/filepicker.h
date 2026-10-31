#pragma once

#include <QObject>
#include <QStringList>

// Opening and saving files goes through an interface so the tests can hand
// the backend a picker that answers without a desktop portal.
class FilePicker : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual void open(const QString &title, const QStringList &patterns) = 0;
    virtual void save(const QString &title, const QString &suggestedName) = 0;

signals:
    void picked(const QString &path);
    void savePicked(const QString &path);
    void failed(const QString &message);
};
