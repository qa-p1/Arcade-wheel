#include "core/CrashReport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#if defined(Q_OS_LINUX)
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace CrashReport {

void prune(const QString &folder, int keep)
{
    const auto reports = QDir(folder).entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time);
    for (int i = keep; i < reports.size(); ++i) QFile::remove(reports.at(i).absoluteFilePath());
}

#if defined(Q_OS_LINUX)
namespace {
char reportPath[4096];
char header[256];
char altStack[64 * 1024];

void writeAll(int fd, const char *text)
{
    const size_t size = strlen(text);
    size_t done = 0;
    while (done < size) {
        const ssize_t n = write(fd, text + done, size - done);
        if (n <= 0) return;
        done += static_cast<size_t>(n);
    }
}

// Only async-signal-safe calls: mkdir, open, write, backtrace_symbols_fd.
void onFatalSignal(int signalNumber)
{
    for (char *p = reportPath + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(reportPath, 0700);
        *p = '/';
    }
    const int fd = open(reportPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) {
        writeAll(fd, header);
        writeAll(fd, "signal: ");
        char digits[16];
        int n = 0;
        for (int v = signalNumber; v > 0 && n < 15; v /= 10) digits[n++] = static_cast<char>('0' + v % 10);
        for (int i = n - 1; i >= 0; --i) {
            const char one[2] = {digits[i], 0};
            writeAll(fd, one);
        }
        writeAll(fd, "\nbacktrace:\n");
        void *frames[64];
        const int count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, count, fd);
        close(fd);
    }
    signal(signalNumber, SIG_DFL);
    raise(signalNumber);
}
} // namespace

void install(const QString &folder, const QString &version)
{
    prune(folder, KeptReports - 1);
    const QString name = QStringLiteral("/crash-%1-%2.txt").arg(static_cast<qint64>(time(nullptr))).arg(getpid());
    strncpy(reportPath, QFile::encodeName(folder + name).constData(), sizeof(reportPath) - 1);
    const QByteArray head = "Arcade Wheel " + version.toUtf8() + " crash report (stays on this computer)\n";
    strncpy(header, head.constData(), sizeof(header) - 1);
    // Warm up backtrace() so its first call does not allocate in the handler.
    void *warm[1];
    backtrace(warm, 1);
    stack_t stack{};
    stack.ss_sp = altStack;
    stack.ss_size = sizeof(altStack);
    sigaltstack(&stack, nullptr);
    struct sigaction action {};
    action.sa_handler = onFatalSignal;
    action.sa_flags = SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (const int s : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) sigaction(s, &action, nullptr);
}
#else
void install(const QString &, const QString &) {}
#endif

} // namespace CrashReport
