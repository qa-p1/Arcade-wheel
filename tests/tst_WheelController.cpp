#include "core/WheelController.h"
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickView>
#include <QQuickItem>
#include <QQmlContext>
#include <QWheelEvent>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ControllerBackend final : public PlatformBackend {
public:
    QString name() const override { return "test"; }
    void configureTrigger(const QJsonObject &trigger) override { configured = trigger; ++configures; }
    QString shortcutConflict(const QString &shortcut) const override {
        return shortcut == "Volume Mute" ? QStringLiteral("Volume Mute is already used") : QString();
    }
    bool prepareOverlay(QQuickWindow *, QString *) override { return true; }
    QPointF cursorPosition() const override { return {5, 5}; }
    QVector<DiscoveredApplication> applications() const override {
        return {{"test.desktop","Test app",{}, {}, {}}, {"second.desktop","Second",{}, {}, {}},
                {"third.desktop","Third",{}, {}, {}}, {"fourth.desktop","Fourth",{}, {}, {}},
                {"fifth.desktop","Fifth",{}, {}, {}}};
    }
    bool launchApplication(const QString &id, bool, QString *error) override {
        ++launches;
        launchedIds << id;
        if (fail && error) *error="Intentional launch failure";
        return !fail;
    }
    bool performSystemAction(const QString &id, QString *) override { systemActions << id; return true; }
    bool setStartOnLogin(bool enabled, QString *error) override {
        if (loginFails) { if (error) *error="Login entry refused"; return false; }
        login=enabled; return true;
    }
    bool startOnLogin() const override { return login; }
    void setShortcutRecording(bool enabled) override { recording=enabled; }
    bool recording=false;
    int launches=0;
    QStringList launchedIds;
    int configures=0;
    QJsonObject configured;
    bool fail=false;
    bool login=false;
    bool loginFails=false;
    QStringList systemActions;
};
static QJsonObject readConfig(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}
class WheelControllerTest final : public QObject {
    Q_OBJECT
    static void addGroup(WheelController &controller, const QString &gesture, const QStringList &ids) {
        for (const auto &id : ids)
            controller.setCenterGestureAction(gesture,-1,{{"type","application"},{"name",id},
                {"payload",QVariantMap{{"desktopId",id}}}});
    }
    static void centerClick(WheelController &controller) {
        QVERIFY(controller.centerPressed(controller.visualCenterX(),controller.visualCenterY()));
        controller.centerReleased(controller.visualCenterX(),controller.visualCenterY());
    }
private slots:
    void doubleClickLaunchesWholeGroupOnce() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        const QStringList ids{"test.desktop","second.desktop","third.desktop","fourth.desktop","fifth.desktop"};
        addGroup(controller,"doubleClick",ids);
        controller.pressTrigger(); centerClick(controller);
        QVERIFY(controller.overlayVisible()); QCOMPARE(backend.launches,0);
        centerClick(controller);
        QVERIFY(!controller.overlayVisible());
        QTRY_COMPARE(backend.launchedIds,ids);
        controller.releaseTrigger(); QTest::qWait(300);
        QCOMPARE(backend.launches,5);
    }
    void tripleClickDoesNotAlsoRunDoubleClick() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        addGroup(controller,"doubleClick",{"test.desktop"});
        addGroup(controller,"tripleClick",{"second.desktop","third.desktop"});
        controller.pressTrigger(); centerClick(controller); centerClick(controller);
        QVERIFY(controller.overlayVisible()); QCOMPARE(backend.launches,0);
        centerClick(controller);
        QTRY_COMPARE(backend.launchedIds,QStringList({"second.desktop","third.desktop"}));
        controller.releaseTrigger(); QTest::qWait(300); QCOMPARE(backend.launches,2);
    }
    void releaseResolvesPendingDoubleAndSingleCancels() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        addGroup(controller,"doubleClick",{"test.desktop"});
        addGroup(controller,"tripleClick",{"second.desktop"});
        controller.pressTrigger(); centerClick(controller); centerClick(controller);
        controller.releaseTrigger(); QTRY_COMPARE(backend.launches,1);
        controller.pressTrigger(); centerClick(controller); controller.releaseTrigger();
        QTest::qWait(300); QCOMPARE(backend.launches,1); QVERIFY(!controller.overlayVisible());
    }
    void singleClickRunsGroupAndPlainReleaseStillCloses() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        addGroup(controller,"singleClick",{"test.desktop","second.desktop"});
        controller.pressTrigger(); controller.releaseTrigger();
        QVERIFY(!controller.overlayVisible()); QTest::qWait(10); QCOMPARE(backend.launches,0);
        controller.pressTrigger(); centerClick(controller);
        QVERIFY(!controller.overlayVisible());
        QTRY_COMPARE(backend.launchedIds,QStringList({"test.desktop","second.desktop"}));
        controller.releaseTrigger(); QTest::qWait(300); QCOMPARE(backend.launches,2);
        controller.setCenterGesture("singleClick","Disabled",false);
        controller.pressTrigger(); centerClick(controller);
        QVERIFY(!controller.overlayVisible()); QTest::qWait(10); QCOMPARE(backend.launches,2);
    }
    void singleClickWaitsForMultiClickGestures() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        addGroup(controller,"singleClick",{"test.desktop"});
        addGroup(controller,"doubleClick",{"second.desktop"});
        controller.pressTrigger(); centerClick(controller);
        QVERIFY(controller.overlayVisible()); QCOMPARE(backend.launches,0);
        QTRY_COMPARE(backend.launchedIds,QStringList({"test.desktop"}));
        QVERIFY(!controller.overlayVisible());
        controller.releaseTrigger(); QCOMPARE(backend.launches,1);
        controller.pressTrigger(); centerClick(controller); centerClick(controller);
        QTRY_COMPARE(backend.launchedIds,QStringList({"test.desktop","second.desktop"}));
        controller.releaseTrigger(); QTest::qWait(300); QCOMPARE(backend.launches,2);
        controller.pressTrigger(); centerClick(controller); controller.releaseTrigger();
        QVERIFY(!controller.overlayVisible());
        QTRY_COMPARE(backend.launchedIds,QStringList({"test.desktop","second.desktop","test.desktop"}));
        QTest::qWait(300); QCOMPARE(backend.launches,3);
    }
    void longPressAndLeavingCenter() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        addGroup(controller,"longPress",{"test.desktop","second.desktop","third.desktop"});
        controller.updateSetting("centerGestures","longPressMs",250);
        controller.pressTrigger();
        QVERIFY(controller.centerPressed(controller.visualCenterX(),controller.visualCenterY()));
        QTRY_COMPARE(backend.launches,3);
        controller.centerReleased(controller.visualCenterX(),controller.visualCenterY());
        controller.releaseTrigger(); QCOMPARE(backend.launches,3);
        controller.pressTrigger();
        QVERIFY(controller.centerPressed(controller.visualCenterX(),controller.visualCenterY()));
        controller.pointerMoved(controller.visualCenterX()+100,controller.visualCenterY());
        QVERIFY(!controller.centerGesturePressed());
        QTest::qWait(300); QCOMPARE(backend.launches,3);
        controller.cancelTrigger();
    }
    void cancelledPreviewAndDisabledGesturesNeverRun() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        addGroup(controller,"doubleClick",{"test.desktop"});
        addGroup(controller,"tripleClick",{"second.desktop"});
        controller.pressTrigger(); centerClick(controller); centerClick(controller);
        controller.cancelTrigger(); controller.pressTrigger();
        QTest::qWait(300); QCOMPARE(backend.launches,0);
        controller.previewOverlay(); centerClick(controller); centerClick(controller); centerClick(controller);
        QCOMPARE(backend.launches,0);
        controller.cancelTrigger();
        controller.setCenterGesture("doubleClick","Disabled",false);
        controller.setCenterGesture("tripleClick","Disabled",false);
        controller.pressTrigger(); centerClick(controller);
        QVERIFY(!controller.overlayVisible()); QCOMPARE(backend.launches,0);
    }
    void groupContinuesAfterMissingApplication() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        QSignalSpy errors(&controller,&WheelController::actionFailed);
        addGroup(controller,"doubleClick",{"missing.desktop","test.desktop","second.desktop"});
        controller.pressTrigger(); centerClick(controller); centerClick(controller);
        QTRY_COMPARE(backend.launches,2);
        QCOMPARE(errors.size(),1); QVERIFY(!controller.overlayVisible());
    }
    void nativeDoubleClickReachesCenterRecognizer() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); addGroup(controller,"doubleClick",{"test.desktop","second.desktop"});
        QQuickView overlay;
        overlay.rootContext()->setContextProperty("controller",&controller);
        overlay.setSource(QUrl::fromLocalFile(QFINDTESTDATA("../qml/wheel/Overlay.qml")));
        QCOMPARE(overlay.status(),QQuickView::Ready);
        overlay.rootObject()->setProperty("controller",QVariant::fromValue(static_cast<QObject *>(&controller)));
        overlay.setResizeMode(QQuickView::SizeRootObjectToView);
        controller.setOverlayView(&overlay); controller.pressTrigger(); QTest::qWait(50);
        const QPointF center(overlay.width()/2.0,overlay.height()/2.0);
        QTest::mouseDClick(&overlay,Qt::LeftButton,Qt::NoModifier,center.toPoint(),20);
        QTRY_COMPARE(backend.launches,2);
        controller.releaseTrigger(); QCOMPARE(backend.launches,2);
    }
    void centerEditorPersistsActionsAndKeepsTrigger() {
        QTemporaryDir dir;
        const QString path=dir.filePath("config.json");
        {
            ControllerBackend backend; WheelController controller(&backend,nullptr,path);
            controller.initialize(); QVERIFY(controller.applyShortcut("Tab",false));
            const int registrations=backend.configures;
            addGroup(controller,"doubleClick",{"test.desktop","second.desktop","third.desktop"});
            controller.setCenterGesture("doubleClick","My workspace",true);
            controller.moveCenterGestureAction("doubleClick",2,0);
            controller.removeCenterGestureAction("doubleClick",2);
            QCOMPARE(backend.configures,registrations);
            QQuickView settings;
            settings.rootContext()->setContextProperty("controller",&controller);
            settings.setSource(QUrl::fromLocalFile(QFINDTESTDATA("../qml/settings/Settings.qml")));
            QCOMPARE(settings.status(),QQuickView::Ready);
            settings.rootObject()->setProperty("currentPage","Center gestures");
            QVERIFY(settings.rootObject()->findChild<QObject *>("centerGesturesPage"));
        }
        ControllerBackend backend; WheelController controller(&backend,nullptr,path); controller.initialize();
        QCOMPARE(backend.configured.value("shortcut").toString(),QString("Tab"));
        const auto group=controller.config().value("centerGestures").toMap().value("doubleClick").toMap();
        QCOMPARE(group.value("name").toString(),QString("My workspace"));
        QVERIFY(group.value("enabled").toBool());
        QCOMPARE(group.value("actions").toList().size(),2);
        QCOMPARE(group.value("actions").toList().first().toMap().value("payload").toMap().value("desktopId").toString(),QString("third.desktop"));
    }
    void mutePulseReportsUnsupportedHoldWithoutChangingInteraction() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        QVERIFY(controller.applyShortcut("Volume Mute",true));
        controller.pressTrigger(); controller.releaseTrigger();
        QVERIFY(!controller.overlayVisible());
        QVERIFY(controller.triggerStatus().contains("immediate release"));
        QCOMPARE(backend.launches,0);
        controller.pressTrigger(); QTest::qWait(60);
        controller.releaseTrigger();
        QVERIFY(!controller.overlayVisible());
    }
    void wheelEventSwitchesDeckWhileModifierHeld() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); controller.createDeck(); controller.selectDeck(0);
        QQuickView overlay;
        overlay.rootContext()->setContextProperty("controller",&controller);
        overlay.setSource(QUrl::fromLocalFile(QFINDTESTDATA("../qml/wheel/Overlay.qml")));
        QCOMPARE(overlay.status(),QQuickView::Ready);
        overlay.rootObject()->setProperty("controller",QVariant::fromValue(static_cast<QObject *>(&controller)));
        overlay.setResizeMode(QQuickView::SizeRootObjectToView);
        controller.setOverlayView(&overlay); controller.pressTrigger();
        QTest::qWait(50);
        const QPointF center(overlay.width()/2.0,overlay.height()/2.0);
        QWheelEvent wheel(center,center,{},QPoint(0,-120),Qt::NoButton,Qt::AltModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(&overlay,&wheel);
        QCOMPARE(controller.currentDeckIndex(),1);
        controller.cancelTrigger();
    }
    void settingsFocusDoesNotCaptureShortcutOrCancelWheel() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView overlay; controller.setOverlayView(&overlay);
        QQuickView settings;
        settings.rootContext()->setContextProperty("controller",&controller);
        settings.setSource(QUrl::fromLocalFile(QFINDTESTDATA("../qml/settings/Settings.qml")));
        QCOMPARE(settings.status(),QQuickView::Ready);
        settings.rootObject()->setProperty("currentPage","Trigger");
        settings.show();
        auto *field=settings.rootObject()->findChild<QQuickItem *>("shortcutField");
        QVERIFY(field);
        field->forceActiveFocus();
        QCoreApplication::processEvents();
        QVERIFY(!backend.recording);
        QVERIFY(QMetaObject::invokeMethod(field,"beginRecording"));
        QVERIFY(backend.recording);
        QVERIFY(QMetaObject::invokeMethod(field,"cancelRecording"));
        settings.rootObject()->forceActiveFocus();
        controller.pressTrigger();
        field->forceActiveFocus();
        QCoreApplication::processEvents();
        QVERIFY(!backend.recording);
        QVERIFY(controller.overlayVisible());
        controller.releaseTrigger();
        QVERIFY(!controller.overlayVisible());
    }
    void shortcutAndOverrideSurviveRestart() {
        QTemporaryDir dir;
        const auto path=dir.filePath("config.json");
        {
            ControllerBackend backend;
            WheelController controller(&backend,nullptr,path);
            controller.initialize();
            QVERIFY(!controller.applyShortcut("Volume Mute",false));
            QVERIFY(controller.applyShortcut("Volume Mute",true));
            QCOMPARE(backend.configured.value("overrideConflict").toBool(),true);
            const int count=backend.configures;
            controller.updateSetting("trigger","holdThresholdMs",120);
            QCOMPARE(backend.configures,count);
        }
        {
            ControllerBackend backend;
            WheelController controller(&backend,nullptr,path);
            controller.initialize();
            QCOMPARE(backend.configured.value("shortcut").toString(),QString("Volume Mute"));
            QVERIFY(backend.configured.value("overrideConflict").toBool());
            QCOMPARE(backend.configured.value("holdThresholdMs").toInt(),120);
            QVERIFY(controller.applyShortcut("Ctrl+Down",false));
        }
        ControllerBackend backend;
        WheelController controller(&backend,nullptr,path);
        controller.initialize();
        QCOMPARE(backend.configured.value("shortcut").toString(),QString("Ctrl+Down"));
        QVERIFY(!backend.configured.value("overrideConflict").toBool());
    }
    void centeredSelectionLaunchesBeforeExitFinishes() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView view; controller.setOverlayView(&view);
        controller.pressTrigger();
        QCOMPARE(controller.visualCenterX(),view.width()/2.0);
        QCOMPARE(controller.visualCenterY(),view.height()/2.0);
        QCOMPARE(controller.selectedIndex(),-1);
        controller.pointerMoved(controller.visualCenterX(),controller.visualCenterY()-160);
        QCOMPARE(controller.selectedIndex(),0);
        controller.releaseTrigger();
        QVERIFY(!controller.overlayVisible());
        QVERIFY(view.isVisible()); // Native surface remains for the short exit.
        QTRY_COMPARE(backend.launches,1);
        controller.finishClose(); QVERIFY(!view.isVisible());
        controller.releaseTrigger(); QCOMPARE(backend.launches,1);
    }
    void centerAndPreviewNeverExecute() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView view; controller.setOverlayView(&view);
        controller.pressTrigger(); controller.releaseTrigger();
        controller.previewOverlay();
        controller.pointerMoved(controller.visualCenterX(),controller.visualCenterY()-160);
        controller.activateSlot(0); QCOMPARE(backend.launches,0);
        controller.releaseTrigger(); QTest::qWait(10); QCOMPARE(backend.launches,0);
        QVERIFY(!controller.overlayVisible());
    }
    void clickLaunchAndRapidReopen() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView view; controller.setOverlayView(&view);
        controller.showWheel(); controller.activateSlot(0);
        controller.pressTrigger(); controller.finishClose();
        QVERIFY(controller.overlayVisible()); QVERIFY(view.isVisible());
        QTRY_COMPARE(backend.launches,1);
        controller.cancelTrigger(); controller.finishClose(); QVERIFY(!view.isVisible());
    }
    void shortHoldAndBackendDisconnectCancel() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView view; controller.setOverlayView(&view);
        controller.updateSetting("trigger","holdThresholdMs",100);
        controller.pressTrigger(); controller.releaseTrigger();
        QTest::qWait(130); QVERIFY(!controller.overlayVisible()); QCOMPARE(backend.launches,0);
        controller.showWheel(); controller.pointerMoved(controller.visualCenterX(),controller.visualCenterY()-160);
        emit backend.triggerCancelled(); QTest::qWait(10); QCOMPARE(backend.launches,0);
    }
    void liveSettingsAreCoalescedAndFlushed() {
        QTemporaryDir dir; const QString path=dir.filePath("config.json");
        {
            ControllerBackend backend; WheelController controller(&backend,nullptr,path); controller.initialize();
            for (int radius=150; radius<=200; radius+=2) controller.updateSetting("appearance","radius",radius);
            // Applied at once for the live preview, written once after the drag settles.
            QCOMPARE(controller.config().value("appearance").toMap().value("radius").toInt(),200);
            QCOMPARE(readConfig(path).value("appearance").toObject().value("radius").toInt(),148);
            QTRY_COMPARE(readConfig(path).value("appearance").toObject().value("radius").toInt(),200);
            controller.updateSetting("appearance","radius",210); // Pending when the controller is destroyed.
        }
        QCOMPARE(readConfig(path).value("appearance").toObject().value("radius").toInt(),210);
    }
    void startOnLoginFollowsTheOperatingSystem() {
        QTemporaryDir dir; const QString path=dir.filePath("config.json");
        const QString exported=dir.filePath("export.json");
        {
            ControllerBackend backend; WheelController controller(&backend,nullptr,path); controller.initialize();
            controller.updateSetting("general","startOnLogin",true);
            QVERIFY(backend.login);
            QVERIFY(controller.exportConfig(exported));
            controller.resetDefaults(); // Reset turns the login entry off as well as the flag.
            QVERIFY(!backend.login);
            QVERIFY(controller.importConfig(exported)); // Import turns it back on.
            QVERIFY(backend.login);
            QVERIFY(readConfig(path).value("general").toObject().value("startOnLogin").toBool());
        }
        {
            // The entry was removed outside the app: show the real state.
            ControllerBackend backend; WheelController controller(&backend,nullptr,path); controller.initialize();
            QVERIFY(!controller.config().value("general").toMap().value("startOnLogin").toBool());
            backend.loginFails=true; // A refused import keeps the flag truthful.
            QVERIFY(controller.importConfig(exported));
            QVERIFY(!controller.config().value("general").toMap().value("startOnLogin").toBool());
            QVERIFY(!readConfig(path).value("general").toObject().value("startOnLogin").toBool());
            QVERIFY(controller.lastError().contains("refused"));
        }
    }
    void screenshotWaitsForTheWheelToLeaveTheScreen() {
        QTemporaryDir dir; ControllerBackend backend;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView view; controller.setOverlayView(&view);
        const QString deck=controller.currentDeck().value("id").toString();
        controller.setAction(deck,0,{{"type","system"},{"name","Screenshot"},{"payload",QVariantMap{{"id","screenshot"}}}});
        controller.setAction(deck,1,{{"type","system"},{"name","Lock"},{"payload",QVariantMap{{"id","lock"}}}});
        controller.showWheel(); controller.activateSlot(1);
        QTRY_COMPARE(backend.systemActions,QStringList({"lock"})); // Other actions stay immediate.
        controller.showWheel(); controller.activateSlot(0);
        QTest::qWait(150); QCOMPARE(backend.systemActions.size(),1);
        QTRY_COMPARE(backend.systemActions,QStringList({"lock","screenshot"}));
    }
    void failureClosesAndReports() {
        QTemporaryDir dir; ControllerBackend backend; backend.fail=true;
        WheelController controller(&backend,nullptr,dir.filePath("config.json"));
        controller.initialize(); QQuickView view; controller.setOverlayView(&view);
        QSignalSpy failures(&controller,&WheelController::actionFailed);
        controller.showWheel(); controller.activateSlot(0);
        QTRY_COMPARE(failures.size(),1); QVERIFY(!controller.overlayVisible());
        QVERIFY(controller.lastError().contains("Intentional"));
        QTRY_VERIFY_WITH_TIMEOUT(!view.isVisible(),500);
    }
};
QTEST_MAIN(WheelControllerTest)
#include "tst_WheelController.moc"
