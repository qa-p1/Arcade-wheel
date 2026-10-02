#pragma once

#include <QQuickImageProvider>

class IconImageProvider final : public QQuickImageProvider {
public:
    IconImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
