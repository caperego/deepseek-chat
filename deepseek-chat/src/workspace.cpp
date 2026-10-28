#include "appcontroller.h"
#include "apiclient.h"
#include "config.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QStandardPaths>
#include <QUrl>

// Workspace helpers used by the side panel: link routing, browser settings,
// page attachments, permission mode and project shortcuts.

QString AppController::displayPath(const QString &path) const
{
    if (path.isEmpty())
        return {};
    const QString workdir = m_activeProject.workdir;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (!workdir.isEmpty() && insideDir(workdir, absolute))
        return QDir::toNativeSeparators(QDir(workdir).relativeFilePath(absolute));
    return QDir::toNativeSeparators(absolute);
}

bool AppController::openLinksInternally() const
{
    return Config::instance().ui().openLinksInternally;
}

void AppController::setOpenLinksInternally(bool enabled)
{
    auto &config = Config::instance();
    if (config.ui().openLinksInternally == enabled)
        return;
    config.ui().openLinksInternally = enabled;
    config.save();
    emit uiSettingsChanged();
}

bool AppController::browserPersistent() const
{
    return Config::instance().ui().browserPersistent;
}

void AppController::setBrowserPersistent(bool enabled)
{
    auto &config = Config::instance();
    if (config.ui().browserPersistent == enabled)
        return;
    config.ui().browserPersistent = enabled;
    config.save();
    emit uiSettingsChanged();
}

void AppController::openLink(const QString &url)
{
    const QUrl target(url);
    if (!target.isValid())
        return;
    const bool web = target.scheme() == QLatin1String("http")
        || target.scheme() == QLatin1String("https");
    if (web && openLinksInternally()) {
        emit browserRequested(target.toString());
        return;
    }
    openExternal(url);
}

QString AppController::downloadsDirectory() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    return QDir::toNativeSeparators(dir);
}

bool AppController::attachPath(const QString &path)
{
    const QString local = path.startsWith(QLatin1String("file:"))
        ? QUrl(path).toLocalFile() : path;
    return addAttachmentPath(local);
}

void AppController::attachImage(const QImage &image)
{
    attachCapturedPixmap(QPixmap::fromImage(image));
}

void AppController::attachPageText(const QString &title, const QString &url, const QString &text)
{
    const QUrl target(url);
    if (text.trimmed().isEmpty()) {
        emit toast(QStringLiteral("A página não tem texto para anexar."));
        return;
    }
    const int limit = 420 * 1024;
    QString body = QStringLiteral("# %1\n%2\n\n").arg(title.trimmed().isEmpty() ? target.host() : title.trimmed(), url)
        + text.left(limit);
    if (text.size() > limit)
        body += QStringLiteral("\n\n[conteúdo truncado em 420 KB]");

    QString name = target.host() + target.path();
    if (name.isEmpty())
        name = url;
    if (name.size() > 60)
        name = name.left(57) + QStringLiteral("...");

    Attachment attachment;
    attachment.kind = QStringLiteral("file");
    attachment.name = name;
    attachment.path = url;
    attachment.mime = QStringLiteral("text/plain");
    attachment.text = body;
    m_pendingAttachments.append(attachment);
    m_attachments.append(QVariantMap{{"name", attachment.name}, {"image", false},
                                     {"path", attachment.path}, {"url", attachment.path}});
    emit attachmentsChanged();
    m_api->logActivity(QStringLiteral("attachment_add"), url);
    emit toast(QStringLiteral("Página anexada à mensagem: %1").arg(name));
}

void AppController::setPermissionMode(const QString &mode)
{
    if (mode != QLatin1String("ask") && mode != QLatin1String("auto")
        && mode != QLatin1String("full"))
        return;
    if (m_activeProjectId > 0) {
        Project project = m_activeProject;
        project.permissionMode = mode;
        QString error;
        if (!m_api->updateProject(project, &error)) {
            emit toast(QStringLiteral("Erro ao salvar a permissão: ") + error);
            return;
        }
        m_activeProject.permissionMode = mode;
        rebuildProjects();
    } else {
        auto &config = Config::instance();
        config.ui().permissionMode = mode;
        QString error;
        if (!config.save(&error)) {
            emit toast(QStringLiteral("Erro ao salvar a permissão: ") + error);
            return;
        }
    }
    emit activeProjectChanged();
    emit toast(QStringLiteral("Permissões da IA: %1").arg(
        mode == QLatin1String("full") ? QStringLiteral("acesso completo")
        : mode == QLatin1String("auto") ? QStringLiteral("aprovar por mim")
                                        : QStringLiteral("solicitar aprovação")));
}

void AppController::openProjectFolder(qint64 id)
{
    for (const Project &project : std::as_const(m_projectList)) {
        if (project.id != id)
            continue;
        if (project.workdir.isEmpty() || !QFileInfo(project.workdir).isDir()) {
            emit toast(QStringLiteral("Este projeto não tem uma pasta de trabalho válida."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(project.workdir));
        return;
    }
}

void AppController::renameProject(qint64 id, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        emit toast(QStringLiteral("Informe um nome para o projeto."));
        return;
    }
    for (const Project &existing : std::as_const(m_projectList)) {
        if (existing.id != id)
            continue;
        Project project = existing;
        project.name = trimmed;
        QString error;
        if (!m_api->updateProject(project, &error)) {
            emit toast(QStringLiteral("Erro ao renomear: ") + error);
            return;
        }
        if (id == m_activeProjectId) {
            m_activeProject.name = trimmed;
            emit activeProjectChanged();
        }
        rebuildProjects();
        rebuildSidebar();
        return;
    }
}
