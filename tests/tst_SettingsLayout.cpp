// Loads Settings in light and dark at common window sizes and fails when content is pushed
// past the window or a clipping container horizontally. Set
// ARCADE_WHEEL_SHOTS to a directory to save a PNG of every page and size.
#include "core/IconImageProvider.h"
#include "core/WheelController.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QJsonArray>
#include <QJsonObject>
#include <ArcadeInstall.h>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(ArcadePlugin)
Q_IMPORT_QML_PLUGIN(ArcadeWheelPlugin)

namespace {
class LayoutBackend final : public PlatformBackend {
public:
    QString name() const override { return "test"; }
    void configureTrigger(const QJsonObject &) override {}
    bool prepareOverlay(QQuickWindow *, QString *) override { return true; }
    QPointF cursorPosition() const override { return {5, 5}; }
    QVector<DiscoveredApplication> applications() const override
    {
        return {{"org.gnome.Nautilus.desktop", "Files", "org.gnome.Nautilus", {}, {}},
                {"org.example.with-a-really-long-identifier-that-should-elide.desktop",
                 "An application with a deliberately long display name",
                 {},
                 {},
                 {}}};
    }
    bool launchApplication(const QString &, bool, QString *) override { return true; }
    bool performSystemAction(const QString &, QString *) override { return true; }
    bool setStartOnLogin(bool, QString *) override { return true; }
    bool startOnLogin() const override { return false; }
};

bool hidden(const QQuickItem *item)
{
    return !item->isVisible() || item->opacity() <= 0.0;
}

// Collects items whose painted area leaves the region they can be seen in.
void collectOverflow(QQuickItem *item, const QRectF &bounds, QStringList *problems)
{
    if (hidden(item)) return;
    QRectF rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    const QVariant painted = item->property("paintedWidth");
    const int alignment = item->property("horizontalAlignment").toInt();
    if (painted.isValid() && !item->property("text").toString().isEmpty() && painted.toReal() > item->width() + 1 && alignment == Qt::AlignLeft)
        rect.setWidth(item->mapRectToScene(QRectF(0, 0, painted.toReal(), 1)).width());
    if (item->width() > 0 && item->height() > 0 && (rect.right() > bounds.right() + 1 || rect.left() < bounds.left() - 1)) {
        QString name = item->objectName();
        if (name.isEmpty()) name = QString::fromLatin1(item->metaObject()->className());
        const QString text = item->property("text").toString();
        QStringList chain;
        for (QQuickItem *p = item->parentItem(); p && chain.size() < 4; p = p->parentItem())
            chain << (p->objectName().isEmpty() ? QString::fromLatin1(p->metaObject()->className()) : p->objectName());
        name += QStringLiteral("[%1x%2,%3 children]").arg(item->width()).arg(item->height()).arg(item->childItems().size());
        problems->append(QStringLiteral("%1 '%2' spans x %3..%4, visible %5..%6 (in %7)")
                             .arg(name, text.left(40))
                             .arg(rect.left())
                             .arg(rect.right())
                             .arg(bounds.left())
                             .arg(bounds.right())
                             .arg(chain.join(QStringLiteral(" < "))));
        return; // one report per branch
    }
    // Only horizontal clipping matters: vertical overflow scrolls.
    QRectF childBounds = bounds;
    if (item->clip()) {
        const QRectF own = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        childBounds.setLeft(qMax(bounds.left(), own.left()));
        childBounds.setRight(qMin(bounds.right(), own.right()));
        if (childBounds.width() <= 0) return; // nothing inside can be seen
    }
    for (QQuickItem *child : item->childItems()) collectOverflow(child, childBounds, problems);
}
// Repeater delegates are items, not QObject children: walk the item tree.
QQuickItem *itemNamed(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name) return item;
    for (QQuickItem *child : item->childItems())
        if (QQuickItem *found = itemNamed(child, name)) return found;
    return nullptr;
}
} // namespace

