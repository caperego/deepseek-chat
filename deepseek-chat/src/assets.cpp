#include "assets.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace {

QPixmap drawnLogo(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient gradient(0, 0, size, size);
    gradient.setColorAt(0.0, QColor(QStringLiteral("#5B7BFF")));
    gradient.setColorAt(1.0, QColor(QStringLiteral("#2F4FE0")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(0, 0, size, size), size * 0.22, size * 0.22);

    painter.setBrush(QColor(QStringLiteral("#FFFFFF")));
    QPainterPath body;
    body.addEllipse(QRectF(size * 0.16, size * 0.38, size * 0.56, size * 0.30));
    painter.drawPath(body);

    QPainterPath tail;
    tail.moveTo(size * 0.68, size * 0.53);
    tail.lineTo(size * 0.87, size * 0.34);
    tail.lineTo(size * 0.87, size * 0.72);
    tail.closeSubpath();
    painter.drawPath(tail);

    QPen pen(QColor(QStringLiteral("#FFFFFF")), size * 0.055, Qt::SolidLine,
             Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(QPointF(size * 0.36, size * 0.40), QPointF(size * 0.32, size * 0.25));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#2F4FE0")));
    painter.drawEllipse(QPointF(size * 0.30, size * 0.51), size * 0.035, size * 0.035);

    painter.end();
    return pixmap;
}

}

namespace Assets {

QPixmap logoPixmap(int size)
{
    QPixmap official(QStringLiteral(":/resources/deepseek-256.png"));
    if (!official.isNull()) {
        return official.scaled(size, size, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    }
    return drawnLogo(size);
}

QIcon appIcon()
{
    QIcon icon;
    const QStringList resourceFiles = {
        QStringLiteral(":/resources/deepseek-32.png"),
        QStringLiteral(":/resources/deepseek-64.png"),
        QStringLiteral(":/resources/deepseek-128.png"),
        QStringLiteral(":/resources/deepseek-256.png"),
        QStringLiteral(":/resources/deepseek-512.png")
    };
    for (const QString &file : resourceFiles) {
        QPixmap pixmap(file);
        if (!pixmap.isNull())
            icon.addPixmap(pixmap);
    }

    if (icon.isNull()) {
        for (int size : {16, 24, 32, 48, 64, 128, 256})
            icon.addPixmap(drawnLogo(size));
    }
    return icon;
}

}
