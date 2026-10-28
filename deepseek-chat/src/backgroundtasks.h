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
    QString previewUrl;
    int exitCode = -1;
    int revision = 0;
    bool stopped = false;
    QProcess *process = nullptr;
    // Windows Job Object that owns the whole process tree (unused elsewhere).
    void *job = nullptr;
    BoundedOutput output{200 * 1024, 100 * 1024};
};