class SettingsLayoutTest final : public QObject {
    Q_OBJECT
    QTemporaryDir arcadeHome; // keep the real Arcade registry out of the layout
private slots:
    void initTestCase()
    {
        QVERIFY(arcadeHome.isValid());
        qputenv("ARCADE_HOME", arcadeHome.path().toUtf8());
        QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7"));
    }
    void pagesFitHorizontally_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<QString>("theme");
        for (const char *theme : {"light", "dark"})
            for (const QSize size : {QSize(1920, 1080), QSize(1080, 720), QSize(900, 600), QSize(720, 520)})
                QTest::addRow("%s-%dx%d", theme, size.width(), size.height()) << size << QString::fromLatin1(theme);
    }
    void pagesFitHorizontally()
    {
        QFETCH(QSize, size);
        QFETCH(QString, theme);
        QTemporaryDir dir;
        LayoutBackend backend;
        WheelController controller(&backend, nullptr, dir.filePath("config.json"));
        controller.initialize();
        controller.updateSetting(QStringLiteral("general"), QStringLiteral("theme"), theme);
        QQuickView overlay;
        controller.setOverlayView(&overlay);
        QQuickView settings;
        settings.setResizeMode(QQuickView::SizeRootObjectToView);
        settings.engine()->addImageProvider(QStringLiteral("icons"), new IconImageProvider);
        settings.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
        settings.loadFromModule("ArcadeWheel", "SettingsWindow");
        QCOMPARE(settings.status(), QQuickView::Ready);
        QObject *root = settings.rootObject();
        settings.resize(size);
        settings.show();
        QVERIFY(QTest::qWaitForWindowExposed(&settings));
        QCOMPARE(settings.rootObject()->width(), qreal(size.width()));

        auto *sidebar = root->findChild<QQuickItem *>("sidebar");
        QVERIFY(sidebar);
        QCOMPARE(sidebar->property("version").toString(), QStringLiteral("9.8.7"));

        const QString shots = qEnvironmentVariable("ARCADE_WHEEL_SHOTS");
        if (!shots.isEmpty()) QDir().mkpath(shots);
        const QVariantList pages = root->property("pages").toList();
        QCOMPARE(pages.size(), 10);
        QStringList failures;
        for (int index = 0; index < pages.size(); ++index) {
            const QString page = pages.at(index).toMap().value(QStringLiteral("title")).toString();
            QVERIFY(QMetaObject::invokeMethod(root, "showPage", Q_ARG(QVariant, index)));
            QTest::qWait(300);
            QCoreApplication::processEvents();
            QStringList problems;
            collectOverflow(settings.contentItem(), QRectF(QPointF(0, 0), QSizeF(size)), &problems);
            for (const QString &problem : problems) failures << page + QStringLiteral(": ") + problem;
            if (!shots.isEmpty()) {
                QString file = page.toLower().replace(u' ', u'-');
                const QImage image = settings.grabWindow();
                QVERIFY(image.save(QDir(shots).filePath(
                    QStringLiteral("%1-%2-%3x%4.png").arg(theme, file).arg(size.width()).arg(size.height()))));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
    }
    // The action picker opens over Settings; each type's fields fit too.
    void actionPickerFits_data() { pagesFitHorizontally_data(); }
    void actionPickerFits()
    {
        QFETCH(QSize, size);
        QFETCH(QString, theme);
        QTemporaryDir dir;
        LayoutBackend backend;
        WheelController controller(&backend, nullptr, dir.filePath("config.json"));
        controller.initialize();
        controller.updateSetting(QStringLiteral("general"), QStringLiteral("theme"), theme);
        QQuickView overlay;
        controller.setOverlayView(&overlay);
        QQuickView settings;
        settings.setResizeMode(QQuickView::SizeRootObjectToView);
        settings.engine()->addImageProvider(QStringLiteral("icons"), new IconImageProvider);
        settings.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
        settings.loadFromModule("ArcadeWheel", "SettingsWindow");
        QCOMPARE(settings.status(), QQuickView::Ready);
        settings.resize(size);
        settings.show();
        QVERIFY(QTest::qWaitForWindowExposed(&settings));
        auto *picker = settings.rootObject()->findChild<QObject *>("actionPicker");
        QVERIFY(picker);
        const QVariantMap deck = controller.currentDeck();
        const QVariantMap action = deck.value(QStringLiteral("actions")).toList().first().toMap();
        QVERIFY(QMetaObject::invokeMethod(picker, "editAction", Q_ARG(QVariant, deck.value(QStringLiteral("id"))),
                                          Q_ARG(QVariant, 0), Q_ARG(QVariant, action)));
        QTRY_VERIFY(picker->property("opened").toBool());
        QTest::qWait(600); // the preview's entrance animation settles
        const QString shots = qEnvironmentVariable("ARCADE_WHEEL_SHOTS");
        if (!shots.isEmpty()) QDir().mkpath(shots);
        QStringList failures;
        for (const char *type : {"application", "command", "system", "arcade", "plugin"}) {
            picker->setProperty("selectedType", QString::fromLatin1(type));
            QTest::qWait(200);
            QStringList problems;
            collectOverflow(settings.contentItem(), QRectF(QPointF(0, 0), QSizeF(size)), &problems);
            for (const QString &problem : problems) failures << QString::fromLatin1(type) + QStringLiteral(": ") + problem;
            if (!shots.isEmpty())
                QVERIFY(settings.grabWindow().save(QDir(shots).filePath(
                    QStringLiteral("%1-picker-%2-%3x%4.png").arg(theme, QString::fromLatin1(type)).arg(size.width()).arg(size.height()))));
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
    }
    // The first-run prompt on the kit: Link's shared strings, the main
    // action rightmost and in Wheel's accent, both options.
    void installPromptUsesTheKit()
    {
        const QJsonObject strings = ArcadeLink::ArcadeInstall::strings();
        const QJsonObject notInstalled = strings.value(QStringLiteral("notInstalled")).toObject();
        QVERIFY(!notInstalled.isEmpty());
        QStringList buttons;
        for (const auto &name : notInstalled.value(QStringLiteral("buttons")).toArray()) buttons << name.toString();
        const auto labels = notInstalled.value(QStringLiteral("options")).toArray();
        const QVariantList options{QVariantMap{{QStringLiteral("label"), labels.at(0).toString()}, {QStringLiteral("checked"), true}},
                                   QVariantMap{{QStringLiteral("label"), labels.at(1).toString()}, {QStringLiteral("checked"), true}}};
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{QStringLiteral("heading"), QStringLiteral("Install Arcade Wheel?")},
                                     {QStringLiteral("paragraphs"), QStringList{QStringLiteral("Arcade Wheel will be installed for you.")}},
                                     {QStringLiteral("options"), options},
                                     {QStringLiteral("buttons"), buttons}});
        engine.loadFromModule("ArcadeWheel", "InstallPrompt");
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *primary = itemNamed(window->contentItem(), QStringLiteral("installButton-0"));
        auto *secondary = itemNamed(window->contentItem(), QStringLiteral("installButton-1"));
        QVERIFY(primary);
        QVERIFY(secondary);
        QCOMPARE(primary->property("variant").toString(), QStringLiteral("primary"));
        // Opened in Wheel's amber, not fading from the kit's default accent.
        const QQuickItem *fill = primary->property("background").value<QQuickItem *>();
        QVERIFY(fill);
        const QColor amber = fill->property("color").value<QColor>();
        QVERIFY2(amber == QColor(QStringLiteral("#9e6506")) || amber == QColor(QStringLiteral("#f59e0b")),
                 qPrintable(amber.name()));
        QVERIFY(primary->mapToScene({0, 0}).x() > secondary->mapToScene({0, 0}).x());
        QVERIFY(itemNamed(window->contentItem(), QStringLiteral("installOption-1")));
        QStringList problems;
        collectOverflow(window->contentItem(), QRectF(QPointF(0, 0), window->size()), &problems);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(u'\n')));
        const QString shots = qEnvironmentVariable("ARCADE_WHEEL_SHOTS");
        if (!shots.isEmpty()) {
            QDir().mkpath(shots);
            QVERIFY(window->grabWindow().save(QDir(shots).filePath(QStringLiteral("install-prompt.png"))));
        }
        QVERIFY(QMetaObject::invokeMethod(primary, "clicked"));
        QCOMPARE(window->property("clicked").toInt(), 0);
    }
};

QTEST_MAIN(SettingsLayoutTest)
#include "tst_SettingsLayout.moc"
