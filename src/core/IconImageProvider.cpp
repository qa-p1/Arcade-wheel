#include "core/IconImageProvider.h"

#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QUrl>

IconImageProvider::IconImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int width = qBound(16, requestedSize.width() > 0 ? requestedSize.width() : 96, 256);
    const int height = qBound(16, requestedSize.height() > 0 ? requestedSize.height() : width, 256);
    // QML adds "?v=<generation>" after the encoded name to bypass its cache.
    QString name = QUrl::fromPercentEncoding(id.section(u'?', 0, 0).toUtf8());
    if (name.startsWith(QStringLiteral("qrc:/"))) name = name.mid(3);
    QIcon icon = QFileInfo::exists(name) ? QIcon(name) : QIcon::fromTheme(name);
    // Current Adwaita ships only symbolic action icons (media-playback-start-symbolic).
    bool symbolic = false;
    if (icon.isNull() && !QFileInfo::exists(name)) {
        icon = QIcon::fromTheme(name + QStringLiteral("-symbolic"));
        if (icon.isNull()) {
            // Names Adwaita has under another standard name only.
            static const QHash<QString, QString> aliases{
                {QStringLiteral("internet-web-browser"), QStringLiteral("web-browser-symbolic")},
                {QStringLiteral("system-suspend"), QStringLiteral("weather-clear-night-symbolic")},
                {QStringLiteral("system-suspend-hibernate"), QStringLiteral("weather-clear-night-symbolic")},
                {QStringLiteral("window-pin"), QStringLiteral("view-pin-symbolic")},
                {QStringLiteral("applications-other"), QStringLiteral("view-app-grid-symbolic")}};
            if (const auto alias = aliases.constFind(name); alias != aliases.cend()) icon = QIcon::fromTheme(*alias);
        }
        symbolic = !icon.isNull();
    }
    if (icon.isNull()) icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    QImage image(QSize(width, height), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    if (icon.isNull()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#64748b")));
        painter.drawRoundedRect(QRectF(5, 5, width - 10, height - 10), width * .22, height * .22);
        painter.setPen(QColor(QStringLiteral("#f8fafc")));
        QFont font = painter.font();
        font.setPixelSize(qMin(width, height) * .46);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("A"));
    } else {
        icon.paint(&painter, image.rect(), Qt::AlignCenter, QIcon::Normal, QIcon::On);
    }
    if (name.startsWith(QStringLiteral(":/assets/arcade/"))) {
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), QColor(QStringLiteral("#E7EAF0")));
    } else if (symbolic) {
        // A mid grey that reads on the light and the dark cards.
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), QColor(QStringLiteral("#7C8596")));
    }
    if (size) *size = image.size();
    return image;
}
