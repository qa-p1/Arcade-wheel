#include "core/IconImageProvider.h"

#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QUrl>

IconImageProvider::IconImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int width = qBound(16, requestedSize.width() > 0 ? requestedSize.width() : 96, 256);
    const int height = qBound(16, requestedSize.height() > 0 ? requestedSize.height() : width, 256);
    QString name = QUrl::fromPercentEncoding(id.toUtf8());
    if (name.startsWith(QStringLiteral("qrc:/"))) name = name.mid(3);
    QIcon icon = QFileInfo::exists(name) ? QIcon(name) : QIcon::fromTheme(name);
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
    }
    if (size) *size = image.size();
    return image;
}
