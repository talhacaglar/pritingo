#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSizeF>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>

#include "planner.h"

class FilePicker;

// The app's one job: take a document, plan its sheets, and feed the printer
// one page per job. The Canon E410 behind CUPS's usb backend prints only the
// first page of a multi-page job and drops the rest without an error, while
// single-page jobs with a pause between them always come out. So each page
// goes out as its own raw job, and the next waits until the job has left the
// queue and the printer has settled.
class Backend : public QObject {
    Q_OBJECT

    // Printer
    Q_PROPERTY(QStringList printers READ printers NOTIFY printerChanged)
    Q_PROPERTY(QString printer READ printer WRITE setPrinter NOTIFY printerChanged)
    Q_PROPERTY(bool printerConnected READ printerConnected NOTIFY printerChanged)
    Q_PROPERTY(int printerQueue READ printerQueue NOTIFY printerChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

    // Document
    Q_PROPERTY(QString fileName READ fileName NOTIFY documentChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY documentChanged)
    Q_PROPERTY(QStringList thumbnails READ thumbnails NOTIFY documentChanged)
    Q_PROPERTY(QStringList grayThumbnails READ grayThumbnails NOTIFY documentChanged)
    Q_PROPERTY(QVariantList landscapePages READ landscapePages NOTIFY documentChanged)

    // Choices
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QVariantList selectedPages READ selectedPages NOTIFY selectionChanged)
    Q_PROPERTY(QString rangeText READ rangeText NOTIFY selectionChanged)
    Q_PROPERTY(bool rangeValid READ rangeValid NOTIFY selectionChanged)

    // Plan
    Q_PROPERTY(QVariantList sheetModel READ sheetModel NOTIFY planChanged)
    Q_PROPERTY(int sheetCount READ sheetCount NOTIFY planChanged)
    Q_PROPERTY(int backSheetCount READ backSheetCount NOTIFY planChanged)
    Q_PROPERTY(int pagesToPrint READ pagesToPrint NOTIFY planChanged)
    Q_PROPERTY(int estimateMinutes READ estimateMinutes NOTIFY planChanged)

    // Job
    Q_PROPERTY(QString state READ state NOTIFY jobChanged)
    Q_PROPERTY(int pass READ pass NOTIFY jobChanged)
    Q_PROPERTY(QString step READ step NOTIFY jobChanged)
    Q_PROPERTY(QVariantList jobItems READ jobItems NOTIFY jobChanged)
    Q_PROPERTY(int currentItem READ currentItem NOTIFY jobChanged)
    Q_PROPERTY(double progress READ progress NOTIFY jobChanged)
    Q_PROPERTY(bool stalled READ stalled NOTIFY jobChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY jobChanged)
    Q_PROPERTY(int lastBlankPage READ lastBlankPage NOTIFY jobChanged)

public:
    explicit Backend(FilePicker *picker = nullptr, QObject *parent = nullptr);
    ~Backend() override;

    QStringList printers() const { return m_printers; }
    QString printer() const { return m_printer; }
    void setPrinter(const QString &printer);
    bool printerConnected() const { return m_printerConnected; }
    int printerQueue() const { return m_printerQueue; }
    QString problem() const { return m_problem; }

    QString fileName() const { return m_fileName; }
    int pageCount() const { return m_pageSizes.size(); }
    bool loading() const { return m_loading; }
    QStringList thumbnails() const { return m_thumbnails; }
    QStringList grayThumbnails() const { return m_grayThumbnails; }
    QVariantList landscapePages() const;

    QVariantMap options() const { return m_options; }
    QVariantList selectedPages() const;
    QString rangeText() const { return m_rangeText; }
    bool rangeValid() const { return m_rangeValid; }

    QVariantList sheetModel() const;
    int sheetCount() const { return m_sheets.size(); }
    int backSheetCount() const { return Planner::sheetsWithBacks(m_sheets); }
    int pagesToPrint() const;
    int estimateMinutes() const;

    QString state() const { return m_state; }
    int pass() const { return m_pass; }
    QString step() const { return m_step; }
    QVariantList jobItems() const { return m_items; }
    int currentItem() const { return m_current; }
    double progress() const;
    bool stalled() const { return m_stalled; }
    int elapsedSeconds() const;
    int lastBlankPage() const;

    Q_INVOKABLE void pickFile();
    Q_INVOKABLE void openFile(const QString &pathOrUrl);
    Q_INVOKABLE void closeFile();
    Q_INVOKABLE void setOption(const QString &key, const QVariant &value);
    Q_INVOKABLE void resetAdvanced();
    Q_INVOKABLE void setRange(const QString &text);
    Q_INVOKABLE void togglePage(int page);
    Q_INVOKABLE void selectPages(const QString &which); // "all", "odd", "even", "none"
    Q_INVOKABLE void requestPreview(int page);

    Q_INVOKABLE void print();
    Q_INVOKABLE void continueAfterFlip();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void maintenance(const QString &kind); // "clean", "nozzle", "align"
    Q_INVOKABLE void testDuplexOrientation();

    static QString driverPpd();

signals:
    void printerChanged();
    void problemChanged();
    void documentChanged();
    void optionsChanged();
    void selectionChanged();
    void planChanged();
    void jobChanged();
    void previewReady(int page, const QString &url);
    void toast(const QString &message);

private:
    using Done = std::function<void(int code, const QString &error)>;
    void run(const QString &program, const QStringList &arguments, Done done,
             const QString &stdoutFile = {}, const QProcessEnvironment &environment = {});

    void setProblem(const QString &problem);
    void setStep(const QString &step);
    void loadPdf();
    void refreshPrinters();
    void pollPrinter();
    void replan();
    void setSelection(const QList<int> &pages);

    void startPass(int pass);
    void sendItem();
    void waitForJob();
    void itemFinished();
    void finishPass();
    void fail(const QString &message);
    void notify(const QString &title, const QString &body, const QString &sound);
    QSizeF paperPoints() const;

    FilePicker *m_picker;
    std::unique_ptr<FilePicker> m_ownedPicker;
    QSettings m_settings;

    QStringList m_printers;
    QString m_printer;
    QString m_deviceUri;
    bool m_printerConnected = false;
    int m_printerQueue = 0;
    QString m_problem;
    QTimer m_printerPoll;
    QProcess *m_queueProbe = nullptr;

    std::unique_ptr<QTemporaryDir> m_work;
    QString m_fileName;
    bool m_loading = false;
    QList<QSizeF> m_pageSizes;
    QList<int> m_pageRotations;
    QStringList m_thumbnails;
    QStringList m_grayThumbnails;
    int m_loadGeneration = 0;

    QVariantMap m_options;
    QList<int> m_selection;
    QString m_rangeText;
    bool m_rangeValid = true;
    QList<Planner::Sheet> m_sheets;

    QString m_state = "idle";
    int m_pass = 0;
    QString m_step;
    QVariantList m_items;
    QList<int> m_queue;
    int m_queueIndex = 0;
    int m_current = -1;
    int m_firstBackItem = 0;
    QString m_jobId;
    bool m_stalled = false;
    QElapsedTimer m_jobClock;
    QElapsedTimer m_itemClock;
    QElapsedTimer m_usbClock;
    bool m_waitingForUsb = false;
    QTimer m_jobPoll;
    QTimer m_tick;
    QList<QProcess *> m_running;
    int m_jobGeneration = 0;
};
