#pragma once

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// Local schedules are claimed on disk before dispatch. Missed intervals coalesce
// into one run; a crash never replays an already claimed run automatically.
class AutomationStore {
public:
    QVariantList tasks() const { return m_tasks; }
    QString error() const { return m_error; }

    bool load(const QString &path)
    {
        m_path = path;
        m_tasks.clear();
        m_error.clear();
        if (path.isEmpty() || !QFile::exists(path)) return true;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { m_error = file.errorString(); return false; }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isArray()) {
            m_error = QStringLiteral("Arquivo de automações inválido. O arquivo foi preservado.");
            m_path.clear(); // Do not overwrite a damaged file on the next edit.
            return false;
        }
        m_tasks = doc.array().toVariantList();
        for (QVariant &entry : m_tasks) {
            QVariantMap task = entry.toMap();
            if (task.value("running").toBool()) {
                task["running"] = false;
                task["status"] = QStringLiteral("Interrompida ao fechar o aplicativo");
                entry = task;
            }
        }
        return true;
    }

    bool add(const QString &title, const QString &prompt, int delayMinutes,
             int repeatMinutes, qint64 projectId, const QString &model,
             const QString &effort, qint64 now)
    {
        if (title.trimmed().isEmpty() || prompt.trimmed().isEmpty()
            || delayMinutes < 1 || delayMinutes > 525600
            || repeatMinutes < 0 || repeatMinutes > 525600 || m_tasks.size() >= 100) {
            m_error = QStringLiteral("Informe título, instrução e um intervalo válido (até 100 tarefas).");
            return false;
        }
        QVariantMap task{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                         {"title", title.trimmed().left(120)}, {"prompt", prompt.trimmed().left(20000)},
                         {"projectId", projectId}, {"model", model}, {"effort", effort},
                         {"nextRun", now + qint64(delayMinutes) * 60000},
                         {"repeatMinutes", repeatMinutes}, {"enabled", true},
                         {"running", false}, {"conversationId", 0}, {"status", QStringLiteral("Agendada")}};
        QVariantList candidate = m_tasks;
        candidate.append(task);
        return commit(candidate);
    }

    QVariantList claimDue(qint64 now, int maximum = 3)
    {
        QVariantList candidate = m_tasks, due;
        for (QVariant &entry : candidate) {
            if (due.size() >= maximum) break;
            QVariantMap task = entry.toMap();
            if (!task.value("enabled").toBool() || task.value("running").toBool()
                || task.value("nextRun").toLongLong() > now) continue;
            // Invalid persisted entries must never become executable instructions.
            if (task.value("id").toString().isEmpty() || task.value("prompt").toString().trimmed().isEmpty()) continue;
            task["running"] = true;
            task["status"] = QStringLiteral("Executando");
            const qint64 interval = task.value("repeatMinutes").toLongLong() * 60000;
            if (interval > 0) task["nextRun"] = now + interval;
            else task["enabled"] = false;
            entry = task;
            due.append(task);
        }
        if (due.isEmpty() || !commit(candidate)) return {};
        return due;
    }

    bool complete(const QString &id, const QString &status, qint64 conversationId = 0)
    {
        return update(id, [status, conversationId](QVariantMap &task) {
            task["running"] = false;
            task["status"] = status.left(240);
            task["conversationId"] = conversationId;
        });
    }
    bool setConversation(const QString &id, qint64 conversationId)
    {
        return update(id, [conversationId](QVariantMap &task) { task["conversationId"] = conversationId; });
    }
    bool toggle(const QString &id)
    {
        return update(id, [](QVariantMap &task) {
            task["enabled"] = !task.value("enabled").toBool();
        });
    }
    bool remove(const QString &id)
    {
        QVariantList candidate;
        for (const auto &entry : m_tasks)
            if (entry.toMap().value("id").toString() != id) candidate.append(entry);
        return commit(candidate);
    }
private:
    bool update(const QString &id, const std::function<void(QVariantMap &)> &change)
    {
        QVariantList candidate = m_tasks;
        for (QVariant &entry : candidate) {
            auto task = entry.toMap();
            if (task.value("id").toString() == id) { change(task); entry = task; return commit(candidate); }
        }
        return false;
    }
    bool commit(const QVariantList &tasks)
    {
        if (m_path.isEmpty()) { m_error = QStringLiteral("Entre na sua conta para usar automações."); return false; }
        if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
            m_error = QStringLiteral("Não foi possível criar a pasta de automações."); return false;
        }
        QSaveFile file(m_path);
        if (!file.open(QIODevice::WriteOnly)) { m_error = file.errorString(); return false; }
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        const auto bytes = QJsonDocument(QJsonArray::fromVariantList(tasks)).toJson();
        if (file.write(bytes) != bytes.size() || !file.commit()) { m_error = file.errorString(); return false; }
        m_tasks = tasks;
        m_error.clear();
        return true;
    }
    QVariantList m_tasks;
    QString m_path;
    QString m_error;
};
