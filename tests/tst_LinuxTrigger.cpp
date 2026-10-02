#include "platform/linux/LinuxBackend.h"
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QDBusArgument>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString root = QStringLiteral("/org/freedesktop/portal/desktop");
const QString global = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
QJsonObject trigger(const QString &key = QStringLiteral("Volume Mute")) {
    return {{"shortcut", key}, {"overrideConflict", true}};
}
}

// Runs on an isolated dbus-run-session bus, never the user's desktop portal.
class PortalFixture final : public QDBusVirtualObject {
public:
    QSet<QString> registered;
    QStringList calls, created, closed;
    QHash<QString,QString> owners;
    QString registrationError;
    bool rejectBind = false;
    int registrations = 0;
    int binds = 0;
    int delay = 15;
    QDBusConnection bus = QDBusConnection::sessionBus();
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &m, const QDBusConnection &connection) override {
        calls << m.member();
        if (m.member() == "Register") {
            ++registrations;
            if (!registrationError.isEmpty()) {
                connection.send(m.createErrorReply("org.freedesktop.portal.Error.Failed",registrationError));
            } else if (registered.contains(m.service())) {
                connection.send(m.createErrorReply("org.freedesktop.portal.Error.Failed","Connection already associated with an application ID"));
            } else {
                registered.insert(m.service());
                connection.send(m.createReply());
            }
            return true;
        }
        if (m.member() == "Close") {
            closed << m.path();
            connection.send(m.createReply());
            return true;
        }
        if (!registered.contains(m.service())) {
            connection.send(m.createErrorReply("org.freedesktop.portal.Error.InvalidArgument","An app id is required"));
            return true;
        }
        const bool create = m.member() == "CreateSession";
        if (!create && m.member() != "BindShortcuts") return false;
        if (!create) {
            ++binds;
            if (rejectBind) {
                connection.send(m.createErrorReply("org.freedesktop.portal.Error.Failed","Binding failed"));
                return true;
            }
        }
        const auto options = qdbus_cast<QVariantMap>(m.arguments().last());
        QString sender = m.service().mid(1); sender.replace('.', '_');
        const QString path = root + "/request/" + sender + "/" + options.value("handle_token").toString();
        QVariantMap result;
        if (create) {
            const QString session = root + "/session/" + sender + "/" + options.value("session_handle_token").toString();
            created << session; owners.insert(session,m.service());
            result.insert("session_handle",session);
        }
        connection.send(m.createReply(QVariant::fromValue(QDBusObjectPath(path))));
        QTimer::singleShot(delay, this, [=] {
            auto response = QDBusMessage::createTargetedSignal(m.service(),path,"org.freedesktop.portal.Request","Response");
            response << uint(0) << result;
            connection.send(response);
        });
        return true;
    }
    void edge(const QString &session, const QString &name) {
        auto signal = QDBusMessage::createTargetedSignal(owners.value(session),root,global,name);
        signal << QDBusObjectPath(session) << QStringLiteral("arcade-wheel-trigger") << qulonglong(0) << QVariantMap{};
        bus.send(signal);
    }
    void closeSession(const QString &session) {
        auto signal = QDBusMessage::createTargetedSignal(owners.value(session),session,"org.freedesktop.portal.Session","Closed");
        signal << QVariantMap{}; bus.send(signal);
    }
};

