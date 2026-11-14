#include "core/CrashReport.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <csignal>

class CrashReportTest final : public QObject {
    Q_OBJECT
private slots:
    void pruneKeepsTheNewest()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 30; ++i) {
            QFile file(dir.filePath(QStringLiteral("crash-%1-1.txt").arg(1000 + i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
            file.flush();
            QVERIFY(file.setFileTime(QDateTime::fromSecsSinceEpoch(1000 + i), QFileDevice::FileModificationTime));
            file.close();
        }
        QFile other(dir.filePath("notes.txt"));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.close();
        CrashReport::prune(dir.path());
        const auto left = QDir(dir.path()).entryList({"crash-*.txt"}, QDir::Files, QDir::Name);
        QCOMPARE(left.size(), CrashReport::KeptReports);
        QVERIFY(left.contains("crash-1029-1.txt"));
        QVERIFY(!left.contains("crash-1000-1.txt"));
        QVERIFY(QFileInfo::exists(dir.filePath("notes.txt")));
    }
#ifdef Q_OS_LINUX
    void aFatalSignalWritesAReportAndStillDies()
    {
        // Re-run this executable as the child that crashes itself.
        QTemporaryDir dir;
        QProcess child;
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({QStringLiteral("crashnow")});
        child.setEnvironment(QProcess::systemEnvironment() << QStringLiteral("WHEEL_CRASH_DIR=") + dir.path() << QStringLiteral("WHEEL_CRASH_CHILD=1"));
        child.start();
        QVERIFY(child.waitForFinished(20000));
        QCOMPARE(child.exitStatus(), QProcess::CrashExit);
        const auto reports = QDir(dir.filePath("nested/crashes")).entryList({"crash-*.txt"}, QDir::Files);
        QCOMPARE(reports.size(), 1);
        QFile report(dir.filePath("nested/crashes/" + reports.first()));
        QVERIFY(report.open(QIODevice::ReadOnly));
        const QByteArray text = report.readAll();
        QVERIFY(text.contains("Arcade Wheel 9.9.9"));
        QVERIFY(text.contains("signal: 11"));
        QVERIFY(text.contains("backtrace:"));
    }
#endif
};

int main(int argc, char **argv)
{
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsSet("WHEEL_CRASH_CHILD")) {
        CrashReport::install(qEnvironmentVariable("WHEEL_CRASH_DIR") + "/nested/crashes", "9.9.9");
        raise(SIGSEGV);
        return 0;
    }
#endif
    QCoreApplication app(argc, argv);
    CrashReportTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_CrashReport.moc"
