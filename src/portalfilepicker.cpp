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
    qDBusRegisterMetaType<PortalFilterRule>();
    qDBusRegisterMetaType<QList<PortalFilterRule>>();
    qDBusRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<QList<PortalFilter>>();

    auto bus = QDBusConnection::sessionBus();
    // Subscribing before the call avoids missing a fast answer; the request
    // path is predictable from our unique name and the handle token.
    const QString token = QStringLiteral("baski%1").arg(QRandomGenerator::global()->generate());
    QString sender = bus.baseService().mid(1).replace('.', '_');
    m_request = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect("org.freedesktop.portal.Desktop", m_request, "org.freedesktop.portal.Request", "Response",
                this, SLOT(response(uint, QVariantMap)));

    PortalFilter documents;
    documents.name = tr("Printable files");
    for (const auto &pattern : patterns)
        documents.rules << PortalFilterRule{0, pattern};
    PortalFilter everything{tr("All files"), {PortalFilterRule{0, "*"}}};

    QVariantMap options;
    options["handle_token"] = token;
    options["modal"] = true;
    options["filters"] = QVariant::fromValue(QList<PortalFilter>{documents, everything});

    auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                               "org.freedesktop.portal.FileChooser", "OpenFile");
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
    if (!uris.isEmpty())
        emit picked(QUrl(uris.first()).toLocalFile());
}
