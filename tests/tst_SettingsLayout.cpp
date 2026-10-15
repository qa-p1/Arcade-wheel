// Loads Settings at common window sizes and fails when content is pushed
// past the window or a clipping container horizontally. Set
// ARCADE_WHEEL_SHOTS to a directory to save a PNG of every page and size.
#include "core/IconImageProvider.h"
#include "core/WheelController.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

namespace {
class LayoutBackend final : public PlatformBackend {
public:
    QString name() const override { return "test"; }
    void configureTrigger(const QJsonObject &) override {}
    bool prepareOverlay(QQuickWindow *, QString *) override { return true; }
    QPointF cursorPosition() const override { return {5, 5}; }
    QVector<DiscoveredApplication> applications() const override {
        return {{"org.gnome.Nautilus.desktop", "Files", "org.gnome.Nautilus", {}, {}},
                {"org.example.with-a-really-long-identifier-that-should-elide.desktop",
                 "An application with a deliberately long display name", {}, {}, {}}};
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
    if (painted.isValid() && !item->property("text").toString().isEmpty()
        && painted.toReal() > item->width() + 1 && alignment == Qt::AlignLeft)
        rect.setWidth(item->mapRectToScene(QRectF(0, 0, painted.toReal(), 1)).width());
    if (item->width() > 0 && item->height() > 0
        && (rect.right() > bounds.right() + 1 || rect.left() < bounds.left() - 1)) {
        QString name = item->objectName();
        if (name.isEmpty()) name = QString::fromLatin1(item->metaObject()->className());
        const QString text = item->property("text").toString();
        QStringList chain;
        for (QQuickItem *p = item->parentItem(); p && chain.size() < 4; p = p->parentItem())
            chain << (p->objectName().isEmpty() ? QString::fromLatin1(p->metaObject()->className()) : p->objectName());
        name += QStringLiteral("[%1x%2,%3 children]").arg(item->width()).arg(item->height()).arg(item->childItems().size());
        problems->append(QStringLiteral("%1 '%2' spans x %3..%4, visible %5..%6 (in %7)")
                             .arg(name, text.left(40)).arg(rect.left()).arg(rect.right())
                             .arg(bounds.left()).arg(bounds.right()).arg(chain.join(QStringLiteral(" < "))));
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
} // namespace

class SettingsLayoutTest final : public QObject {
    Q_OBJECT
    QTemporaryDir arcadeHome; // keep the real Arcade registry out of the layout
private slots:
    void initTestCase() {
        QVERIFY(arcadeHome.isValid());
        qputenv("ARCADE_HOME", arcadeHome.path().toUtf8());
        QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7"));
    }
    void pagesFitHorizontally_data() {
        QTest::addColumn<QSize>("size");
        for (const QSize size : {QSize(1920, 1080), QSize(1228, 693), QSize(1000, 650), QSize(800, 560)})
            QTest::addRow("%dx%d", size.width(), size.height()) << size;
    }
    void pagesFitHorizontally() {
        QFETCH(QSize, size);
        QTemporaryDir dir;
        LayoutBackend backend;
        WheelController controller(&backend, nullptr, dir.filePath("config.json"));
        controller.initialize();
        QQuickView overlay;
        controller.setOverlayView(&overlay);
        QQuickView settings;
        settings.setColor(QColor(QStringLiteral("#0d1118")));
        settings.setResizeMode(QQuickView::SizeRootObjectToView);
        settings.engine()->addImageProvider(QStringLiteral("icons"), new IconImageProvider);
        settings.rootContext()->setContextProperty("controller", &controller);
        settings.setSource(QUrl::fromLocalFile(QFINDTESTDATA("../qml/settings/Settings.qml")));
        QCOMPARE(settings.status(), QQuickView::Ready);
        settings.resize(size);
        settings.show();
        QVERIFY(QTest::qWaitForWindowExposed(&settings));
        QCOMPARE(settings.rootObject()->width(), qreal(size.width()));

        auto *footer = settings.rootObject()->findChild<QQuickItem *>("versionLabel");
        QVERIFY(footer);
        QVERIFY2(footer->property("text").toString().contains(QStringLiteral("9.8.7")),
                 qPrintable(footer->property("text").toString()));

        const QString shots = qEnvironmentVariable("ARCADE_WHEEL_SHOTS");
        if (!shots.isEmpty()) QDir().mkpath(shots);
        const QStringList pages = settings.rootObject()->property("pages").toStringList();
        QVERIFY(!pages.isEmpty());
        QStringList failures;
        for (const QString &page : pages) {
            settings.rootObject()->setProperty("currentPage", page);
            QTest::qWait(400);
            QCoreApplication::processEvents();
            QStringList problems;
            collectOverflow(settings.contentItem(), QRectF(QPointF(0, 0), QSizeF(size)), &problems);
            for (const QString &problem : problems) failures << page + QStringLiteral(": ") + problem;
            if (!shots.isEmpty()) {
                QString file = page.toLower().replace(u' ', u'-');
                const QImage image = settings.grabWindow();
                QVERIFY(image.save(QDir(shots).filePath(
                    QStringLiteral("%1-%2x%3.png").arg(file).arg(size.width()).arg(size.height()))));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
    }
};

QTEST_MAIN(SettingsLayoutTest)
#include "tst_SettingsLayout.moc"
