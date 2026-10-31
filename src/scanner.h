#pragma once

#include <QObject>
#include <QProcess>
#include <QRect>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>

class FilePicker;

// Scans from the flatbed through SANE's scanimage and turns the scans into a
// PDF, either to save or to hand to the printer side as a copy. Two layouts:
// "document" keeps each scan as a full A4 page, and "card" finds the card on
// the glass, cuts it out, and puts the two sides of each card on one page at
// their real size, the way an ID card is photocopied.
class Scanner : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString device READ device NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY deviceChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString step READ step NOTIFY stateChanged)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY pagesChanged)
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

public:
    explicit Scanner(FilePicker *picker = nullptr, QObject *parent = nullptr);
    ~Scanner() override;

    QString device() const { return m_device; }
    QString deviceName() const { return m_deviceName; }
    bool searching() const { return m_searching; }
    bool busy() const { return !m_step.isEmpty(); }
    double progress() const { return m_progress; }
    QString step() const { return m_step; }
    QVariantList pages() const;
    QVariantMap options() const { return m_options; }
    QString problem() const { return m_problem; }

    Q_INVOKABLE void findDevice();
    Q_INVOKABLE void scan();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void setOption(const QString &key, const QVariant &value);
    Q_INVOKABLE void rotatePage(int index);
    Q_INVOKABLE void removePage(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void save();
    Q_INVOKABLE void print();

    // Takes an image as if it had just come off the glass, at the given
    // resolution. Scans go through here, and so do the tests.
    void addScan(const QString &image, int resolution);
    // Writes every page into one PDF; done gets an error, empty on success.
    void writePdf(const QString &target, std::function<void(const QString &error)> done);

signals:
    void deviceChanged();
    void stateChanged();
    void pagesChanged();
    void optionsChanged();
    void problemChanged();
    void toast(const QString &message);
    void readyToPrint(const QString &pdf);

private:
    using Done = std::function<void(int code, const QString &output, const QString &error)>;
    void run(const QString &program, const QStringList &arguments, Done done);
    void setProblem(const QString &problem);
    void setStep(const QString &step, double progress = 0);
    void saveTo(const QString &path);
    void thumbnail(int id);
    int indexOf(int id) const;

    struct Page {
        int id = 0;
        QString file;
        QString thumb;
        int resolution = 300;
        int version = 0;
    };

    FilePicker *m_picker;
    std::unique_ptr<FilePicker> m_ownedPicker;
    QSettings m_settings;
    QTemporaryDir m_work;

    QString m_device;
    QString m_deviceName;
    bool m_searching = false;
    bool m_scanAfterSearch = false;
    QString m_step;
    double m_progress = 0;
    QString m_problem;
    QVariantMap m_options;
    QList<Page> m_pages;
    int m_counter = 0;
    QProcess *m_scan = nullptr;
};
