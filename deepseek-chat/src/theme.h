#pragma once

#include <QString>

class QApplication;

namespace Theme {

void apply(QApplication &app, const QString &mode);
QString normalize(const QString &mode);
bool isDark(const QString &mode);
QString styleSheet(const QString &mode);

}
