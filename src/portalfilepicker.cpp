#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QRandomGenerator>
#include <QUrl>

// The portal's filter type: a(sa(us)), a name and its (kind, pattern) pairs,
// where kind 0 is a glob.
struct PortalFilterRule {
    uint kind = 0;
    QString pattern;
};
struct PortalFilter {
    QString name;
    QList<PortalFilterRule> rules;
};
Q_DECLARE_METATYPE(PortalFilterRule)
Q_DECLARE_METATYPE(PortalFilter)
Q_DECLARE_METATYPE(QList<PortalFilter>)

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule) {
    argument.beginStructure();
    argument << rule.kind << rule.pattern;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule) {
    argument.beginStructure();
    argument >> rule.kind >> rule.pattern;
    argument.endStructure();
    return argument;
}
QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilter &filter) {
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilter &filter) {
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

void PortalFilePicker::open(const QString &title, const QStringList &patterns) {
    PortalFilter documents;
    documents.name = tr("Printable files");
    for (const auto &pattern : patterns)
        documents.rules << PortalFilterRule{0, pattern};
    PortalFilter everything{tr("All files"), {PortalFilterRule{0, "*"}}};

    QVariantMap options;
    options["filters"] = QVariant::fromValue(QList<PortalFilter>{documents, everything});
    m_saving = false;
    request("OpenFile", title, options);
}

void PortalFilePicker::save(const QString &title, const QString &suggestedName) {
    QVariantMap options;
    options["current_name"] = suggestedName;
    options["filters"] = QVariant::fromValue(QList<PortalFilter>{{tr("PDF documents"), {PortalFilterRule{0, "*.pdf"}}}});
    m_saving = true;
    request("SaveFile", title, options);
}

void PortalFilePicker::request(const QString &method, const QString &title, QVariantMap options) {
    qDBusRegisterMetaType<PortalFilterRule>();
    qDBusRegisterMetaType<QList<PortalFilterRule>>();
    qDBusRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<QList<PortalFilter>>();

    auto bus = QDBusConnection::sessionBus();
    // Subscribing before the call avoids missing a fast answer; the request
    // path is predictable from our unique name and the handle token.
    const QString token = QStringLiteral("pritingo%1").arg(QRandomGenerator::global()->generate());
    QString sender = bus.baseService().mid(1).replace('.', '_');
    m_request = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect("org.freedesktop.portal.Desktop", m_request, "org.freedesktop.portal.Request", "Response",
                this, SLOT(response(uint, QVariantMap)));

    options["handle_token"] = token;
    options["modal"] = true;

    auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                               "org.freedesktop.portal.FileChooser", method);
    call << QString() << title << options;
    const QDBusReply<QDBusObjectPath> reply = bus.call(call);
    if (!reply.isValid()) {
        bus.disconnect("org.freedesktop.portal.Desktop", m_request, "org.freedesktop.portal.Request", "Response",
                       this, SLOT(response(uint, QVariantMap)));
        emit failed(tr("The file chooser is not available: %1").arg(reply.error().message()));
        return;
    }
    // Older portals pick their own path; follow it if it differs.
    if (reply.value().path() != m_request) {
        bus.disconnect("org.freedesktop.portal.Desktop", m_request, "org.freedesktop.portal.Request", "Response",
                       this, SLOT(response(uint, QVariantMap)));
        m_request = reply.value().path();
        bus.connect("org.freedesktop.portal.Desktop", m_request, "org.freedesktop.portal.Request", "Response",
                    this, SLOT(response(uint, QVariantMap)));
    }
}

void PortalFilePicker::response(uint code, const QVariantMap &results) {
    QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_request,
                                             "org.freedesktop.portal.Request", "Response", this,
                                             SLOT(response(uint, QVariantMap)));
    if (code != 0)
        return; // cancelled
    const QStringList uris = results.value("uris").toStringList();
    if (uris.isEmpty())
        return;
    const QString path = QUrl(uris.first()).toLocalFile();
    if (m_saving)
        emit savePicked(path);
    else
        emit picked(path);
}
