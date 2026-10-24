#pragma once
#include "boundedoutput.h"
#include <QString>
class QProcess;
struct BackgroundTask {
    int id = 0;
    qint64 projectId = 0;
    qint64 conversationId = 0;
    qint64 pid = 0;
    QString command;
    QString workdir;
    QString status = QStringLiteral("Iniciando");
    int exitCode = -1;
    bool stopped = false;
    QProcess *process = nullptr;
    BoundedOutput output{200 * 1024, 100 * 1024};
};