class LinuxTriggerTest final : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    PortalFixture *portal = nullptr;
private slots:
    void initTestCase() {
        QVERIFY(data.isValid());
        qunsetenv("ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT");
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("XDG_DATA_HOME",data.path().toUtf8());
        qputenv("XDG_DATA_DIRS",data.filePath("empty").toUtf8());
    }
    void init() {
        portal = new PortalFixture;
        QVERIFY(portal->bus.registerService(service));
        QVERIFY(portal->bus.registerVirtualObject(root,portal,QDBusConnection::SubPath));
    }
    void cleanup() {
        portal->bus.unregisterObject(root,QDBusConnection::UnregisterTree);
        portal->bus.unregisterService(service);
        delete portal; portal = nullptr;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void registrationPrecedesSessionAndEdgesArePaired() {
        // Simulate Qt having already used its shared connection anonymously.
        portal->registered.insert(portal->bus.baseService());
        LinuxBackend backend;
        QSignalSpy status(&backend,&PlatformBackend::triggerStatusChanged);
        QSignalSpy press(&backend,&PlatformBackend::triggerPressed);
        QSignalSpy release(&backend,&PlatformBackend::triggerReleased);
        backend.configureTrigger(trigger());
        QTRY_VERIFY(!status.isEmpty() && status.last().first().toString().startsWith("Ready"));
        QCOMPARE(portal->calls.first(),QString("Register"));
        QCOMPARE(portal->registrations,1);
        QVERIFY(QFile::exists(data.filePath("applications/com.arcadewheel.ArcadeWheel.desktop")));
        const auto session=portal->created.last();
        portal->edge(session,"Activated"); portal->edge(session,"Activated");
        QTRY_COMPARE(press.size(),1);
        portal->edge(session,"Deactivated"); portal->edge(session,"Deactivated");
        QTRY_COMPARE(release.size(),1);
        backend.setShortcutRecording(true);
        portal->edge(session,"Activated"); QTest::qWait(20);
        QCOMPARE(press.size(),1);
        backend.setShortcutRecording(false);
    }
    void registrationFailureDoesNotCreateAnonymousSession() {
        portal->registrationError="Registration rejected";
        LinuxBackend backend; QSignalSpy status(&backend,&PlatformBackend::triggerStatusChanged);
        backend.configureTrigger(trigger());
        QTRY_VERIFY(!status.isEmpty() && status.last().first().toString().contains("Registration rejected"));
        QVERIFY(portal->created.isEmpty());
    }
    void newlyCreatedDesktopEntryIsRetried() {
        portal->registrationError="App info not found for 'com.arcadewheel.ArcadeWheel'";
        LinuxBackend backend; QSignalSpy status(&backend,&PlatformBackend::triggerStatusChanged);
        backend.configureTrigger(trigger());
        QTRY_COMPARE(portal->registrations,1);
        portal->registrationError.clear();
        QTRY_VERIFY(status.last().first().toString().startsWith("Ready"));
        QCOMPARE(portal->registrations,2);
    }
    void rapidChangesDiscardOldSessions() {
        LinuxBackend backend; QSignalSpy status(&backend,&PlatformBackend::triggerStatusChanged);
        backend.configureTrigger(trigger("F8"));
        QTRY_COMPARE(portal->created.size(),1);
        backend.configureTrigger(trigger("Ctrl+Down"));
        QTRY_VERIFY(status.last().first().toString().startsWith("Ready"));
        QCOMPARE(portal->created.size(),2);
        QTRY_VERIFY(portal->closed.contains(portal->created.first()));
        QCOMPARE(portal->binds,1);
    }
    void failedBindCanBeRetried() {
        portal->rejectBind=true;
        LinuxBackend backend; QSignalSpy status(&backend,&PlatformBackend::triggerStatusChanged);
        backend.configureTrigger(trigger());
        QTRY_VERIFY(status.last().first().toString().contains("Binding failed"));
        portal->rejectBind=false;
        backend.configureTrigger(trigger());
        QTRY_VERIFY(status.last().first().toString().startsWith("Ready"));
        QCOMPARE(portal->registrations,1);
        QCOMPARE(portal->created.size(),2);
    }
    void sessionLossCancelsAndReconnects() {
        LinuxBackend backend;
        QSignalSpy status(&backend,&PlatformBackend::triggerStatusChanged);
        QSignalSpy cancelled(&backend,&PlatformBackend::triggerCancelled);
        QSignalSpy pressed(&backend,&PlatformBackend::triggerPressed);
        backend.configureTrigger(trigger());
        QTRY_VERIFY(status.last().first().toString().startsWith("Ready"));
        const auto old=portal->created.last();
        portal->edge(old,"Activated"); QTRY_COMPARE(pressed.size(),1);
        portal->closeSession(old);
        QTRY_COMPARE(cancelled.size(),1);
        QTRY_COMPARE(portal->created.size(),2);
        QTRY_VERIFY(status.last().first().toString().startsWith("Ready"));
        portal->edge(old,"Activated"); QTest::qWait(20);
        QCOMPARE(pressed.size(),1);
    }
    void supportedMediaAndCombinationKeys() {
        LinuxBackend backend;
        for (const QString &key : {"Volume Mute","Volume Up","Volume Down","Ctrl+Down","Meta+Space","Ctrl+Plus","F8"})
            QVERIFY2(backend.shortcutValidationError(key).isEmpty(),qPrintable(key));
        QVERIFY(!backend.shortcutValidationError("Ctrl").isEmpty());
        QVERIFY(!backend.shortcutValidationError("Mouse 4").isEmpty());
    }
};
QTEST_GUILESS_MAIN(LinuxTriggerTest)
#include "tst_LinuxTrigger.moc"
