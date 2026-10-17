#include "appcontroller.h"
#include "apiclient.h"
#include "assets.h"
#include "config.h"
#include "qmltheme.h"
#include "streamclient.h"

#include <QAudioFormat>
#include <QAudioSource>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QCursor>
#include <QDataStream>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMediaDevices>
#include <QMouseEvent>
#include <QMenu>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPdfDocument>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QScopeGuard>
#include <QSharedPointer>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTextToSpeech>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVoice>
#include <QUuid>
#include <QWidget>
#include <QWindow>
#include <QXmlStreamReader>

#include <atomic>
#include <algorithm>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <thread>
#else
#include <pty.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

const char *kAppVersion = "1.1.12";
const int kMaxAgentRounds = 12;
const int kMaxListEntries = 500;
const int kMaxReadBytes = 512 * 1024;
const int kMaxWriteBytes = 2 * 1024 * 1024;
const int kMaxFetchBytes = 400 * 1024;
const int kMaxToolOutput = 100 * 1024;
const qint64 kMaxDownloadBytes = 256LL * 1024 * 1024;

class ScreenRegionPicker : public QWidget {
public:
    ScreenRegionPicker(QScreen *screen, std::function<void(const QPixmap &)> selected)
        : m_capture(screen->grabWindow(0)), m_selected(std::move(selected))
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_DeleteOnClose);
        setGeometry(screen->geometry());
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);
        show();
        raise();
        activateWindow();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawPixmap(rect(), m_capture);
        painter.fillRect(rect(), QColor(0, 0, 0, 85));
        const QRect selected = QRect(m_start, m_end).normalized().intersected(rect());
        if (m_dragging && selected.width() > 0 && selected.height() > 0) {
            const qreal sx = qreal(m_capture.width()) / width();
            const qreal sy = qreal(m_capture.height()) / height();
            const QRect physical(qRound(selected.x() * sx), qRound(selected.y() * sy),
                                 qRound(selected.width() * sx), qRound(selected.height() * sy));
            painter.drawPixmap(selected, m_capture, physical);
            painter.setPen(QPen(Qt::white, 2));
            painter.drawRect(selected);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_start = event->pos();
        m_end = m_start;
        m_dragging = true;
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging) {
            m_end = event->pos();
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_dragging)
            return;
        m_end = event->pos();
        const QRect selected = QRect(m_start, m_end).normalized().intersected(rect());
        if (selected.width() >= 8 && selected.height() >= 8) {
            const qreal sx = qreal(m_capture.width()) / width();
            const qreal sy = qreal(m_capture.height()) / height();
            const QRect physical(qRound(selected.x() * sx), qRound(selected.y() * sy),
                                 qRound(selected.width() * sx), qRound(selected.height() * sy));
            const QPixmap cropped = m_capture.copy(physical);
            close();
            m_selected(cropped);
            return;
        }
        close();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape)
            close();
    }

private:
    QPixmap m_capture;
    std::function<void(const QPixmap &)> m_selected;
    QPoint m_start;
    QPoint m_end;
    bool m_dragging = false;
};

QString formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

QString jsonQuote(const QString &text)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact))
        .mid(1)
        .chopped(1);
}

QByteArray zipEntryData(const QByteArray &data, const QString &wanted)
{
    auto u16 = [&](int off) {
        return static_cast<quint16>(static_cast<uchar>(data[off])
                                    | (static_cast<uchar>(data[off + 1]) << 8));
    };
    auto u32 = [&](int off) {
        return static_cast<quint32>(static_cast<uchar>(data[off])
                                    | (static_cast<uchar>(data[off + 1]) << 8)
                                    | (static_cast<uchar>(data[off + 2]) << 16)
                                    | (static_cast<quint32>(static_cast<uchar>(data[off + 3])) << 24));
    };

    int eocd = -1;
    const int scanStart = qMax(0, data.size() - 65557);
    for (int i = data.size() - 22; i >= scanStart; --i) {
        if (static_cast<uchar>(data[i]) == 0x50 && static_cast<uchar>(data[i + 1]) == 0x4b
            && static_cast<uchar>(data[i + 2]) == 0x05
            && static_cast<uchar>(data[i + 3]) == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return QByteArray();

    const int count = u16(eocd + 10);
    int offset = static_cast<int>(u32(eocd + 16));
    const QByteArray target = wanted.toUtf8();

    for (int n = 0; n < count && offset + 46 <= data.size(); ++n) {
        if (static_cast<uchar>(data[offset]) != 0x50
            || static_cast<uchar>(data[offset + 1]) != 0x4b
            || static_cast<uchar>(data[offset + 2]) != 0x01
            || static_cast<uchar>(data[offset + 3]) != 0x02)
            break;

        const quint16 method = u16(offset + 10);
        const quint32 compressedSize = u32(offset + 20);
        const quint16 nameLength = u16(offset + 28);
        const quint16 extraLength = u16(offset + 30);
        const quint16 commentLength = u16(offset + 32);
        const quint32 localOffset = u32(offset + 42);
        const QByteArray name = data.mid(offset + 46, nameLength);

        if (name == target && localOffset + 30 <= static_cast<quint32>(data.size())) {
            const int lo = static_cast<int>(localOffset);
            const quint16 localNameLength = u16(lo + 26);
            const quint16 localExtraLength = u16(lo + 28);
            const int start = lo + 30 + localNameLength + localExtraLength;
            if (start < 0 || start + static_cast<int>(compressedSize) > data.size())
                return QByteArray();
            const QByteArray payload = data.mid(start, static_cast<int>(compressedSize));
            if (method == 0)
                return payload;
            if (method == 8)
                return qUncompress(QByteArray::fromHex("789c") + payload);
            return QByteArray();
        }

        offset += 46 + nameLength + extraLength + commentLength;
    }
    return QByteArray();
}

QString stripXmlText(const QString &xml)
{
    QString text = xml;
    text.replace(QStringLiteral("</w:p>"), QStringLiteral("\n"));
    text.replace(QStringLiteral("</text:p>"), QStringLiteral("\n"));
    text.replace(QStringLiteral("</text:h>"), QStringLiteral("\n"));
    text.replace(QStringLiteral("<w:br/>"), QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QString());
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    text.replace(QRegularExpression(QStringLiteral("[ \\t]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return text.trimmed();
}

QString extractPdfText(const QString &path)
{
    QPdfDocument document;
    if (document.load(path) != QPdfDocument::Error::None)
        return QString();

    QString text;
    for (int page = 0; page < document.pageCount(); ++page) {
        text += document.getAllText(page).text();
        text += QLatin1Char('\n');
        if (text.size() > 600 * 1024)
            break;
    }
    return text.trimmed();
}

QString backupsRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/backups");
}

QString terminalShellProgram()
{
#ifdef Q_OS_WIN
    return QStringLiteral("cmd.exe");
#else
    const QString shell = qEnvironmentVariable("SHELL");
    return shell.isEmpty() ? QStringLiteral("/bin/bash") : shell;
#endif
}

bool isDangerousCommand(const QString &command)
{
    static const QRegularExpression patterns(
        QStringLiteral("(rm\\s+-[a-z]*r[a-z]*[fn]?\\s+/"
                       "|rm\\s+-[a-z]*f[a-z]*r\\s+/"
                       "|\\bformat\\s+[a-z]:"
                       "|\\bdel\\s+/[a-z]*[sq]"
                       "|\\brd\\s+/s|\\brmdir\\s+/s"
                       "|\\bmkfs\\b|\\bdd\\s+if="
                       "|:\\s*\\(\\s*\\)\\s*\\{"
                       "|\\bshutdown\\b|\\breboot\\b|\\bdiskpart\\b"
                       "|\\breg\\s+delete\\b|\\bvssadmin\\s+delete|\\bbcdedit\\b"
                       "|\\bcipher\\s+/w|\\btakeown\\b|\\bcacls\\b"
                       "|remove-item[^\\n]*-recurse[^\\n]*c:\\\\s*$"
                       "|>\\s*/dev/sd|\\bchmod\\s+-r\\s+777\\s+/)"),
        QRegularExpression::CaseInsensitiveOption);
    return patterns.match(command).hasMatch();
}

#ifdef Q_OS_WIN
typedef void *DsHPCON;
typedef HRESULT(WINAPI *CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, DsHPCON *);
typedef VOID(WINAPI *ClosePseudoConsoleFn)(DsHPCON);

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 22
#endif

class ConPty {
public:
    ConPty(QObject *receiver, std::function<void(const QString &)> onData,
           std::function<void(int)> onExit)
        : m_receiver(receiver),
          m_onData(std::move(onData)),
          m_onExit(std::move(onExit))
    {
    }

    bool start()
    {
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        const auto create = reinterpret_cast<CreatePseudoConsoleFn>(
            reinterpret_cast<void *>(GetProcAddress(kernel, "CreatePseudoConsole")));
        const auto closePty = reinterpret_cast<ClosePseudoConsoleFn>(
            reinterpret_cast<void *>(GetProcAddress(kernel, "ClosePseudoConsole")));
        if (!create || !closePty)
            return false;
        m_closePty = closePty;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE inRead = nullptr;
        HANDLE inWrite = nullptr;
        HANDLE outRead = nullptr;
        HANDLE outWrite = nullptr;
        if (!CreatePipe(&inRead, &inWrite, &sa, 0))
            return false;
        if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
            CloseHandle(inRead);
            CloseHandle(inWrite);
            return false;
        }

        COORD size{120, 30};
        DsHPCON pty = nullptr;
        if (FAILED(create(size, inRead, outWrite, 0, &pty))) {
            CloseHandle(inRead);
            CloseHandle(inWrite);
            CloseHandle(outRead);
            CloseHandle(outWrite);
            return false;
        }
        CloseHandle(inRead);
        CloseHandle(outWrite);

        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            HeapAlloc(GetProcessHeap(), 0, attrSize));
        if (!si.lpAttributeList
            || !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize)
            || !UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                          PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                          pty, sizeof(pty), nullptr, nullptr)) {
            closePty(pty);
            CloseHandle(inWrite);
            CloseHandle(outRead);
            if (si.lpAttributeList)
                HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
            return false;
        }

        wchar_t command[] = L"cmd.exe";
        PROCESS_INFORMATION pi{};
        const BOOL created = CreateProcessW(nullptr, command, nullptr, nullptr, FALSE,
                                            EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                            nullptr, &si.StartupInfo, &pi);
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        if (!created) {
            closePty(pty);
            CloseHandle(inWrite);
            CloseHandle(outRead);
            return false;
        }
        CloseHandle(pi.hThread);

        auto session = std::make_shared<Session>();
        session->pty = pty;
        session->inWrite = inWrite;
        session->outRead = outRead;
        session->process = pi.hProcess;
        m_session = session;

        std::thread([this, session] { readLoop(session); }).detach();
        return true;
    }

    void write(const QByteArray &data)
    {
        auto session = m_session;
        if (!session || !session->running.load())
            return;
        DWORD written = 0;
        WriteFile(session->inWrite, data.constData(),
                  static_cast<DWORD>(data.size()), &written, nullptr);
    }

    void close()
    {
        auto session = m_session;
        m_session.reset();
        if (!session)
            return;
        session->running.store(false);
        if (session->pty) {
            m_closePty(session->pty);
            session->pty = nullptr;
        }
        if (session->process) {
            TerminateProcess(session->process, 0);
            CloseHandle(session->process);
            session->process = nullptr;
        }
        if (session->inWrite) {
            CloseHandle(session->inWrite);
            session->inWrite = nullptr;
        }
    }

private:
    struct Session {
        DsHPCON pty = nullptr;
        HANDLE inWrite = nullptr;
        HANDLE outRead = nullptr;
        HANDLE process = nullptr;
        std::atomic<bool> running{true};
    };

    void readLoop(std::shared_ptr<Session> session)
    {
        char buffer[4096];
        DWORD read = 0;
        while (session->running.load()
               && ReadFile(session->outRead, buffer, sizeof(buffer), &read, nullptr)
               && read > 0) {
            const QByteArray data(buffer, static_cast<int>(read));
            QMetaObject::invokeMethod(m_receiver,
                [this, data] {
                    if (m_onData)
                        m_onData(QString::fromUtf8(data));
                }, Qt::QueuedConnection);
        }
        if (session->outRead) {
            CloseHandle(session->outRead);
            session->outRead = nullptr;
        }
        int code = 0;
        if (session->process) {
            DWORD exitCode = 0;
            GetExitCodeProcess(session->process, &exitCode);
            code = static_cast<int>(exitCode);
        }
        QMetaObject::invokeMethod(m_receiver,
            [this, code] {
                if (m_onExit)
                    m_onExit(code);
            }, Qt::QueuedConnection);
    }

    QObject *m_receiver = nullptr;
    std::function<void(const QString &)> m_onData;
    std::function<void(int)> m_onExit;
    std::shared_ptr<Session> m_session;
    ClosePseudoConsoleFn m_closePty = nullptr;
};
#endif

bool isImageSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    return s == QLatin1String("png") || s == QLatin1String("jpg")
        || s == QLatin1String("jpeg") || s == QLatin1String("webp")
        || s == QLatin1String("gif") || s == QLatin1String("bmp");
}

QString mimeForSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == QLatin1String("png"))
        return QStringLiteral("image/png");
    if (s == QLatin1String("jpg") || s == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    if (s == QLatin1String("webp"))
        return QStringLiteral("image/webp");
    if (s == QLatin1String("gif"))
        return QStringLiteral("image/gif");
    if (s == QLatin1String("bmp"))
        return QStringLiteral("image/bmp");
    return QStringLiteral("application/octet-stream");
}

bool makeAttachment(const QString &path, Attachment *out, QString *error)
{
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (error)
            *error = QStringLiteral("Arquivo não encontrado: %1").arg(path);
        return false;
    }

    out->path = info.absoluteFilePath();
    out->name = info.fileName();

    if (isImageSuffix(info.suffix())) {
        if (info.size() > 10LL * 1024 * 1024) {
            if (error)
                *error = QStringLiteral("Imagem maior que 10 MB: %1").arg(info.fileName());
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Não foi possível ler %1").arg(path);
            return false;
        }
        out->kind = QStringLiteral("image");
        out->mime = mimeForSuffix(info.suffix());
        out->data = file.readAll();
        return true;
    }

    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("pdf") || suffix == QLatin1String("docx")
        || suffix == QLatin1String("odt")) {
        QString extracted;
        if (suffix == QLatin1String("pdf")) {
            extracted = extractPdfText(path);
        } else {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                if (error)
                    *error = QStringLiteral("Não foi possível ler %1").arg(path);
                return false;
            }
            const QByteArray raw = file.read(64 * 1024 * 1024);
            file.close();
            const QString entry = suffix == QLatin1String("docx")
                ? QStringLiteral("word/document.xml")
                : QStringLiteral("content.xml");
            const QByteArray xml = zipEntryData(raw, entry);
            if (!xml.isEmpty())
                extracted = stripXmlText(QString::fromUtf8(xml));
        }
        if (extracted.trimmed().isEmpty()) {
            if (error)
                *error = QStringLiteral("Não foi possível extrair texto de %1")
                             .arg(info.fileName());
            return false;
        }
        out->kind = QStringLiteral("file");
        out->mime = QStringLiteral("text/plain");
        out->text = extracted.left(420 * 1024);
        if (extracted.size() > out->text.size())
            out->text += QStringLiteral("\n\n[conteúdo truncado em 420 KB]");
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Não foi possível ler %1").arg(path);
        return false;
    }
    const QByteArray raw = file.read(420 * 1024);
    if (raw.contains('\0')) {
        if (error)
            *error = QStringLiteral("Arquivo binário não suportado: %1").arg(info.fileName());
        return false;
    }

    out->kind = QStringLiteral("file");
    out->mime = QStringLiteral("text/plain");
    out->text = QString::fromUtf8(raw);
    if (info.size() > raw.size())
        out->text += QStringLiteral("\n\n[conteúdo truncado em %1 KB]").arg(raw.size() / 1024);
    return true;
}

bool writeWavFile(const QString &path, const QByteArray &pcm, int sampleRate,
                  int channels)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    const quint32 dataSize = static_cast<quint32>(pcm.size());
    const quint32 byteRate = static_cast<quint32>(sampleRate * channels * 2);

    file.write("RIFF", 4);
    out << quint32(36 + dataSize);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    out << quint32(16) << quint16(1) << quint16(channels)
        << quint32(sampleRate) << byteRate << quint16(channels * 2)
        << quint16(16);
    file.write("data", 4);
    out << dataSize;
    file.write(pcm);
    file.close();
    return true;
}

const QRegularExpression &fileBlockRegex()
{
    static const QRegularExpression re(
        QStringLiteral("```file:([^\\n`]+)\\n([\\s\\S]*?)```"),
        QRegularExpression::MultilineOption);
    return re;
}

}

MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QString MessageListModel::displayContent(const QString &content)
{
    QString display = content;
    QRegularExpressionMatchIterator it = fileBlockRegex().globalMatch(content);
    int removed = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        display.remove(match.capturedStart() - removed, match.capturedLength());
        removed += match.capturedLength();
    }
    const int partial = display.lastIndexOf(QStringLiteral("```file:"));
    if (partial >= 0 && display.indexOf(QStringLiteral("```"), partial + 3) < 0)
        display.truncate(partial);
    return display.trimmed();
}

QVariantList MessageListModel::extractFiles(const QString &content)
{
    QVariantList files;
    QRegularExpressionMatchIterator it = fileBlockRegex().globalMatch(content);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        QVariantMap file;
        file.insert(QStringLiteral("path"), match.captured(1).trimmed());
        QString body = match.captured(2);
        if (body.endsWith(QLatin1Char('\n')))
            body.chop(1);
        file.insert(QStringLiteral("content"), body);
        files.append(file);
    }
    return files;
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const Item &item = m_items.at(index.row());
    switch (role) {
    case RoleRole:
        return item.role;
    case ContentRole:
        return item.content;
    case DisplayContentRole:
        return displayContent(item.content);
    case ReasoningRole:
        return item.reasoning;
    case AttachmentNamesRole:
        return item.attachments;
    case FilesRole:
        return extractFiles(item.content);
    case ErrorRole:
        return item.error;
    case StreamingRole:
        return item.streaming;
    case MessageIdRole:
        return item.id;
    case DetailRole:
        return item.detail;
    case SourcesRole:
        return item.sources;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {
        {RoleRole, "msgRole"},
        {ContentRole, "content"},
        {DisplayContentRole, "displayContent"},
        {ReasoningRole, "reasoning"},
        {AttachmentNamesRole, "attachmentNames"},
        {FilesRole, "files"},
        {ErrorRole, "errorText"},
        {StreamingRole, "streaming"},
        {MessageIdRole, "messageId"},
        {DetailRole, "detail"},
        {SourcesRole, "sources"}
    };
}

void MessageListModel::setMessages(const QVector<Message> &messages)
{
    beginResetModel();
    m_items.clear();
    for (const Message &message : messages) {
        Item item;
        item.id = message.id;
        item.role = message.role;
        item.content = message.content;
        item.reasoning = message.reasoning;
        item.sources = message.sources;
        for (const Attachment &a : message.attachments)
            item.attachments.append(a.isImage() ? QStringLiteral("[imagem] ") + a.name
                                                : a.name);
        m_items.append(item);
    }
    endResetModel();
}

void MessageListModel::clearMessages()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}

void MessageListModel::beginAssistant()
{
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    Item item;
    item.role = QStringLiteral("assistant");
    item.streaming = true;
    m_items.append(item);
    endInsertRows();
}

void MessageListModel::appendChunk(const QString &chunk)
{
    if (m_items.isEmpty())
        return;
    const int row = m_items.size() - 1;
    m_items[row].content += chunk;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {ContentRole, DisplayContentRole, FilesRole});
}

void MessageListModel::appendReasoning(const QString &chunk)
{
    if (m_items.isEmpty())
        return;
    const int row = m_items.size() - 1;
    m_items[row].reasoning += chunk;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {ReasoningRole});
}

void MessageListModel::finishAssistant(const QString &fullText,
                                       const QString &fullReasoning)
{
    if (m_items.isEmpty())
        return;
    const int row = m_items.size() - 1;
    m_items[row].content = fullText;
    m_items[row].reasoning = fullReasoning;
    m_items[row].streaming = false;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {ContentRole, DisplayContentRole, FilesRole,
                                ReasoningRole, StreamingRole});
}

void MessageListModel::failAssistant(const QString &error)
{
    if (m_items.isEmpty())
        return;
    const int row = m_items.size() - 1;
    m_items[row].error = error;
    m_items[row].streaming = false;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {ErrorRole, StreamingRole});
}

void MessageListModel::appendTool(const QString &text, const QString &detail)
{
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    Item item;
    item.role = QStringLiteral("tool");
    item.content = text;
    item.detail = detail;
    item.streaming = true;
    m_items.append(item);
    endInsertRows();
}

void MessageListModel::updateLastTool(const QString &text, const QString &detail)
{
    if (m_items.isEmpty() || m_items.last().role != QLatin1String("tool"))
        return;
    const int row = m_items.size() - 1;
    m_items[row].content = text;
    m_items[row].detail = detail;
    m_items[row].streaming = false;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {ContentRole, DisplayContentRole, StreamingRole,
                                DetailRole});
}

void MessageListModel::removeLast()
{
    if (m_items.isEmpty())
        return;
    const int row = m_items.size() - 1;
    beginRemoveRows(QModelIndex(), row, row);
    m_items.removeLast();
    endRemoveRows();
}

void MessageListModel::resumeAssistant(const QString &text, const QString &reasoning)
{
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    Item item;
    item.role = QStringLiteral("assistant");
    item.content = text;
    item.reasoning = reasoning;
    item.streaming = true;
    m_items.append(item);
    endInsertRows();
}

QString MessageListModel::lastRole() const
{
    if (m_items.isEmpty())
        return QString();
    return m_items.last().role;
}

AppController::AppController(QObject *parent)
    : QObject(parent),
      m_messages(new MessageListModel(this))
{
    m_restoringSession = !Config::instance().api().token.isEmpty();
    m_tts = new QTextToSpeech(this);
    configureTts();

    m_api = new ApiClient(this);
    m_api->setBaseUrl(Config::instance().api().baseUrl);
    if (!Config::instance().api().token.isEmpty())
        m_api->setToken(Config::instance().api().token);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(Assets::appIcon(), this);
        m_tray->setToolTip(QStringLiteral("DeepSeek Chat"));

        m_trayMenu = new QMenu();
        QAction *openAction = m_trayMenu->addAction(QStringLiteral("Abrir DeepSeek Chat"));
        QAction *quitAction = m_trayMenu->addAction(QStringLiteral("Sair"));
        connect(openAction, &QAction::triggered, this, [this] { restoreWindow(); });
        connect(quitAction, &QAction::triggered, this, [this] {
            m_forceQuit = true;
            QCoreApplication::quit();
        });
        m_tray->setContextMenu(m_trayMenu);

        connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
            restoreWindow();
            if (m_notifyConversation > 0)
                openConversation(m_notifyConversation);
        });
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger
                        || reason == QSystemTrayIcon::DoubleClick)
                        restoreWindow();
                });
        m_tray->show();
    }

    loadMcpServers();
}

void AppController::setWindow(QWindow *window)
{
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
}

void AppController::restoreWindow()
{
    if (!m_window)
        return;
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

bool AppController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::Close) {
        if (m_forceQuit || !Config::instance().ui().closeToTray)
            return QObject::eventFilter(watched, event);
        auto *closeEvent = static_cast<QCloseEvent *>(event);
        closeEvent->ignore();
        m_window->hide();
        if (m_tray && !m_trayNotified) {
            m_trayNotified = true;
            m_tray->showMessage(
                QStringLiteral("DeepSeek Chat"),
                QStringLiteral("O aplicativo continua em segundo plano. "
                               "Clique no ícone da bandeja para voltar."),
                QSystemTrayIcon::Information, 5000);
        }
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void AppController::updateTrayTooltip()
{
    if (!m_tray)
        return;
    if (m_runs.isEmpty())
        m_tray->setToolTip(QStringLiteral("DeepSeek Chat"));
    else
        m_tray->setToolTip(QStringLiteral("DeepSeek Chat — %1 em processamento")
                               .arg(m_runs.size()));
}

QVariantList AppController::streamingConversationIds() const
{
    QVariantList ids;
    for (const AgentRun *run : m_runs) {
        if (run->conversationId > 0
            && !ids.contains(QVariant::fromValue(run->conversationId)))
            ids.append(QVariant::fromValue(run->conversationId));
    }
    return ids;
}

QVariantList AppController::streamingProjectIds() const
{
    QVariantList ids;
    for (const AgentRun *run : m_runs) {
        if (run->projectId > 0
            && !ids.contains(QVariant::fromValue(run->projectId)))
            ids.append(QVariant::fromValue(run->projectId));
    }
    return ids;
}

bool AppController::streaming() const
{
    return runFor(m_currentConversationId) != nullptr;
}

QString AppController::configPath() const
{
    return Config::instance().filePath();
}

QString AppController::appVersion() const
{
    return QString::fromLatin1(kAppVersion);
}

QString AppController::apiUrl() const
{
    return Config::instance().api().baseUrl;
}

bool AppController::enterToSend() const
{
    return Config::instance().ui().enterToSend;
}

bool AppController::showReasoning() const
{
    return Config::instance().ui().showReasoning;
}

int AppController::fontSize() const
{
    return Config::instance().ui().fontSize;
}

bool AppController::ttsEnabled() const
{
    return Config::instance().audio().ttsEnabled;
}

bool AppController::sttEnabled() const
{
    return Config::instance().audio().sttEnabled;
}

QString AppController::ttsStatus() const
{
    if (m_tts->availableEngines().isEmpty())
        return QStringLiteral("Nenhum backend de voz encontrado "
                              "(instale speech-dispatcher).");
    return QStringLiteral("Backend: %1, %2 voz(es).")
        .arg(m_tts->availableEngines().join(QStringLiteral(", ")))
        .arg(m_tts->availableVoices().size());
}

QVariantMap AppController::conversationMap(const Conversation &c)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), c.id);
    map.insert(QStringLiteral("projectId"), c.projectId);
    map.insert(QStringLiteral("parentId"), c.parentId);
    map.insert(QStringLiteral("title"), c.title);
    map.insert(QStringLiteral("model"), c.model);
    map.insert(QStringLiteral("effort"), c.effort);
    map.insert(QStringLiteral("updatedAt"),
               c.updatedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
    return map;
}

QVariantMap AppController::projectMap(const Project &p)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), p.id);
    map.insert(QStringLiteral("name"), p.name);
    map.insert(QStringLiteral("description"), p.description);
    map.insert(QStringLiteral("workdir"), p.workdir);
    map.insert(QStringLiteral("systemPrompt"), p.systemPrompt);
    map.insert(QStringLiteral("defaultModel"), p.defaultModel);
    map.insert(QStringLiteral("defaultEffort"), p.defaultEffort);
    return map;
}

QVariantMap AppController::itemMap(const ProjectItem &item)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), item.id);
    map.insert(QStringLiteral("kind"), item.kind);
    map.insert(QStringLiteral("name"), item.name);
    map.insert(QStringLiteral("path"), item.path);
    return map;
}

QVariantMap AppController::commandMap(const Command &command)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), command.id);
    map.insert(QStringLiteral("name"), command.name);
    map.insert(QStringLiteral("prompt"), command.prompt);
    return map;
}

void AppController::tryAutoLogin()
{
    const auto finishRestoring = qScopeGuard([this] {
        if (m_restoringSession) {
            m_restoringSession = false;
            emit restoringSessionChanged();
        }
    });
    if (m_user.id > 0)
        return;

    const ApiConfig &api = Config::instance().api();
    if (api.token.isEmpty())
        return;

    m_api->setToken(api.token);
    User user;
    QString error;
    if (m_api->me(&user, &error)) {
        afterLogin(user);
    } else {
        m_api->clearToken();
        Config::instance().api().token.clear();
        Config::instance().save();
        if (error.contains(QLatin1String("expirado"), Qt::CaseInsensitive))
            emit toast(QStringLiteral("Sessão expirada. Entre novamente."));
    }
}

void AppController::afterLogin(const User &user)
{
    m_user = user;

    Config &config = Config::instance();
    config.api().token = m_api->token();
    config.api().userId = user.id;
    config.api().username = user.username;
    config.api().displayName = user.displayName;
    config.save();

    m_api->setBaseUrl(config.api().baseUrl);

    rebuildProjects();
    rebuildSidebar();
    loadGlobalCommands();
    refreshModels();
    refreshUsage();
    refreshBalance();

    emit loggedInChanged();
    emit userChanged();

    newConversation(0);
}

void AppController::login(const QString &username, const QString &password)
{
    QString error;
    User user;
    if (!m_api->login(username, password, &user, &error)) {
        emit loginFailed(error);
        return;
    }
    afterLogin(user);
}

void AppController::registerUser(const QString &username, const QString &displayName,
                                 const QString &password)
{
    QString error;
    if (!m_api->registerUser(username, displayName, password, &error)) {
        emit registerFailed(error);
        return;
    }
    login(username, password);
}

void AppController::logout()
{
    m_api->logout();
    m_api->clearToken();

    Config &config = Config::instance();
    config.api().token.clear();
    config.api().userId = 0;
    config.api().username.clear();
    config.api().displayName.clear();
    config.save();

    m_user = User();
    m_currentConversationId = 0;
    m_history.clear();
    m_messages->clearMessages();
    m_sidebar.clear();
    m_projects.clear();
    m_projectItems.clear();
    m_projectCommands.clear();
    m_projectMemories.clear();
    m_attachments.clear();
    m_workdirFiles.clear();
    m_balanceInfo.clear();
    destroyAllRuns();
    m_activeProject = Project();
    m_activeProjectId = 0;
    m_currentParentId = 0;

    emit loggedInChanged();
    emit sidebarChanged();
    emit projectsChanged();
    emit projectItemsChanged();
    emit projectCommandsChanged();
    emit projectMemoriesChanged();
    emit attachmentsChanged();
    emit activeProjectChanged();
    emit currentConversationChanged();
    emit usageChanged();
    emit workdirFilesChanged();
    emit balanceChanged();
}

void AppController::rebuildProjects()
{
    QVector<Project> projects;
    QString error;
    if (!m_api->projects(&projects, &error)) {
        emit toast(QStringLiteral("Erro ao listar projetos: ") + error);
        return;
    }
    m_projectList = projects;
    m_projects.clear();
    for (const Project &project : m_projectList)
        m_projects.append(projectMap(project));
    emit projectsChanged();
}

void AppController::rebuildSidebar()
{
    m_sidebar.clear();

    QVector<Conversation> conversations;
    QString error;
    if (!m_api->conversations(&conversations, &error)) {
        emit toast(QStringLiteral("Erro ao listar conversas: ") + error);
        return;
    }
    const QStringList pinned = Config::instance().ui().pinnedConversations;
    std::stable_sort(conversations.begin(), conversations.end(), [&pinned](const Conversation &a,
                                                                             const Conversation &b) {
        return pinned.contains(QString::number(a.id)) && !pinned.contains(QString::number(b.id));
    });

    if (!m_projectList.isEmpty()) {
        QVariantMap projectsHeader;
        projectsHeader.insert(QStringLiteral("type"), QStringLiteral("header"));
        projectsHeader.insert(QStringLiteral("title"), QStringLiteral("Projetos"));
        projectsHeader.insert(QStringLiteral("indent"), 0);
        projectsHeader.insert(QStringLiteral("level"), 0);
        m_sidebar.append(projectsHeader);
    }

    for (const Project &project : m_projectList) {
        QVariantMap projectItem;
        projectItem.insert(QStringLiteral("type"), QStringLiteral("project"));
        projectItem.insert(QStringLiteral("id"), project.id);
        projectItem.insert(QStringLiteral("title"), project.name);
        projectItem.insert(QStringLiteral("subtitle"), project.workdir);
        projectItem.insert(QStringLiteral("indent"), 0);
        m_sidebar.append(projectItem);

        for (const Conversation &c : conversations) {
            if (c.projectId != project.id)
                continue;
            QVariantMap convItem = conversationMap(c);
            convItem.insert(QStringLiteral("pinned"), pinned.contains(QString::number(c.id)));
            convItem.insert(QStringLiteral("tags"), Config::instance().ui().conversationTags.value(c.id));
            convItem.insert(QStringLiteral("type"), QStringLiteral("conv"));
            convItem.insert(QStringLiteral("indent"), 1);
            m_sidebar.append(convItem);
        }
    }

    QVariantMap header;
    header.insert(QStringLiteral("type"), QStringLiteral("header"));
    header.insert(QStringLiteral("title"), QStringLiteral("Conversas"));
    header.insert(QStringLiteral("indent"), 0);
    header.insert(QStringLiteral("level"), 0);
    m_sidebar.append(header);

    const QDate today = QDate::currentDate();
    QString currentGroup;
    for (const Conversation &c : conversations) {
        if (c.projectId != 0)
            continue;
        QString group = QStringLiteral("Mais antigos");
        const QDate date = c.updatedAt.date();
        if (date == today)
            group = QStringLiteral("Hoje");
        else if (date.daysTo(today) <= 7)
            group = QStringLiteral("Últimos 7 dias");

        if (group != currentGroup) {
            QVariantMap groupItem;
            groupItem.insert(QStringLiteral("type"), QStringLiteral("header"));
            groupItem.insert(QStringLiteral("title"), group);
            groupItem.insert(QStringLiteral("indent"), 0);
            groupItem.insert(QStringLiteral("level"), 1);
            m_sidebar.append(groupItem);
            currentGroup = group;
        }

        QVariantMap convItem = conversationMap(c);
        convItem.insert(QStringLiteral("pinned"), pinned.contains(QString::number(c.id)));
        convItem.insert(QStringLiteral("tags"), Config::instance().ui().conversationTags.value(c.id));
        convItem.insert(QStringLiteral("type"), QStringLiteral("conv"));
        convItem.insert(QStringLiteral("indent"), 0);
        m_sidebar.append(convItem);
    }

    emit sidebarChanged();
}

void AppController::loadGlobalCommands()
{
    QVector<Command> commands;
    if (!m_api->commands(0, &commands))
        return;
    m_globalCommands.clear();
    for (const Command &command : commands)
        m_globalCommands.append(commandMap(command));
    emit globalCommandsChanged();
}

void AppController::loadProjectDetails(qint64 projectId)
{
    m_projectItems.clear();
    m_projectCommands.clear();
    m_projectMemories.clear();
    if (projectId > 0) {
        QVector<ProjectItem> items;
        QString error;
        if (m_api->projectItems(projectId, &items, &error)) {
            for (const ProjectItem &item : items)
                m_projectItems.append(itemMap(item));
        }
        QVector<Command> commands;
        if (m_api->commands(projectId, &commands)) {
            for (const Command &command : commands)
                m_projectCommands.append(commandMap(command));
        }
        QString memoryError;
        if (!m_api->projectMemories(projectId, &m_projectMemories, &memoryError))
            emit toast(QStringLiteral("Erro ao carregar memórias: ") + memoryError);
    }
    emit projectItemsChanged();
    emit projectCommandsChanged();
    emit projectMemoriesChanged();
}

void AppController::refreshProjectMemories()
{
    if (m_activeProjectId <= 0) {
        m_projectMemories.clear();
        emit projectMemoriesChanged();
        return;
    }
    QVariantList memories;
    QString error;
    if (!m_api->projectMemories(m_activeProjectId, &memories, &error)) {
        emit toast(QStringLiteral("Erro ao carregar memórias: ") + error);
        return;
    }
    m_projectMemories = memories;
    emit projectMemoriesChanged();
}

bool AppController::addProjectMemory(const QString &content)
{
    if (m_activeProjectId <= 0 || content.trimmed().isEmpty())
        return false;
    QString error;
    if (!m_api->addProjectMemory(m_activeProjectId, content.trimmed(), &error)) {
        emit toast(QStringLiteral("Erro ao salvar memória: ") + error);
        return false;
    }
    refreshProjectMemories();
    emit toast(QStringLiteral("Memória salva no projeto."));
    return true;
}

bool AppController::updateProjectMemory(qint64 id, const QString &content)
{
    if (id <= 0 || content.trimmed().isEmpty())
        return false;
    QString error;
    if (!m_api->updateProjectMemory(id, content.trimmed(), &error)) {
        emit toast(QStringLiteral("Erro ao editar memória: ") + error);
        return false;
    }
    refreshProjectMemories();
    return true;
}

void AppController::deleteProjectMemory(qint64 id)
{
    if (id <= 0)
        return;
    QString error;
    if (!m_api->deleteProjectMemory(id, &error)) {
        emit toast(QStringLiteral("Erro ao excluir memória: ") + error);
        return;
    }
    refreshProjectMemories();
}

void AppController::rememberMessage(qint64 messageId)
{
    if (m_activeProjectId <= 0) {
        emit toast(QStringLiteral("Abra uma conversa de projeto para salvar uma memória."));
        return;
    }
    for (const Message &message : m_history) {
        if (message.id == messageId) {
            addProjectMemory(message.content.trimmed().left(2000));
            return;
        }
    }
}

void AppController::setActiveProject(qint64 id)
{
    m_activeProjectId = id;
    m_activeProject = Project();
    for (const Project &project : m_projectList) {
        if (project.id == id) {
            m_activeProject = project;
            break;
        }
    }
    loadProjectDetails(id);
    refreshWorkdirFiles();
    emit activeProjectChanged();
}

void AppController::fillModels(const QVector<ModelInfo> &models)
{
    m_models = models;
    m_modelsList.clear();
    for (const ModelInfo &info : models) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), info.id);
        map.insert(QStringLiteral("name"), info.name);
        map.insert(QStringLiteral("supportsImages"), info.supportsImages);
        map.insert(QStringLiteral("contextWindow"), info.contextWindow);
        map.insert(QStringLiteral("maxOutput"), info.maxOutputTokens);
        QStringList efforts = info.efforts;
        if (efforts.isEmpty())
            efforts = {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")};
        map.insert(QStringLiteral("efforts"), efforts);
        map.insert(QStringLiteral("defaultEffort"), info.defaultEffort);
        m_modelsList.append(map);
    }
    emit modelsChanged();
    updateEfforts();
}

void AppController::refreshModels()
{
    QVector<ModelInfo> models;
    QString error;
    if (!m_api->models(&models, &error) || models.isEmpty()) {
        const QStringList fallbackIds = {QStringLiteral("deepseek-flash"),
                                         QStringLiteral("deepseek-v4-pro")};
        QVector<ModelInfo> fallback;
        for (const QString &id : fallbackIds) {
            ModelInfo info;
            info.id = id;
            info.name = id;
            info.supportsImages = id == QLatin1String("deepseek-flash");
            info.efforts = {QStringLiteral("low"), QStringLiteral("high"),
                            QStringLiteral("max")};
            info.defaultEffort = QStringLiteral("high");
            fallback.append(info);
        }
        fillModels(fallback);
        return;
    }
    fillModels(models);
}

void AppController::updateEfforts()
{
    QStringList efforts;
    for (const ModelInfo &info : m_models) {
        if (info.id == m_currentModel) {
            efforts = info.efforts;
            break;
        }
    }
    if (efforts.isEmpty()) {
        for (const QVariant &value : m_modelsList) {
            const QVariantMap map = value.toMap();
            if (map.value(QStringLiteral("id")).toString() == m_currentModel) {
                efforts = map.value(QStringLiteral("efforts")).toStringList();
                break;
            }
        }
    }
    if (efforts.isEmpty())
        efforts = {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")};

    if (efforts != m_efforts) {
        m_efforts = efforts;
        emit effortsChanged();
    }
}

void AppController::setCurrentModel(const QString &model)
{
    if (m_currentModel == model)
        return;
    m_currentModel = model;
    if (m_currentConversationId > 0)
        m_api->updateConversation(m_currentConversationId, QString(), model, QString());
    updateEfforts();
    emit currentConversationChanged();
}

void AppController::setCurrentEffort(const QString &effort)
{
    if (m_currentEffort == effort)
        return;
    m_currentEffort = effort;
    if (m_currentConversationId > 0)
        m_api->updateConversation(m_currentConversationId, QString(), QString(), effort);
    emit currentConversationChanged();
}

void AppController::newConversation(qint64 projectId)
{
    m_currentConversationId = 0;
    m_currentParentId = 0;
    m_history.clear();
    m_messages->clearMessages();
    clearAttachments();
    m_currentTitle = QStringLiteral("Nova conversa");
    setActiveProject(projectId);

    if (projectId > 0 && !m_activeProject.defaultModel.isEmpty()) {
        m_currentModel = m_activeProject.defaultModel;
        m_currentEffort = m_activeProject.defaultEffort.isEmpty()
            ? QStringLiteral("high") : m_activeProject.defaultEffort;
    } else {
        m_currentModel = Config::instance().deepseek().defaultModel;
        m_currentEffort = QStringLiteral("high");
    }
    updateEfforts();

    m_viewMode = 0;
    emit viewModeChanged();
    emit currentConversationChanged();
    refreshUsage();
}

void AppController::openConversationInternal(qint64 id)
{
    m_currentConversationId = id;
    m_currentParentId = 0;
    clearAttachments();

    QVector<Message> stored;
    QString error;
    if (m_api->messages(id, &stored, &error)) {
        m_history = stored;
        m_messages->setMessages(stored);
    } else {
        emit toast(QStringLiteral("Erro ao carregar mensagens: ") + error);
    }

    AgentRun *run = runFor(id);
    if (run && run->stream && run->stream->isStreaming())
        m_messages->resumeAssistant(run->stream->fullText(),
                                    run->stream->fullReasoning());

    QVector<Conversation> conversations;
    if (m_api->conversations(&conversations)) {
        for (const Conversation &c : conversations) {
            if (c.id != id)
                continue;
            m_currentTitle = c.title;
            m_currentParentId = c.parentId;
            m_currentModel = c.model;
            m_currentEffort = c.effort;
            setActiveProject(c.projectId);
            break;
        }
    }
    updateEfforts();

    m_viewMode = 0;
    emit viewModeChanged();
    emit currentConversationChanged();
    refreshUsage();
    maybeQueueNext(id);
}

void AppController::openConversation(qint64 id)
{
    openConversationInternal(id);
}

void AppController::openProject(qint64 id)
{
    setActiveProject(id);
    m_currentConversationId = 0;
    m_currentParentId = 0;
    m_currentTitle = m_activeProject.name;
    m_viewMode = 1;
    emit viewModeChanged();
    emit currentConversationChanged();
}

void AppController::showChatView()
{
    m_viewMode = 0;
    emit viewModeChanged();
}

void AppController::createProject(const QVariantMap &data)
{
    Project project;
    project.name = data.value(QStringLiteral("name")).toString().trimmed();
    project.description = data.value(QStringLiteral("description")).toString();
    project.workdir = data.value(QStringLiteral("workdir")).toString();
    project.systemPrompt = data.value(QStringLiteral("systemPrompt")).toString();
    project.defaultModel = data.value(QStringLiteral("defaultModel")).toString();
    project.defaultEffort = data.value(QStringLiteral("defaultEffort")).toString();
    project.permissionMode = data.value(QStringLiteral("permissionMode")).toString();
    if (project.defaultModel.isEmpty())
        project.defaultModel = QStringLiteral("deepseek-flash");
    if (project.defaultEffort.isEmpty())
        project.defaultEffort = QStringLiteral("high");
    if (project.permissionMode.isEmpty())
        project.permissionMode = Config::instance().ui().permissionMode;

    if (project.name.isEmpty()) {
        emit toast(QStringLiteral("Informe um nome para o projeto."));
        return;
    }

    qint64 id = 0;
    QString error;
    if (!m_api->createProject(project, &id, &error)) {
        emit toast(QStringLiteral("Erro ao criar projeto: ") + error);
        return;
    }

    rebuildProjects();
    rebuildSidebar();
    openProject(id);
    emit toast(QStringLiteral("Projeto criado."));
}

void AppController::saveActiveProject(const QVariantMap &data)
{
    if (m_activeProjectId <= 0)
        return;

    const QString previousWorkdir = m_activeProject.workdir;

    m_activeProject.name = data.value(QStringLiteral("name")).toString().trimmed();
    m_activeProject.description = data.value(QStringLiteral("description")).toString();
    m_activeProject.workdir = data.value(QStringLiteral("workdir")).toString();
    m_activeProject.systemPrompt = data.value(QStringLiteral("systemPrompt")).toString();
    m_activeProject.defaultModel = data.value(QStringLiteral("defaultModel")).toString();
    m_activeProject.defaultEffort = data.value(QStringLiteral("defaultEffort")).toString();
    m_activeProject.permissionMode = data.value(QStringLiteral("permissionMode"),
                                                m_activeProject.permissionMode).toString();

    if (m_activeProject.name.isEmpty()) {
        emit toast(QStringLiteral("Informe um nome para o projeto."));
        return;
    }

    QString error;
    if (!m_api->updateProject(m_activeProject, &error)) {
        emit toast(QStringLiteral("Erro ao salvar: ") + error);
        return;
    }

    rebuildProjects();
    rebuildSidebar();
    if (previousWorkdir != m_activeProject.workdir)
        refreshWorkdirFiles();
    emit activeProjectChanged();
    emit toast(QStringLiteral("Projeto salvo."));
}

void AppController::deleteActiveProject()
{
    if (m_activeProjectId > 0)
        deleteProject(m_activeProjectId);
}

void AppController::deleteProject(qint64 id)
{
    if (id <= 0)
        return;

    QString error;
    if (!m_api->deleteProject(id, &error)) {
        emit toast(QStringLiteral("Erro ao excluir: ") + error);
        return;
    }

    const bool wasActive = id == m_activeProjectId;
    if (wasActive) {
        m_activeProjectId = 0;
        m_activeProject = Project();
        m_workdirFiles.clear();
        emit workdirFilesChanged();
        loadProjectDetails(0);
    }

    rebuildProjects();
    rebuildSidebar();

    if (wasActive)
        newConversation(0);

    emit toast(QStringLiteral("Projeto excluído."));
}

void AppController::renameConversation(qint64 id, const QString &title)
{
    if (title.trimmed().isEmpty())
        return;
    QString error;
    if (!m_api->updateConversation(id, title.trimmed(), QString(), QString(), &error)) {
        emit toast(error);
        return;
    }
    if (id == m_currentConversationId)
        m_currentTitle = title.trimmed();
    rebuildSidebar();
    emit currentConversationChanged();
}

void AppController::deleteConversation(qint64 id)
{
    QString error;
    if (!m_api->deleteConversation(id, &error)) {
        emit toast(error);
        return;
    }
    if (id == m_currentConversationId)
        newConversation(m_activeProjectId);
    rebuildSidebar();
}

bool AppController::isConversationPinned(qint64 id) const
{
    return Config::instance().ui().pinnedConversations.contains(QString::number(id));
}

void AppController::toggleConversationPinned(qint64 id)
{
    if (id <= 0)
        return;
    QStringList &pinned = Config::instance().ui().pinnedConversations;
    const QString key = QString::number(id);
    if (pinned.contains(key))
        pinned.removeAll(key);
    else
        pinned.prepend(key);
    QString error;
    if (!Config::instance().save(&error)) {
        emit toast(error);
        return;
    }
    rebuildSidebar();
}

QString AppController::conversationTags(qint64 id) const
{
    return Config::instance().ui().conversationTags.value(id).join(QStringLiteral(", "));
}

void AppController::setConversationTags(qint64 id, const QString &tags)
{
    if (id <= 0)
        return;
    QStringList values;
    for (const QString &tag : tags.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString clean = tag.trimmed().left(32);
        if (!clean.isEmpty() && !values.contains(clean, Qt::CaseInsensitive) && values.size() < 8)
            values.append(clean);
    }
    auto &stored = Config::instance().ui().conversationTags;
    if (values.isEmpty()) stored.remove(id); else stored.insert(id, values);
    QString error;
    if (!Config::instance().save(&error)) { emit toast(error); return; }
    rebuildSidebar();
}

void AppController::branchConversation(qint64 id)
{
    if (id <= 0 || runFor(id))
        return;
    qint64 branchId = 0;
    QString error;
    if (!m_api->branchConversation(id, 0, QString(), &branchId, &error)) {
        emit toast(QStringLiteral("Erro ao ramificar: ") + error);
        return;
    }
    if (m_compaction.contains(id))
        m_compaction.insert(branchId, m_compaction.value(id));
    rebuildSidebar();
    openConversationInternal(branchId);
    emit toast(QStringLiteral("Ramificação criada. A conversa original foi preservada."));
}

void AppController::branchFromMessage(qint64 messageId)
{
    if (m_currentConversationId <= 0 || messageId <= 0
        || runFor(m_currentConversationId))
        return;
    qint64 branchId = 0;
    QString error;
    if (!m_api->branchConversation(m_currentConversationId, messageId,
                                   QString(), &branchId, &error)) {
        emit toast(QStringLiteral("Erro ao ramificar: ") + error);
        return;
    }
    rebuildSidebar();
    openConversationInternal(branchId);
    emit toast(QStringLiteral("Ramificação criada a partir da mensagem."));
}

void AppController::convertConversationToProject(qint64 id)
{
    if (id <= 0)
        return;

    QVector<Conversation> conversations;
    QString error;
    if (!m_api->conversations(&conversations, &error)) {
        emit toast(QStringLiteral("Erro ao carregar conversas: ") + error);
        return;
    }

    Conversation target;
    bool found = false;
    for (const Conversation &c : conversations) {
        if (c.id == id) {
            target = c;
            found = true;
            break;
        }
    }
    if (!found) {
        emit toast(QStringLiteral("Conversa não encontrada."));
        return;
    }
    if (target.projectId > 0) {
        emit toast(QStringLiteral("Esta conversa já está em um projeto."));
        return;
    }

    const QString docs = QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation);
    if (docs.isEmpty()) {
        emit toast(QStringLiteral("Não foi possível localizar a pasta Documentos."));
        return;
    }

    const QString baseName = folderNameForTitle(target.title);
    QString workdir = QDir(docs).absoluteFilePath(baseName);
    int counter = 2;
    while (QFileInfo::exists(workdir)) {
        workdir = QDir(docs).absoluteFilePath(
            QStringLiteral("%1 (%2)").arg(baseName).arg(counter++));
    }
    if (!QDir().mkpath(workdir)) {
        emit toast(QStringLiteral("Não foi possível criar a pasta: %1").arg(workdir));
        return;
    }

    Project project;
    project.name = target.title.trimmed().isEmpty() ? QStringLiteral("Projeto")
                                                    : target.title.trimmed();
    project.workdir = QDir::toNativeSeparators(workdir);
    project.defaultModel = target.model;
    project.defaultEffort = target.effort;
    project.permissionMode = Config::instance().ui().permissionMode;

    qint64 projectId = 0;
    if (!m_api->createProject(project, &projectId, &error)) {
        emit toast(QStringLiteral("Erro ao criar projeto: ") + error);
        return;
    }
    if (!m_api->moveConversation(id, projectId, &error)) {
        emit toast(QStringLiteral("Projeto criado, mas não foi possível mover a "
                                  "conversa: ") + error);
    }

    rebuildProjects();
    rebuildSidebar();
    if (id == m_currentConversationId)
        openConversationInternal(id);
    else
        emit currentConversationChanged();

    emit toast(QStringLiteral("Projeto \"%1\" criado em %2")
                   .arg(project.name, project.workdir));
}

QString AppController::folderNameForTitle(const QString &title)
{
    QString name = title.simplified();
    static const QRegularExpression invalid(
        QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]"));
    name.remove(invalid);
    name.remove(QRegularExpression(QStringLiteral("[. ]+$")));
    if (name.isEmpty())
        name = QStringLiteral("Projeto");
    if (name.size() > 80)
        name = name.left(80).trimmed();
    return name;
}

bool AppController::missingWorkdir(const AgentRun *run, const QString &path) const
{
    return run->workdir.isEmpty() && !QFileInfo(path).isAbsolute();
}

AgentRun *AppController::runFor(qint64 conversationId) const
{
    for (AgentRun *run : m_runs) {
        if (run->conversationId == conversationId)
            return run;
    }
    return nullptr;
}

bool AppController::displayingRun(const AgentRun *run) const
{
    return run && run->conversationId == m_currentConversationId;
}

void AppController::destroyRun(AgentRun *run)
{
    if (!run)
        return;

    removeApprovalsFor(run);

    if (run->process) {
        QProcess *process = run->process;
        run->process = nullptr;
        QObject::disconnect(process, nullptr, this, nullptr);
        process->kill();
        process->deleteLater();
    }
    if (run->stream) {
        StreamClient *stream = run->stream;
        run->stream = nullptr;
        QObject::disconnect(stream, nullptr, this, nullptr);
        stream->cancel();
        stream->deleteLater();
    }

    m_runs.removeAll(run);
    delete run;
    emit streamingChanged();
    updateTrayTooltip();
}

void AppController::destroyAllRuns()
{
    const QVector<AgentRun *> runs = m_runs;
    for (AgentRun *run : runs)
        destroyRun(run);
}

void AppController::send(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    if (runFor(m_currentConversationId)) {
        m_queuedText.insert(m_currentConversationId, trimmed);
        emit toast(QStringLiteral("Mensagem na fila — será enviada quando esta "
                                  "conversa terminar."));
        return;
    }

    QString title = trimmed.simplified();
    if (title.size() > 60)
        title = title.left(57) + QStringLiteral("...");

    Message userMessage;
    userMessage.role = QStringLiteral("user");
    userMessage.content = trimmed;
    userMessage.attachments = m_pendingAttachments;
    m_history.append(userMessage);

    m_messages->setMessages(m_history);
    m_messages->beginAssistant();
    clearAttachments();

    startRun(title, true);
}

void AppController::startRun(const QString &title, bool persistUser)
{
    m_lastPromptTokens = 0;
    m_lastCompletionTokens = 0;
    m_lastCachedTokens = 0;

    auto *run = new AgentRun;
    run->conversationId = m_currentConversationId;
    run->projectId = m_activeProjectId;
    run->title = title;
    run->model = m_currentModel;
    run->effort = m_currentEffort;
    run->workdir = m_activeProject.workdir;
    run->permissionMode = permissionMode();
    run->persistUser = persistUser;
    run->generateTitle = m_history.size() <= 1;

    for (const QVariant &value : m_modelsList) {
        const QVariantMap map = value.toMap();
        if (map.value(QStringLiteral("id")).toString() == m_currentModel) {
            run->allowImages = map.value(QStringLiteral("supportsImages")).toBool();
            break;
        }
    }
    run->toolsEnabled = !m_currentModel.contains(QLatin1String("reasoner"),
                                                 Qt::CaseInsensitive);

    QString systemPrompt = Config::instance().ui().globalInstructions.trimmed();
    const QString projectPrompt = m_activeProject.systemPrompt.trimmed();
    if (!projectPrompt.isEmpty()) {
        systemPrompt = systemPrompt.isEmpty()
            ? projectPrompt
            : systemPrompt + QStringLiteral("\n\n") + projectPrompt;
    }
    if (run->projectId > 0 && !m_projectMemories.isEmpty()) {
        QStringList memories;
        int totalLength = 0;
        for (const QVariant &value : m_projectMemories) {
            const QString note = value.toMap().value(QStringLiteral("content"))
                                     .toString().trimmed();
            if (!note.isEmpty()) {
                const QString included = note.left(qMin(2000, 6000 - totalLength));
                memories.append(QStringLiteral("- ") + included);
                totalLength += included.size();
            }
            if (memories.size() >= 20 || totalLength >= 6000)
                break;
        }
        if (!memories.isEmpty())
            systemPrompt += QStringLiteral("\n\nMemórias salvas deste projeto:\n")
                            + memories.join(QLatin1Char('\n'));
    }
    if (run->toolsEnabled) {
        const QString context = agentContextText();
        if (!context.isEmpty()) {
            systemPrompt = systemPrompt.isEmpty()
                ? context
                : systemPrompt + QStringLiteral("\n\n") + context;
        }
    }
    if (!systemPrompt.isEmpty()) {
        Message systemMessage;
        systemMessage.role = QStringLiteral("system");
        systemMessage.content = systemPrompt;
        run->messages.append(systemMessage);
    }

    QVector<Message> history = m_history;
    const QString summary = m_compaction.value(run->conversationId);
    if (!summary.isEmpty() && history.size() > 12) {
        Message compact;
        compact.role = QStringLiteral("system");
        compact.content = QStringLiteral("Resumo do histórico anterior desta conversa:\n")
                          + summary;
        run->messages.append(compact);
        history = history.mid(history.size() - 12);
    }
    run->messages += history;

    run->stream = new StreamClient(this);
    run->stream->setBaseUrl(Config::instance().api().baseUrl);
    run->stream->setToken(m_api->token());

    connect(run->stream, &StreamClient::metaReceived, this, [this, run](qint64 id) {
        if (m_currentConversationId == 0) {
            m_currentConversationId = id;
            emit currentConversationChanged();
        }
        if (run->conversationId == 0)
            run->conversationId = id;
        emit streamingChanged();
    });
    connect(run->stream, &StreamClient::chunkReceived, this,
            [this, run](const QString &piece) {
                if (displayingRun(run))
                    m_messages->appendChunk(piece);
            });
    connect(run->stream, &StreamClient::reasoningReceived, this,
            [this, run](const QString &piece) {
                if (displayingRun(run))
                    m_messages->appendReasoning(piece);
            });
    connect(run->stream, &StreamClient::usageReceived, this,
            [this](int promptTokens, int completionTokens, int cachedTokens) {
                m_lastPromptTokens = promptTokens;
                m_lastCompletionTokens = completionTokens;
                m_lastCachedTokens = cachedTokens;
            });
    connect(run->stream, &StreamClient::finished, this,
            [this, run](const QString &fullText, const QString &fullReasoning) {
                onRunFinished(run, fullText, fullReasoning);
            });
    connect(run->stream, &StreamClient::failed, this, [this, run](const QString &error) {
        failRun(run, error);
    });

    m_runs.append(run);
    emit streamingChanged();
    updateTrayTooltip();

    sendRound(run);
}

void AppController::sendRound(AgentRun *run)
{
    if (run->aborted) {
        finishRun(run, QString(), QString());
        return;
    }

    ++run->round;
    if (run->round > 1 && displayingRun(run))
        m_messages->beginAssistant();

    QVector<Message> messages = run->messages;
    const bool finalRound = run->round > kMaxAgentRounds;
    if (finalRound) {
        const QString conclusion = QStringLiteral(
            "\n\nO limite de etapas desta execução foi atingido. Agora conclua com uma "
            "resposta final em português, usando apenas os resultados já recebidos. "
            "Informe o que foi concluído, eventuais erros, o que falta e os caminhos "
            "dos arquivos gerados. Não prometa executar outra ação nem chame ferramentas. "
            "Não declare sucesso sem evidência nos resultados.");
        if (!messages.isEmpty() && messages.first().role == QLatin1String("system"))
            messages.first().content += conclusion;
        else {
            Message system;
            system.role = QStringLiteral("system");
            system.content = conclusion;
            messages.prepend(system);
        }
    }
    run->stream->send(run->conversationId, run->projectId, run->title,
                      run->model, run->effort, run->allowImages, messages,
                      run->toolsEnabled && !finalRound ? toolDefinitions() : QJsonArray(),
                      run->persistUser && run->round == 1, true, run->sources);
}

void AppController::onRunFinished(AgentRun *run, const QString &fullText,
                                  const QString &fullReasoning)
{
    const QVector<ToolCall> calls = run->stream->toolCalls();

    if (!run->aborted && !calls.isEmpty() && run->round <= kMaxAgentRounds) {
        Message assistant;
        assistant.role = QStringLiteral("assistant");
        assistant.content = fullText;
        assistant.reasoning = fullReasoning;
        assistant.toolCalls = calls;
        run->messages.append(assistant);

        if (displayingRun(run)) {
            if (fullText.isEmpty() && fullReasoning.isEmpty())
                m_messages->removeLast();
            else
                m_messages->finishAssistant(fullText, fullReasoning);
        }

        handleToolCalls(run, calls);
        return;
    }

    if (!run->aborted && !calls.isEmpty()) {
        failRun(run, QStringLiteral("O limite de etapas foi atingido. As ações pendentes "
                                    "não foram executadas; peça para continuar."));
        return;
    }

    finishRun(run, fullText, fullReasoning);
}

void AppController::failRun(AgentRun *run, const QString &error)
{
    if (displayingRun(run)) {
        if (m_messages->lastRole() != QLatin1String("assistant"))
            m_messages->beginAssistant();
        m_messages->failAssistant(error);
        // Preserve partial output in the context for an explicit continuation.
        const QString text = run->stream->fullText();
        if (!text.isEmpty()) {
            Message partial;
            partial.role = QStringLiteral("assistant");
            partial.content = text;
            partial.reasoning = run->stream->fullReasoning();
            m_history.append(partial);
        }
    }
    destroyRun(run);
    emit toast(QStringLiteral("Falha no chat: ") + error);
}

void AppController::finishRun(AgentRun *run, const QString &fullText,
                              const QString &fullReasoning)
{
    const qint64 streamConversation = run->conversationId;
    const bool displaying = displayingRun(run);
    const QString runTitle = run->title;
    const QString runModel = run->model;
    const QString runEffort = run->effort;
    const bool runGenerateTitle = run->generateTitle;
    const bool aborted = run->aborted;

    if (displaying && m_messages->lastRole() == QLatin1String("assistant")) {
        if (fullText.isEmpty() && fullReasoning.isEmpty())
            m_messages->removeLast();
        else
            m_messages->finishAssistant(fullText, fullReasoning);
    }

    destroyRun(run);

    if (!aborted && displaying && streamConversation > 0
        && m_currentConversationId == streamConversation) {
        QVector<Message> stored;
        if (m_api->messages(m_currentConversationId, &stored)) {
            m_history = stored;
            m_messages->setMessages(stored);
        }
    }

    rebuildSidebar();
    refreshUsage();

    if (!aborted && Config::instance().ui().notifyOnFinish && m_tray) {
        const QString label = runTitle.left(60);
        m_notifyConversation = streamConversation;
        m_tray->showMessage(QStringLiteral("DeepSeek Chat"),
                            QStringLiteral("Resposta pronta: %1").arg(label),
                            QSystemTrayIcon::Information, 6000);
    }

    if (!aborted && displaying && streamConversation > 0) {
        if (runGenerateTitle)
            maybeGenerateTitle(streamConversation, runModel, runEffort);
        const double percent = m_usage.value(QStringLiteral("percent")).toDouble();
        if (percent >= 80.0)
            requestCompactionSummary(streamConversation, runModel, runEffort);
    }

    maybeQueueNext(streamConversation);

    if (Config::instance().audio().ttsEnabled && !fullText.isEmpty()) {
        QString spoken = fullText;
        spoken.remove(QRegularExpression(QStringLiteral("```[\\s\\S]*?```")));
        spoken.remove(QRegularExpression(QStringLiteral("[*_#`>]")));
        spoken = spoken.simplified();
        if (!spoken.isEmpty())
            m_tts->say(spoken);
    }
}

void AppController::handleToolCalls(AgentRun *run, const QVector<ToolCall> &calls)
{
    run->toolQueue = calls;
    run->toolIndex = 0;
    runNextTool(run);
}

void AppController::runNextTool(AgentRun *run)
{
    if (run->aborted) {
        finishRun(run, QString(), QString());
        return;
    }
    if (run->toolIndex >= run->toolQueue.size()) {
        sendRound(run);
        return;
    }

    run->currentTool = run->toolQueue.at(run->toolIndex);
    if (displayingRun(run))
        m_messages->appendTool(QStringLiteral("⚙ %1…").arg(run->currentTool.name),
                               QString());
    executeToolCall(run, run->currentTool);
}

void AppController::executeToolCall(AgentRun *run, const ToolCall &call)
{
    bool outside = false;
    if (toolNeedsApproval(run, call, &outside) && !run->autoApprove) {
        QString diff;
        if (call.name == QLatin1String("write_file")) {
            QString parseError;
            const QJsonObject args = parseArguments(call, &parseError);
            if (parseError.isEmpty()) {
                const QString path = args.value(QStringLiteral("path")).toString();
                const QString resolved = resolvePathFor(run->workdir, path);
                const QFileInfo info(resolved);
                QString before;
                if (info.exists() && info.isFile() && info.size() <= 256 * 1024) {
                    QFile file(resolved);
                    if (file.open(QIODevice::ReadOnly)) {
                        before = QString::fromUtf8(file.readAll());
                        file.close();
                    }
                }
                diff = diffText(before, args.value(QStringLiteral("content")).toString());
            }
        }
        requestApproval(run, toolApprovalTitle(call),
                        toolApprovalDetail(run, call, outside), diff, call.name,
                        [this, run](bool allow) {
                            if (run->aborted) {
                                runNextTool(run);
                                return;
                            }
                            if (!allow) {
                                finishToolCall(
                                    run,
                                    QStringLiteral("{\"error\":\"Ação negada pelo usuário.\"}"),
                                    QStringLiteral("⚙ %1: negado pelo usuário")
                                        .arg(run->currentTool.name));
                                return;
                            }
                            performToolCall(run, run->currentTool);
                        });
        return;
    }
    performToolCall(run, call);
}

void AppController::performToolCall(AgentRun *run, const ToolCall &call)
{
    if (call.name == QLatin1String("fetch_url")) {
        fetchUrl(run, call);
        return;
    }
    if (call.name == QLatin1String("download_file")) {
        downloadFile(run, call);
        return;
    }
    if (call.name == QLatin1String("run_command")) {
        runCommand(run, call);
        return;
    }
    if (call.name == QLatin1String("browse_url")) {
        browseUrl(run, call);
        return;
    }
    if (call.name == QLatin1String("search_web")) {
        searchWeb(run, call);
        return;
    }
    if (call.name.startsWith(QLatin1String("mcp__"))) {
        mcpCallTool(run, call);
        return;
    }

    QString result;
    QString summary;
    if (call.name == QLatin1String("list_files"))
        runListFiles(run, call, &result, &summary);
    else if (call.name == QLatin1String("read_file"))
        runReadFile(run, call, &result, &summary);
    else if (call.name == QLatin1String("write_file"))
        runWriteFile(run, call, &result, &summary);
    else if (call.name == QLatin1String("find_files"))
        runFindFiles(run, call, &result, &summary);
    else if (call.name == QLatin1String("grep_files"))
        runGrepFiles(run, call, &result, &summary);
    else if (call.name == QLatin1String("restore_file"))
        runRestoreFile(run, call, &result, &summary);
    else {
        result = QStringLiteral("{\"error\":%1}")
                     .arg(jsonQuote(QStringLiteral("Ferramenta desconhecida: %1")
                                        .arg(call.name)));
        summary = QStringLiteral("⚙ %1: ferramenta desconhecida").arg(call.name);
    }
    finishToolCall(run, result, summary);
}

void AppController::finishToolCall(AgentRun *run, const QString &result,
                                   const QString &summary)
{
    if (run->currentTool.name == QLatin1String("fetch_url")
        || run->currentTool.name == QLatin1String("browse_url")) {
        const QJsonObject page = QJsonDocument::fromJson(result.toUtf8()).object();
        const QString url = page.value(QStringLiteral("url")).toString();
        const QString content = page.value(QStringLiteral("content")).toString().simplified();
        if (!url.isEmpty() && !content.isEmpty()
            && page.value(QStringLiteral("status")).toInt(200) < 400) {
            bool duplicate = false;
            for (const QJsonValue &value : run->sources)
                duplicate |= value.toObject().value(QStringLiteral("url")).toString() == url;
            if (!duplicate && run->sources.size() < 12) {
                QJsonObject source;
                source.insert(QStringLiteral("url"), url);
                const QString pageTitle = page.value(QStringLiteral("title"))
                                              .toString().trimmed();
                source.insert(QStringLiteral("title"),
                              (pageTitle.isEmpty() ? QUrl(url).host() : pageTitle).left(200));
                source.insert(QStringLiteral("excerpt"), content.left(400));
                run->sources.append(source);
            }
        }
    }
    if (displayingRun(run))
        m_messages->updateLastTool(summary, result.left(20000));

    if (run->currentTool.id.isEmpty())
        run->currentTool.id = QStringLiteral("call_%1").arg(run->toolIndex);

    Message tool;
    tool.role = QStringLiteral("tool");
    tool.toolCallId = run->currentTool.id;
    tool.content = result;
    run->messages.append(tool);

    ++run->toolIndex;
    runNextTool(run);
}

void AppController::requestApproval(AgentRun *run, const QString &title,
                                    const QString &detail, const QString &diff,
                                    const QString &toolName,
                                    std::function<void(bool)> callback)
{
    ApprovalRequest request;
    request.id = ++m_approvalId;
    request.title = title;
    request.detail = detail;
    request.diff = diff;
    request.toolName = toolName;
    request.run = run;
    request.callback = std::move(callback);
    m_approvals.append(request);
    showNextApproval();
}

void AppController::showNextApproval()
{
    if (m_approvalShown || m_approvals.isEmpty())
        return;
    m_approvalShown = true;
    const ApprovalRequest &request = m_approvals.first();
    emit approvalRequested(request.id, request.title, request.detail, request.diff);
}

void AppController::removeApprovalsFor(AgentRun *run)
{
    bool removedShown = false;
    for (int i = m_approvals.size() - 1; i >= 0; --i) {
        if (m_approvals.at(i).run != run)
            continue;
        if (i == 0)
            removedShown = true;
        m_approvals.removeAt(i);
    }
    if (removedShown) {
        m_approvalShown = false;
        emit approvalClosed();
    }
    showNextApproval();
}

void AppController::resolveApproval(bool allow, bool always)
{
    resolveApprovalEx(allow, always, false);
}

void AppController::resolveApprovalEx(bool allow, bool alwaysSession,
                                      bool alwaysProject)
{
    if (m_approvals.isEmpty())
        return;

    ApprovalRequest request = m_approvals.takeFirst();
    m_approvalShown = false;
    if (alwaysSession && request.run)
        request.run->autoApprove = true;
    if (alwaysProject && request.run && !request.toolName.isEmpty())
        Config::instance().allowToolForProject(request.run->projectId,
                                               request.toolName);

    emit approvalClosed();
    if (request.callback)
        request.callback(allow);
    showNextApproval();
}

void AppController::stop()
{
    AgentRun *run = runFor(m_currentConversationId);
    if (!run)
        return;

    run->aborted = true;
    bool waitingApproval = false;
    for (const ApprovalRequest &request : m_approvals)
        waitingApproval |= request.run == run;
    if (run->process)
        run->process->kill();
    if (run->stream)
        run->stream->cancel();

    removeApprovalsFor(run);
    if (waitingApproval) {
        if (displayingRun(run) && m_messages->lastRole() == QLatin1String("tool"))
            m_messages->updateLastTool(QStringLiteral("Ação cancelada pelo usuário."), QString());
        finishRun(run, QString(), QString());
    }
}

QString AppController::diffText(const QString &before, const QString &after)
{
    const QStringList a = before.split(QLatin1Char('\n'));
    const QStringList b = after.split(QLatin1Char('\n'));
    if (a.size() > 400 || b.size() > 400) {
        return QStringLiteral("[alteração grande: %1 -> %2 linhas]")
            .arg(a.size())
            .arg(b.size());
    }

    const int n = a.size();
    const int m = b.size();
    QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j) {
            dp[i][j] = a.at(i) == b.at(j) ? dp[i + 1][j + 1] + 1
                                          : qMax(dp[i + 1][j], dp[i][j + 1]);
        }
    }

    QString out;
    int i = 0;
    int j = 0;
    int shown = 0;
    while (i < n && j < m && shown < 300) {
        if (a.at(i) == b.at(j)) {
            out += QStringLiteral("  ") + a.at(i) + QLatin1Char('\n');
            ++i;
            ++j;
        } else if (dp[i + 1][j] >= dp[i][j + 1]) {
            out += QStringLiteral("- ") + a.at(i) + QLatin1Char('\n');
            ++i;
        } else {
            out += QStringLiteral("+ ") + b.at(j) + QLatin1Char('\n');
            ++j;
        }
        ++shown;
    }
    while (i < n && shown < 300) {
        out += QStringLiteral("- ") + a.at(i++) + QLatin1Char('\n');
        ++shown;
    }
    while (j < m && shown < 300) {
        out += QStringLiteral("+ ") + b.at(j++) + QLatin1Char('\n');
        ++shown;
    }
    if (i < n || j < m)
        out += QStringLiteral("...\n");
    return out.trimmed();
}

void AppController::backupFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return;

    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
    QDir dir(backupsRoot() + QLatin1Char('/') + key);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return;

    const QString stamp = QDateTime::currentDateTime()
                              .toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QFile::copy(path, dir.filePath(stamp + QLatin1Char('_') + info.fileName()));

    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time);
    for (int k = 5; k < files.size(); ++k)
        QFile::remove(files.at(k).absoluteFilePath());
}

void AppController::maybeQueueNext(qint64 conversationId)
{
    if (m_currentConversationId != conversationId || runFor(conversationId))
        return;
    if (!m_queuedText.contains(conversationId))
        return;

    const QString text = m_queuedText.take(conversationId);
    if (!text.isEmpty())
        QTimer::singleShot(0, this, [this, text] { send(text); });
}

void AppController::maybeGenerateTitle(qint64 conversationId, const QString &model,
                                       const QString &effort)
{
    if (conversationId <= 0 || m_titlePending.contains(conversationId))
        return;
    if (m_currentConversationId != conversationId)
        return;
    if (m_currentTitle != QStringLiteral("Nova conversa")
        && !m_currentTitle.startsWith(m_history.isEmpty()
                                          ? QString()
                                          : m_history.first().content.left(20)))
        return;

    QString firstUser;
    for (const Message &message : m_history) {
        if (message.role == QLatin1String("user")) {
            firstUser = message.content;
            break;
        }
    }
    if (firstUser.isEmpty())
        return;

    m_titlePending.insert(conversationId);

    auto *stream = new StreamClient(this);
    stream->setBaseUrl(Config::instance().api().baseUrl);
    stream->setToken(m_api->token());

    connect(stream, &StreamClient::finished, this,
            [this, stream, conversationId](const QString &text, const QString &) {
                QString title = text.simplified();
                title.remove(QRegularExpression(QStringLiteral("^[\"'\u201c\u201d\\s]+")));
                title.remove(QRegularExpression(QStringLiteral("[\"'\u201c\u201d.\\s]+$")));
                if (title.size() > 60)
                    title = title.left(57) + QStringLiteral("...");
                if (!title.isEmpty()
                    && m_api->updateConversation(conversationId, title, QString(),
                                                 QString())) {
                    if (m_currentConversationId == conversationId)
                        m_currentTitle = title;
                    rebuildSidebar();
                    emit currentConversationChanged();
                }
                m_titlePending.remove(conversationId);
                stream->deleteLater();
            });
    connect(stream, &StreamClient::failed, this,
            [this, stream, conversationId](const QString &) {
                m_titlePending.remove(conversationId);
                stream->deleteLater();
            });

    QVector<Message> messages;
    Message system;
    system.role = QStringLiteral("system");
    system.content = QStringLiteral(
        "Gere um título curto (máximo 6 palavras) em português para a conversa. "
        "Responda apenas com o título, sem aspas e sem pontuação final.");
    messages.append(system);
    Message user;
    user.role = QStringLiteral("user");
    user.content = firstUser.left(600);
    messages.append(user);

    stream->send(conversationId, 0, QString(), model, effort, false, messages,
                 QJsonArray(), false, false);
}

void AppController::requestCompactionSummary(qint64 conversationId,
                                             const QString &model,
                                             const QString &effort)
{
    if (conversationId <= 0)
        return;
    if (m_currentConversationId != conversationId)
        return;

    QString transcript;
    for (const Message &message : m_history) {
        if (message.role == QLatin1String("system"))
            continue;
        transcript += (message.role == QLatin1String("user")
                           ? QStringLiteral("Usuário: ")
                           : QStringLiteral("Assistente: "));
        transcript += message.content.left(2000);
        transcript += QLatin1Char('\n');
        if (transcript.size() > 16000)
            break;
    }
    if (transcript.isEmpty())
        return;

    auto *stream = new StreamClient(this);
    stream->setBaseUrl(Config::instance().api().baseUrl);
    stream->setToken(m_api->token());

    connect(stream, &StreamClient::finished, this,
            [this, stream, conversationId](const QString &text, const QString &) {
                if (!text.trimmed().isEmpty())
                    m_compaction.insert(conversationId, text.trimmed());
                stream->deleteLater();
            });
    connect(stream, &StreamClient::failed, this, [stream](const QString &) {
        stream->deleteLater();
    });

    QVector<Message> messages;
    Message system;
    system.role = QStringLiteral("system");
    system.content = QStringLiteral(
        "Resuma a conversa a seguir em até 400 palavras, em português, preservando "
        "decisões, arquivos citados, comandos executados e pendências. Responda "
        "apenas com o resumo.");
    messages.append(system);
    Message user;
    user.role = QStringLiteral("user");
    user.content = transcript;
    messages.append(user);

    stream->send(conversationId, 0, QString(), model, effort, false, messages,
                 QJsonArray(), false, false);
}

void AppController::regenerateLast()
{
    if (m_currentConversationId <= 0 || runFor(m_currentConversationId))
        return;

    int lastUser = -1;
    for (int i = m_history.size() - 1; i >= 0; --i) {
        if (m_history.at(i).role == QLatin1String("user")) {
            lastUser = i;
            break;
        }
    }
    if (lastUser < 0 || lastUser == m_history.size() - 1) {
        emit toast(QStringLiteral("Não há resposta para regenerar."));
        return;
    }

    const qint64 throughId = m_history.at(lastUser).id;
    if (throughId <= 0) {
        emit toast(QStringLiteral("Não foi possível regenerar esta conversa."));
        return;
    }

    qint64 branchId = 0;
    QString error;
    if (!m_api->branchConversation(m_currentConversationId, throughId,
                                   QString(), &branchId, &error)) {
        emit toast(QStringLiteral("Erro ao regenerar: ") + error);
        return;
    }

    rebuildSidebar();
    openConversationInternal(branchId);
    m_messages->beginAssistant();
    startRun(m_currentTitle.isEmpty() ? QStringLiteral("Nova conversa")
                                      : m_currentTitle,
             false);
}

void AppController::editMessage(qint64 messageId, const QString &newText)
{
    const QString trimmed = newText.trimmed();
    if (trimmed.isEmpty() || messageId <= 0 || runFor(m_currentConversationId))
        return;

    int index = -1;
    for (int i = m_history.size() - 1; i >= 0; --i) {
        if (m_history.at(i).id == messageId
            && m_history.at(i).role == QLatin1String("user")) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        emit toast(QStringLiteral("Mensagem não encontrada para editar."));
        return;
    }

    const qint64 throughId = index > 0 ? m_history.at(index - 1).id : -1;
    qint64 branchId = 0;
    QString error;
    if (!m_api->branchConversation(m_currentConversationId, throughId,
                                   QString(), &branchId, &error)) {
        emit toast(QStringLiteral("Erro ao editar: ") + error);
        return;
    }

    rebuildSidebar();
    openConversationInternal(branchId);
    send(trimmed);
}

void AppController::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void AppController::openExternal(const QString &url)
{
    const QUrl target(url);
    if (target.isValid())
        QDesktopServices::openUrl(target);
}

void AppController::attachUrl(const QString &url)
{
    const QUrl target(url.trimmed());
    if (!target.isValid() || (target.scheme() != QLatin1String("http")
                              && target.scheme() != QLatin1String("https"))) {
        emit toast(QStringLiteral("URL inválida."));
        return;
    }

    emit toast(QStringLiteral("Baixando página: %1").arg(target.host()));
    auto *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(target);
    request.setTransferTimeout(60000);
    request.setMaximumRedirectsAllowed(5);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DeepSeekChat/1.0"));

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager, url] {
        const QByteArray data = reply->readAll();
        const auto error = reply->error();
        const QString errorText = reply->errorString();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        reply->deleteLater();
        manager->deleteLater();

        if (error != QNetworkReply::NoError || data.isEmpty()) {
            emit toast(QStringLiteral("Falha ao baixar a página: %1").arg(errorText));
            return;
        }

        const QByteArray limited = data.left(420 * 1024);
        QString text = QString::fromUtf8(limited);
        if (contentType.contains(QLatin1String("html"), Qt::CaseInsensitive))
            text = htmlToText(text);
        if (data.size() > limited.size())
            text += QStringLiteral("\n\n[conteúdo truncado em 420 KB]");

        const QUrl target(url);
        QString name = target.host() + target.path();
        if (name.size() > 60)
            name = name.left(57) + QStringLiteral("...");

        Attachment attachment;
        attachment.kind = QStringLiteral("file");
        attachment.name = name;
        attachment.path = url;
        attachment.mime = QStringLiteral("text/plain");
        attachment.text = text;
        m_pendingAttachments.append(attachment);

        QVariantMap map;
        map.insert(QStringLiteral("name"), attachment.name);
        map.insert(QStringLiteral("image"), false);
        map.insert(QStringLiteral("path"), attachment.path);
        map.insert(QStringLiteral("url"), attachment.path);
        m_attachments.append(map);
        emit attachmentsChanged();

        m_api->logActivity(QStringLiteral("attachment_add"), url);
        emit toast(QStringLiteral("Página anexada: %1 (%2)")
                       .arg(name, formatSize(limited.size())));
    });
}

void AppController::searchMessages(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.size() < 2) {
        clearSearch();
        return;
    }

    QVector<SearchHit> hits;
    QString error;
    if (!m_api->searchMessages(trimmed, &hits, &error)) {
        emit toast(QStringLiteral("Erro na busca: ") + error);
        return;
    }

    m_searchResults.clear();
    for (const SearchHit &hit : hits) {
        QVariantMap map;
        map.insert(QStringLiteral("conversationId"), hit.conversationId);
        map.insert(QStringLiteral("title"), hit.title);
        map.insert(QStringLiteral("role"), hit.role);
        map.insert(QStringLiteral("snippet"), hit.snippet.simplified());
        map.insert(QStringLiteral("date"),
                   hit.createdAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
        m_searchResults.append(map);
    }
    emit searchResultsChanged();
}

void AppController::clearSearch()
{
    if (m_searchResults.isEmpty())
        return;
    m_searchResults.clear();
    emit searchResultsChanged();
}

bool AppController::terminalRunning() const
{
    return m_terminalRunning;
}

void AppController::runTerminalCommand(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
        return;

#ifdef Q_OS_WIN
    auto *pty = static_cast<ConPty *>(m_conpty);
    if (pty && !m_terminalRunning) {
        if (!pty->start()) {
            delete pty;
            m_conpty = nullptr;
        } else {
            m_terminalRunning = true;
            emit terminalChanged();
        }
    }
    if (!pty && !m_conpty) {
        auto *created = new ConPty(this,
            [this](const QString &text) { emit terminalOutput(text); },
            [this](int code) {
                m_terminalRunning = false;
                emit terminalOutput(
                    QStringLiteral("\n[processo terminou: %1]\n").arg(code));
                emit terminalChanged();
            });
        if (created->start()) {
            m_conpty = created;
            m_terminalRunning = true;
            emit terminalChanged();
            emit terminalOutput(QStringLiteral("DeepSeek Chat — terminal (ConPTY, cmd.exe)\n"));
        } else {
            delete created;
            emit terminalOutput(QStringLiteral(
                "[ConPTY indisponível — usando modo simples]\n"));
        }
    }
    if (m_conpty) {
        emit terminalOutput(QStringLiteral("> ") + trimmed + QLatin1Char('\n'));
        static_cast<ConPty *>(m_conpty)->write(trimmed.toUtf8()
                                               + QByteArrayLiteral("\r\n"));
        return;
    }
#endif

#ifndef Q_OS_WIN
    if (!m_terminal && m_ptyMaster < 0) {
        int master = -1;
        int slave = -1;
        if (openpty(&master, &slave, nullptr, nullptr, nullptr) == 0) {
            auto *process = new QProcess(this);
            process->setProcessChannelMode(QProcess::ForwardedChannels);
            const int slaveFd = slave;
            process->setChildProcessModifier([slaveFd]() {
                setsid();
                ioctl(slaveFd, TIOCSCTTY, 0);
                dup2(slaveFd, 0);
                dup2(slaveFd, 1);
                dup2(slaveFd, 2);
                if (slaveFd > 2)
                    close(slaveFd);
            });
            process->start(terminalShellProgram());
            if (process->waitForStarted(5000)) {
                ::close(slave);
                m_ptyMaster = master;
                m_terminal = process;
                auto *notifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
                connect(notifier, &QSocketNotifier::activated, this, [this] {
                    char buffer[4096];
                    const ssize_t n = ::read(m_ptyMaster, buffer, sizeof(buffer));
                    if (n > 0)
                        emit terminalOutput(QString::fromUtf8(buffer,
                                                              static_cast<int>(n)));
                });
                m_ptyNotifier = notifier;
                connect(process, QOverload<int, QProcess::ExitStatus>::of(
                                     &QProcess::finished),
                        this, [this](int code, QProcess::ExitStatus) {
                            terminalOutput(QStringLiteral("\n[processo terminou: %1]\n")
                                               .arg(code));
                            if (m_ptyNotifier) {
                                m_ptyNotifier->setEnabled(false);
                                m_ptyNotifier->deleteLater();
                                m_ptyNotifier = nullptr;
                            }
                            if (m_ptyMaster >= 0) {
                                ::close(m_ptyMaster);
                                m_ptyMaster = -1;
                            }
                            if (m_terminal) {
                                m_terminal->deleteLater();
                                m_terminal = nullptr;
                            }
                            m_terminalRunning = false;
                            emit terminalChanged();
                        });
                m_terminalRunning = true;
                emit terminalChanged();
            } else {
                ::close(master);
                ::close(slave);
                process->deleteLater();
            }
        }
    }
    if (m_ptyMaster >= 0) {
        emit terminalOutput(QStringLiteral("> ") + trimmed + QLatin1Char('\n'));
        const QByteArray payload = trimmed.toUtf8() + QByteArrayLiteral("\n");
        const ssize_t written = ::write(m_ptyMaster, payload.constData(),
                                        static_cast<size_t>(payload.size()));
        Q_UNUSED(written);
        return;
    }
#endif

    if (!m_terminal) {
        m_terminal = new QProcess(this);
        m_terminal->setProcessChannelMode(QProcess::MergedChannels);
        m_terminal->setWorkingDirectory(m_activeProject.workdir.isEmpty()
                                            ? QDir::homePath()
                                            : m_activeProject.workdir);
        connect(m_terminal, &QProcess::readyRead, this, [this] {
            if (m_terminal)
                emit terminalOutput(QString::fromUtf8(m_terminal->readAll()));
        });
        connect(m_terminal, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) {
                    emit terminalOutput(
                        QStringLiteral("\n[processo terminou: %1]\n").arg(code));
                    m_terminal->deleteLater();
                    m_terminal = nullptr;
                    m_terminalRunning = false;
                    emit terminalChanged();
                });
        m_terminal->start(terminalShellProgram());
        if (!m_terminal->waitForStarted(5000)) {
            emit terminalOutput(QStringLiteral("[falha ao iniciar o shell]\n"));
            m_terminal->deleteLater();
            m_terminal = nullptr;
            m_terminalRunning = false;
            emit terminalChanged();
            return;
        }
        m_terminalRunning = true;
        emit terminalChanged();
    }

    emit terminalOutput(QStringLiteral("> ") + trimmed + QLatin1Char('\n'));
    m_terminal->write(trimmed.toUtf8() + QByteArrayLiteral("\r\n"));
}

void AppController::sendTerminalInput(const QString &text)
{
#ifdef Q_OS_WIN
    if (m_conpty) {
        static_cast<ConPty *>(m_conpty)->write(text.toUtf8() + QByteArrayLiteral("\r\n"));
        return;
    }
#else
    if (m_ptyMaster >= 0) {
        const QByteArray payload = text.toUtf8() + QByteArrayLiteral("\n");
        const ssize_t written = ::write(m_ptyMaster, payload.constData(),
                                        static_cast<size_t>(payload.size()));
        Q_UNUSED(written);
        return;
    }
#endif
    if (!m_terminal)
        return;
    m_terminal->write(text.toUtf8() + QByteArrayLiteral("\r\n"));
}

void AppController::killTerminal()
{
#ifdef Q_OS_WIN
    if (m_conpty) {
        static_cast<ConPty *>(m_conpty)->close();
        return;
    }
#endif
    if (!m_terminal)
        return;
    m_terminal->kill();
}

QString AppController::gitStatusText()
{
    const QString dir = m_activeProject.workdir;
    if (dir.isEmpty())
        return QStringLiteral("projeto sem pasta de trabalho");

    QProcess process;
    process.setWorkingDirectory(dir);
    process.start(QStringLiteral("git"),
                  {QStringLiteral("-C"), dir, QStringLiteral("status"),
                   QStringLiteral("--porcelain=v1"), QStringLiteral("-b")});
    if (!process.waitForFinished(5000)) {
        process.kill();
        return QStringLiteral("git não respondeu");
    }
    if (process.exitCode() != 0)
        return QStringLiteral("não é um repositório git");

    const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString branch;
    int changes = 0;
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("## ")))
            branch = line.mid(3).section(QStringLiteral("..."), 0, 0).trimmed();
        else
            ++changes;
    }
    return QStringLiteral("%1 — %2 alteração(ões)")
        .arg(branch.isEmpty() ? QStringLiteral("(sem branch)") : branch)
        .arg(changes);
}

void AppController::exportCurrentConversation()
{
    if (m_history.isEmpty()) {
        emit toast(QStringLiteral("Não há mensagens para exportar."));
        return;
    }

    QString name = folderNameForTitle(m_currentTitle);
    if (name.isEmpty())
        name = QStringLiteral("conversa");
    const QString suggested = QDir::toNativeSeparators(
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(name + QStringLiteral(".md")));
    const QString file = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Exportar conversa"), suggested,
        QStringLiteral("Markdown (*.md)"));
    if (file.isEmpty())
        return;

    QString markdown = QStringLiteral("# %1\n\n").arg(m_currentTitle);
    for (const Message &message : m_history) {
        if (message.role == QLatin1String("system"))
            continue;
        markdown += message.role == QLatin1String("user")
            ? QStringLiteral("## Usuário\n\n")
            : QStringLiteral("## Assistente\n\n");
        if (!message.attachments.isEmpty()) {
            QStringList names;
            for (const Attachment &attachment : message.attachments)
                names.append(attachment.name);
            markdown += QStringLiteral("_Anexos: %1_\n\n").arg(names.join(QStringLiteral(", ")));
        }
        markdown += message.content;
        markdown += QStringLiteral("\n\n");
    }

    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit toast(QStringLiteral("Não foi possível gravar %1").arg(file));
        return;
    }
    out.write(markdown.toUtf8());
    out.close();
    m_api->logActivity(QStringLiteral("conversation_export"), file);
    emit toast(QStringLiteral("Conversa exportada: %1").arg(file));
}

void AppController::checkForUpdates()
{
    m_updateStatus = QStringLiteral("Verificando…");
    m_updateAvailable = false;
    emit updateStatusChanged();

    auto *manager = new QNetworkAccessManager(this);
    const QUrl url(Config::instance().api().baseUrl + QStringLiteral("/version.txt"));
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager] {
        const QByteArray data = reply->readAll().trimmed();
        const auto error = reply->error();
        reply->deleteLater();
        manager->deleteLater();

        if (error != QNetworkReply::NoError || data.isEmpty()) {
            m_updateStatus = QStringLiteral("Não foi possível verificar.");
            m_updateAvailable = false;
            emit updateStatusChanged();
            return;
        }

        const QString version = QString::fromUtf8(data).trimmed();
        if (version == QString::fromLatin1(kAppVersion)) {
            m_updateStatus = QStringLiteral("Você está na versão mais recente (%1).")
                                 .arg(version);
            m_updateAvailable = false;
        } else {
            m_updateStatus = QStringLiteral("Nova versão disponível: %1 (você tem %2).")
                                 .arg(version, QString::fromLatin1(kAppVersion));
            m_updateAvailable = true;
            m_pendingUpdateUrl = Config::instance().api().baseUrl
                + QStringLiteral("/download/deepseek-chat-windows-x64.zip");
        }
        emit updateStatusChanged();
    });
}

void AppController::downloadUpdate()
{
    if (m_pendingUpdateUrl.isEmpty()) {
        emit toast(QStringLiteral("Verifique atualizações primeiro."));
        return;
    }

#ifdef Q_OS_WIN
    const QString updateDir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/deepseek-chat-update");
    QDir(updateDir).removeRecursively();
    QDir().mkpath(updateDir);
    const QString zipPath = updateDir + QStringLiteral("/update.zip");
#else
    const QString zipPath =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
        + QStringLiteral("/deepseek-chat-windows-x64.zip");
#endif

    m_updateStatus = QStringLiteral("Baixando atualização…");
    emit updateStatusChanged();

    auto *manager = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(m_pendingUpdateUrl)};
    request.setTransferTimeout(600000);
    request.setMaximumRedirectsAllowed(5);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DeepSeekChat/1.0"));

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, manager, zipPath] {
                const QByteArray data = reply->readAll();
                const auto error = reply->error();
                const QString errorText = reply->errorString();
                reply->deleteLater();
                manager->deleteLater();

                if (error != QNetworkReply::NoError || data.isEmpty()) {
                    m_updateStatus = QStringLiteral("Falha no download: %1").arg(errorText);
                    emit updateStatusChanged();
                    return;
                }
                QFile out(zipPath);
                if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
                    || out.write(data) != data.size()) {
                    m_updateStatus = QStringLiteral("Falha ao gravar o download.");
                    emit updateStatusChanged();
                    return;
                }
                out.close();

#ifdef Q_OS_WIN
                m_updateStatus = QStringLiteral("Extraindo atualização…");
                emit updateStatusChanged();

                const QString extractDir = QFileInfo(zipPath).absolutePath()
                    + QStringLiteral("/extract");
                QDir(extractDir).removeRecursively();
                QDir().mkpath(extractDir);

                auto *extract = new QProcess(this);
                connect(extract, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                        this, [this, extract, zipPath, extractDir](int code, QProcess::ExitStatus) {
                            extract->deleteLater();
                            const QString exeInExtract =
                                extractDir + QStringLiteral("/deepseek-chat.exe");
                            if (code != 0 || !QFileInfo::exists(exeInExtract)) {
                                const QString downloads =
                                    QStandardPaths::writableLocation(
                                        QStandardPaths::DownloadLocation)
                                    + QStringLiteral("/deepseek-chat-windows-x64.zip");
                                QFile::remove(downloads);
                                QFile::copy(zipPath, downloads);
                                m_updateStatus =
                                    QStringLiteral("Falha ao extrair. O pacote foi salvo em %1 "
                                                   "— extraia manualmente.")
                                        .arg(QDir::toNativeSeparators(downloads));
                                emit updateStatusChanged();
                                return;
                            }
                            launchUpdateScript(zipPath, extractDir);
                        });
                const QString escapedZip = zipPath;
                const QString escapedDir = extractDir;
                extract->start(QStringLiteral("powershell"),
                               {QStringLiteral("-NoProfile"),
                                QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                QStringLiteral("-Command"),
                                QStringLiteral("Expand-Archive -LiteralPath '%1' "
                                               "-DestinationPath '%2' -Force")
                                    .arg(escapedZip, escapedDir)});
#else
                m_updateStatus = QStringLiteral("Atualização baixada em %1 — extraia e "
                                                "substitua o aplicativo.")
                                     .arg(QDir::toNativeSeparators(zipPath));
                m_updateAvailable = false;
                emit updateStatusChanged();
                emit toast(QStringLiteral("Atualização baixada em %1").arg(zipPath));
#endif
            });
}

void AppController::launchUpdateScript(const QString &zipPath, const QString &extractDir)
{
#ifdef Q_OS_WIN
    QString src = extractDir;
    QString dst = QCoreApplication::applicationDirPath();
    QString zip = zipPath;
    src.replace(QLatin1Char('\''), QLatin1String("''"));
    dst.replace(QLatin1Char('\''), QLatin1String("''"));
    zip.replace(QLatin1Char('\''), QLatin1String("''"));

    const QString scriptPath = QFileInfo(zipPath).absolutePath()
        + QStringLiteral("/apply-update.ps1");
    const QString content = QStringLiteral(
        "$ErrorActionPreference = 'SilentlyContinue'\n"
        "$src = '%1'\n"
        "$dst = '%2'\n"
        "$zip = '%3'\n"
        "while (Get-Process -Name 'deepseek-chat' -ErrorAction SilentlyContinue) "
        "{ Start-Sleep -Seconds 1 }\n"
        "Start-Sleep -Seconds 2\n"
        "Copy-Item -Path (Join-Path $src '*') -Destination $dst -Recurse -Force\n"
        "$exe = Join-Path $dst 'deepseek-chat.exe'\n"
        "Start-Process -FilePath $exe -WorkingDirectory $dst\n"
        "Remove-Item -LiteralPath $src -Recurse -Force\n"
        "Remove-Item -LiteralPath $zip -Force\n"
        "Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force\n")
        .arg(src, dst, zip);

    QFile file(scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_updateStatus = QStringLiteral("Não foi possível preparar a atualização.");
        emit updateStatusChanged();
        return;
    }
    file.write(content.toUtf8());
    file.close();

    m_updateStatus = QStringLiteral("Aplicando atualização — o aplicativo vai reiniciar…");
    emit updateStatusChanged();
    emit toast(QStringLiteral("Atualizando e reiniciando…"));

    QProcess::startDetached(QStringLiteral("powershell"),
                            {QStringLiteral("-NoProfile"),
                             QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                             QStringLiteral("-File"), scriptPath});
    m_forceQuit = true;
    QTimer::singleShot(1200, qApp, &QCoreApplication::quit);
#else
    Q_UNUSED(zipPath);
    Q_UNUSED(extractDir);
#endif
}

void AppController::openArtifact(const QString &path)
{
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        emit toast(QStringLiteral("Arquivo não encontrado: %1").arg(path));
        return;
    }
    if (info.size() > 2 * 1024 * 1024) {
        emit toast(QStringLiteral("Arquivo muito grande para o painel."));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit toast(QStringLiteral("Não foi possível ler %1").arg(path));
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();
    if (raw.contains('\0')) {
        emit toast(QStringLiteral("Arquivo binário não suportado no painel."));
        return;
    }
    m_artifactPath = info.absoluteFilePath();
    m_artifactContent = QString::fromUtf8(raw);
    refreshArtifactVersions();
    emit artifactChanged();
    emit toast(QStringLiteral("Aberto no painel: %1").arg(info.fileName()));
}

void AppController::closeArtifact()
{
    m_artifactPath.clear();
    m_artifactContent.clear();
    m_artifactVersions.clear();
    emit artifactChanged();
}

void AppController::saveArtifact(const QString &content)
{
    if (m_artifactPath.isEmpty())
        return;
    backupFile(m_artifactPath);
    QSaveFile file(m_artifactPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit toast(QStringLiteral("Não foi possível gravar %1").arg(m_artifactPath));
        return;
    }
    const QByteArray bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        emit toast(QStringLiteral("Não foi possível concluir a gravação."));
        return;
    }
    m_artifactContent = content;
    refreshArtifactVersions();
    m_api->logActivity(QStringLiteral("file_write"), m_artifactPath);
    emit artifactChanged();
    emit toast(QStringLiteral("Arquivo salvo: %1").arg(m_artifactPath));
}

void AppController::refreshArtifactVersions()
{
    m_artifactVersions.clear();
    if (m_artifactPath.isEmpty())
        return;
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(m_artifactPath.toUtf8(), QCryptographicHash::Sha1).toHex());
    QDir dir(backupsRoot() + QLatin1Char('/') + key);
    for (const QFileInfo &file : dir.entryInfoList(QDir::Files, QDir::Time)) {
        QVariantMap version;
        version.insert(QStringLiteral("path"), file.absoluteFilePath());
        version.insert(QStringLiteral("label"),
                       file.lastModified().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")));
        m_artifactVersions.append(version);
    }
}

void AppController::restoreArtifactVersion(const QString &versionPath)
{
    if (m_artifactPath.isEmpty() || versionPath.isEmpty())
        return;
    const QString clean = QFileInfo(versionPath).absoluteFilePath();
    bool listed = false;
    for (const QVariant &value : m_artifactVersions)
        listed |= value.toMap().value(QStringLiteral("path")).toString() == clean;
    if (!listed) {
        emit toast(QStringLiteral("Versão não encontrada."));
        return;
    }
    QFile previous(clean);
    if (!previous.open(QIODevice::ReadOnly)) {
        emit toast(QStringLiteral("Não foi possível ler a versão."));
        return;
    }
    const QByteArray data = previous.readAll();
    previous.close();
    if (data.size() > kMaxWriteBytes || data.contains('\0')) {
        emit toast(QStringLiteral("Versão incompatível com o painel."));
        return;
    }
    backupFile(m_artifactPath);
    QSaveFile current(m_artifactPath);
    if (!current.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || current.write(data) != data.size()
        || !current.commit()) {
        emit toast(QStringLiteral("Não foi possível restaurar a versão."));
        return;
    }
    m_artifactContent = QString::fromUtf8(data);
    refreshArtifactVersions();
    emit artifactChanged();
    emit toast(QStringLiteral("Versão restaurada."));
}

QString AppController::artifactDiff(const QString &edited) const
{
    if (edited == m_artifactContent)
        return QStringLiteral("Sem alterações desde a última gravação.");
    return diffText(m_artifactContent, edited);
}

QString AppController::artifactPreviewUrl(const QString &edited) const
{
    if (!m_artifactPath.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        return QString();
    return QStringLiteral("data:image/svg+xml;base64,")
           + QString::fromLatin1(edited.toUtf8().toBase64());
}

void AppController::loadMcpServers()
{
    for (McpServer *server : m_mcpServers) {
        if (server->process) {
            QObject::disconnect(server->process, nullptr, this, nullptr);
            server->process->kill();
            server->process->deleteLater();
        }
        delete server;
    }
    m_mcpServers.clear();
    m_mcpTools.clear();
    m_mcpCallRuns.clear();
    m_mcpCallTools.clear();

    const QString configDir = QFileInfo(Config::instance().filePath()).absolutePath();
    const QStringList candidates = {
        configDir + QStringLiteral("/mcp.json"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/mcp.json")
    };
    QString path;
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            path = candidate;
            break;
        }
    }
    m_mcpLoaded = true;
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    const QJsonObject servers = doc.object().value(QStringLiteral("mcpServers")).toObject();
    for (auto it = servers.constBegin(); it != servers.constEnd(); ++it) {
        auto *server = new McpServer;
        server->name = it.key();
        server->config = it.value().toObject();
        m_mcpServers.append(server);
        startMcpServer(server);
    }
}

void AppController::startMcpServer(McpServer *server)
{
    const QString command = server->config.value(QStringLiteral("command")).toString();
    if (command.isEmpty()) {
        emit toast(QStringLiteral("MCP %1: comando não configurado.").arg(server->name));
        return;
    }

    QStringList args;
    for (const QJsonValue &value : server->config.value(QStringLiteral("args")).toArray())
        args.append(value.toString());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QJsonObject envObject = server->config.value(QStringLiteral("env")).toObject();
    for (auto it = envObject.constBegin(); it != envObject.constEnd(); ++it)
        env.insert(it.key(), it.value().toString());

    auto *process = new QProcess(this);
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    const QString cwd = server->config.value(QStringLiteral("cwd")).toString();
    if (!cwd.isEmpty())
        process->setWorkingDirectory(cwd);
    server->process = process;

    connect(process, &QProcess::readyReadStandardOutput, this, [this, server] {
        handleMcpOutput(server);
    });
    connect(process, &QProcess::readyReadStandardError, this, [process] {
        process->readAllStandardError();
    });
    connect(process, &QProcess::started, this, [this, server] {
        QJsonObject params;
        params.insert(QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05"));
        params.insert(QStringLiteral("capabilities"), QJsonObject());
        QJsonObject clientInfo;
        clientInfo.insert(QStringLiteral("name"), QStringLiteral("DeepSeekChat"));
        clientInfo.insert(QStringLiteral("version"), QString::fromLatin1(kAppVersion));
        params.insert(QStringLiteral("clientInfo"), clientInfo);

        QJsonObject message;
        message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        message.insert(QStringLiteral("id"), server->nextId++);
        message.insert(QStringLiteral("method"), QStringLiteral("initialize"));
        message.insert(QStringLiteral("params"), params);
        mcpSend(server, message);
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, server](QProcess::ProcessError processError) {
                if (processError == QProcess::FailedToStart)
                    emit toast(QStringLiteral("MCP %1: falha ao iniciar.").arg(server->name));
            });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, server](int code, QProcess::ExitStatus) {
                server->initialized = false;
                emit toast(QStringLiteral("MCP %1 terminou (código %2).")
                               .arg(server->name)
                               .arg(code));
            });

    process->start(command, args);
}

void AppController::mcpSend(McpServer *server, const QJsonObject &message)
{
    if (!server->process || server->process->state() != QProcess::Running)
        return;
    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');
    server->process->write(payload);
}

QString AppController::mcpPublicName(const QString &server, const QString &tool)
{
    QString safeServer = server;
    safeServer.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")),
                       QStringLiteral("_"));
    QString safeTool = tool;
    safeTool.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")),
                     QStringLiteral("_"));
    return QStringLiteral("mcp__%1__%2").arg(safeServer, safeTool);
}

QString AppController::mcpJsonText(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void AppController::handleMcpOutput(McpServer *server)
{
    if (!server->process)
        return;
    server->buffer.append(server->process->readAllStandardOutput());

    int index = -1;
    while ((index = server->buffer.indexOf('\n')) >= 0) {
        const QByteArray line = server->buffer.left(index).trimmed();
        server->buffer.remove(0, index + 1);
        if (line.isEmpty())
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        const QJsonObject object = doc.object();
        if (!object.contains(QStringLiteral("id")))
            continue;

        const int id = object.value(QStringLiteral("id")).toInt();
        if (object.contains(QStringLiteral("error"))) {
            const QString message = object.value(QStringLiteral("error")).toObject()
                                        .value(QStringLiteral("message")).toString();
            if (m_mcpCallRuns.contains(id)) {
                AgentRun *run = m_mcpCallRuns.take(id);
                const QString toolName = m_mcpCallTools.take(id);
                finishToolCall(run,
                    QStringLiteral("{\"error\":%1}").arg(jsonQuote(
                        QStringLiteral("MCP: %1").arg(message))),
                    QStringLiteral("⚙ %1: erro MCP").arg(toolName));
            }
            continue;
        }

        const QJsonObject result = object.value(QStringLiteral("result")).toObject();

        if (m_mcpCallRuns.contains(id)) {
            AgentRun *run = m_mcpCallRuns.take(id);
            const QString toolName = m_mcpCallTools.take(id);
            QString text;
            for (const QJsonValue &value : result.value(QStringLiteral("content")).toArray()) {
                const QJsonObject part = value.toObject();
                if (part.value(QStringLiteral("type")).toString() == QLatin1String("text"))
                    text += part.value(QStringLiteral("text")).toString() + QLatin1Char('\n');
            }
            for (const QJsonValue &value : result.value(QStringLiteral("contents")).toArray()) {
                const QJsonObject part = value.toObject();
                const QString uri = part.value(QStringLiteral("uri")).toString();
                if (!uri.isEmpty())
                    text += QStringLiteral("### %1\n").arg(uri);
                text += part.value(QStringLiteral("text")).toString() + QLatin1Char('\n');
            }
            for (const QJsonValue &value : result.value(QStringLiteral("messages")).toArray()) {
                const QJsonObject messageObject = value.toObject();
                const QJsonObject contentObject =
                    messageObject.value(QStringLiteral("content")).toObject();
                text += contentObject.value(QStringLiteral("text")).toString()
                        + QLatin1Char('\n');
            }
            if (text.trimmed().isEmpty())
                text = mcpJsonText(result);
            finishToolCall(run,
                QStringLiteral("{\"result\":%1}").arg(jsonQuote(text.left(100000))),
                QStringLiteral("⚙ %1: concluído (MCP)").arg(toolName));
            continue;
        }

        if (result.contains(QStringLiteral("capabilities")) && !server->initialized) {
            server->initialized = true;
            const QJsonObject caps = result.value(QStringLiteral("capabilities")).toObject();
            server->capsResources = caps.contains(QStringLiteral("resources"));
            server->capsPrompts = caps.contains(QStringLiteral("prompts"));
            QJsonObject note;
            note.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
            note.insert(QStringLiteral("method"), QStringLiteral("notifications/initialized"));
            note.insert(QStringLiteral("params"), QJsonObject());
            mcpSend(server, note);

            QJsonObject listMessage;
            listMessage.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
            listMessage.insert(QStringLiteral("id"), server->nextId++);
            listMessage.insert(QStringLiteral("method"), QStringLiteral("tools/list"));
            listMessage.insert(QStringLiteral("params"), QJsonObject());
            mcpSend(server, listMessage);

            if (server->capsResources) {
                QJsonObject resourcesMessage;
                resourcesMessage.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
                resourcesMessage.insert(QStringLiteral("id"), server->nextId++);
                resourcesMessage.insert(QStringLiteral("method"), QStringLiteral("resources/list"));
                resourcesMessage.insert(QStringLiteral("params"), QJsonObject());
                mcpSend(server, resourcesMessage);
            }
            if (server->capsPrompts) {
                QJsonObject promptsMessage;
                promptsMessage.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
                promptsMessage.insert(QStringLiteral("id"), server->nextId++);
                promptsMessage.insert(QStringLiteral("method"), QStringLiteral("prompts/list"));
                promptsMessage.insert(QStringLiteral("params"), QJsonObject());
                mcpSend(server, promptsMessage);
            }
            continue;
        }

        if (result.contains(QStringLiteral("resources"))) {
            server->hasResources = true;
            continue;
        }

        if (result.contains(QStringLiteral("prompts"))) {
            server->hasPrompts = true;
            continue;
        }

        if (result.contains(QStringLiteral("tools"))) {
            for (int i = m_mcpTools.size() - 1; i >= 0; --i) {
                if (m_mcpTools.at(i).server == server->name)
                    m_mcpTools.removeAt(i);
            }
            int count = 0;
            for (const QJsonValue &value : result.value(QStringLiteral("tools")).toArray()) {
                const QJsonObject toolObject = value.toObject();
                McpTool tool;
                tool.server = server->name;
                tool.name = toolObject.value(QStringLiteral("name")).toString();
                tool.description = toolObject.value(QStringLiteral("description")).toString();
                tool.schema = toolObject.value(QStringLiteral("inputSchema")).toObject();
                if (!tool.name.isEmpty()) {
                    m_mcpTools.append(tool);
                    ++count;
                }
            }
            emit toast(QStringLiteral("MCP %1: %2 ferramenta(s) disponível(is).")
                           .arg(server->name)
                           .arg(count));
        }
    }
}

void AppController::mcpCallTool(AgentRun *run, const ToolCall &call)
{
    for (McpServer *server : m_mcpServers) {
        if (!server->process || server->process->state() != QProcess::Running)
            continue;

        if (mcpPublicName(server->name, QStringLiteral("read_resource")) == call.name) {
            QString parseError;
            const QJsonObject args = parseArguments(call, &parseError);
            QJsonObject params;
            params.insert(QStringLiteral("uri"), args.value(QStringLiteral("uri")).toString());
            QJsonObject message;
            message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
            const int id = server->nextId++;
            message.insert(QStringLiteral("id"), id);
            message.insert(QStringLiteral("method"), QStringLiteral("resources/read"));
            message.insert(QStringLiteral("params"), params);
            m_mcpCallRuns.insert(id, run);
            m_mcpCallTools.insert(id, call.name);
            if (displayingRun(run))
                m_messages->updateLastTool(
                    QStringLiteral("⚙ %1: lendo recurso MCP…").arg(call.name), QString());
            mcpSend(server, message);
            return;
        }

        if (mcpPublicName(server->name, QStringLiteral("get_prompt")) == call.name) {
            QString parseError;
            const QJsonObject args = parseArguments(call, &parseError);
            QJsonObject params;
            params.insert(QStringLiteral("name"), args.value(QStringLiteral("name")).toString());
            const QString argumentsText = args.value(QStringLiteral("arguments")).toString();
            if (!argumentsText.isEmpty()) {
                const QJsonDocument argumentsDoc =
                    QJsonDocument::fromJson(argumentsText.toUtf8());
                if (argumentsDoc.isObject())
                    params.insert(QStringLiteral("arguments"), argumentsDoc.object());
            }
            QJsonObject message;
            message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
            const int id = server->nextId++;
            message.insert(QStringLiteral("id"), id);
            message.insert(QStringLiteral("method"), QStringLiteral("prompts/get"));
            message.insert(QStringLiteral("params"), params);
            m_mcpCallRuns.insert(id, run);
            m_mcpCallTools.insert(id, call.name);
            if (displayingRun(run))
                m_messages->updateLastTool(
                    QStringLiteral("⚙ %1: obtendo prompt MCP…").arg(call.name), QString());
            mcpSend(server, message);
            return;
        }
    }

    const McpTool *found = nullptr;
    for (const McpTool &tool : m_mcpTools) {
        if (mcpPublicName(tool.server, tool.name) == call.name) {
            found = &tool;
            break;
        }
    }
    if (!found) {
        finishToolCall(run,
            QStringLiteral("{\"error\":\"Ferramenta MCP não encontrada.\"}"),
            QStringLiteral("⚙ %1: MCP não encontrado").arg(call.name));
        return;
    }

    McpServer *server = nullptr;
    for (McpServer *candidate : m_mcpServers) {
        if (candidate->name == found->server) {
            server = candidate;
            break;
        }
    }
    if (!server || !server->process || server->process->state() != QProcess::Running) {
        finishToolCall(run,
            QStringLiteral("{\"error\":%1}").arg(jsonQuote(
                QStringLiteral("Servidor MCP %1 não está rodando.").arg(found->server))),
            QStringLiteral("⚙ %1: servidor MCP parado").arg(call.name));
        return;
    }

    QString error;
    const QJsonObject arguments = parseArguments(call, &error);
    if (!error.isEmpty()) {
        finishToolCall(run, QStringLiteral("{\"error\":%1}").arg(jsonQuote(error)),
                       QStringLiteral("⚙ %1: argumentos inválidos").arg(call.name));
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("name"), found->name);
    params.insert(QStringLiteral("arguments"), arguments);

    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    const int id = server->nextId++;
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("method"), QStringLiteral("tools/call"));
    message.insert(QStringLiteral("params"), params);

    m_mcpCallRuns.insert(id, run);
    m_mcpCallTools.insert(id, call.name);
    if (displayingRun(run))
        m_messages->updateLastTool(
            QStringLiteral("⚙ %1: executando via MCP…").arg(call.name), QString());
    mcpSend(server, message);
}

void AppController::reloadMcp()
{
    loadMcpServers();
    emit toast(QStringLiteral("Servidores MCP recarregados (%1).").arg(m_mcpServers.size()));
}

void AppController::openMcpFile()
{
    const QString configDir = QFileInfo(Config::instance().filePath()).absolutePath();
    const QString path = configDir + QStringLiteral("/mcp.json");
    if (!QFileInfo::exists(path)) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write("{\n  \"mcpServers\": {\n"
                       "    \"exemplo\": {\n"
                       "      \"command\": \"npx\",\n"
                       "      \"args\": [\"-y\", \"@modelcontextprotocol/server-filesystem\", \".\"]\n"
                       "    }\n  }\n}\n");
            file.close();
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString AppController::permissionMode() const
{
    const QString mode = m_activeProject.permissionMode;
    if (mode == QLatin1String("ask") || mode == QLatin1String("auto")
        || mode == QLatin1String("full"))
        return mode;

    const QString fallback = Config::instance().ui().permissionMode;
    if (fallback == QLatin1String("ask") || fallback == QLatin1String("auto")
        || fallback == QLatin1String("full"))
        return fallback;
    return QStringLiteral("ask");
}

QString AppController::agentContextText() const
{
    const QString mode = permissionMode();
    QString modeText;
    if (mode == QLatin1String("ask"))
        modeText = QStringLiteral("solicitar aprovação do usuário para executar comandos, "
                                  "gravar/baixar arquivos e acessar a internet");
    else if (mode == QLatin1String("auto"))
        modeText = QStringLiteral("executar comandos, ler/gravar/baixar automaticamente "
                                  "dentro da pasta do projeto; pedir aprovação para comandos "
                                  "e para acesso fora da pasta e internet");
    else
        modeText = QStringLiteral("acesso completo, sem pedir aprovação");

    QString context = QStringLiteral(
        "Você é um agente com acesso ao computador do usuário por ferramentas. "
        "Use list_files e read_file para inspecionar arquivos, write_file para criar ou "
        "editar arquivos no disco, download_file para baixar arquivos/instaladores da "
        "internet, run_command para executar programas e comandos (instalações, builds, "
        "scripts, etc.), search_web para pesquisar e fetch_url para ler páginas. "
        "Ao pesquisar, abra as fontes relevantes e cite seus URLs em links Markdown. "
        "Use caminhos relativos à pasta de trabalho do projeto sempre que possível. "
        "Para gerar uma imagem, crie o arquivo (png/jpg) com run_command (ex.: script "
        "Python/matplotlib ou ImageMagick) e, para exibi-la no chat, escreva na resposta "
        "uma linha no formato ![descrição](caminho_do_arquivo) — o aplicativo mostra a "
        "imagem inline; use caminho relativo à pasta do projeto ou absoluto. "
        "Quando a mensagem do usuário citar um link (URL), use fetch_url ou browse_url "
        "para ler o conteúdo da página antes de responder.");
    context += QStringLiteral("\nPasta de trabalho: %1")
                   .arg(m_activeProject.workdir.isEmpty()
                            ? QStringLiteral("(não definida)")
                            : QDir::toNativeSeparators(m_activeProject.workdir));
    context += QStringLiteral("\nModo de permissão atual: %1.").arg(modeText);
    context += QStringLiteral(
        "\nConclua a tarefa com uma resposta final que descreva o resultado e o que "
        "não conseguiu concluir. Evite repetir verificações sem novas evidências.");
#ifdef Q_OS_WIN
    context += QStringLiteral(
        "\nO run_command executa comandos no cmd.exe (/D /S /C), não no PowerShell. "
        "Para PowerShell, invoque powershell.exe explicitamente. Para abrir um aplicativo "
        "gráfico sem aguardar seu fechamento, use start \"\" \"caminho\\programa.exe\". "
        "Um processo encerrado por timeout não comprova que o aplicativo ficou aberto.");
#endif
    return context;
}

bool AppController::toolNeedsApproval(const AgentRun *run, const ToolCall &call,
                                      bool *outsideWorkdir) const
{
    if (Config::instance().projectAllowsTool(run->projectId, call.name))
        return false;

    if (call.name == QLatin1String("run_command")) {
        QString parseError;
        const QJsonObject args = parseArguments(call, &parseError);
        if (parseError.isEmpty()
            && isDangerousCommand(args.value(QStringLiteral("command")).toString())) {
            if (outsideWorkdir)
                *outsideWorkdir = true;
            return true;
        }
    }

    QString mode = run->permissionMode;
    if (mode != QLatin1String("ask") && mode != QLatin1String("auto")
        && mode != QLatin1String("full"))
        mode = permissionMode();
    if (mode == QLatin1String("full"))
        return false;

    if (call.name == QLatin1String("run_command"))
        return true;

    if (call.name == QLatin1String("fetch_url")
        || call.name == QLatin1String("browse_url")
        || call.name == QLatin1String("search_web"))
        return true;

    if (call.name.startsWith(QLatin1String("mcp__")))
        return true;

    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty())
        return false;

    QString resolved = resolvePathFor(run->workdir,
                                      args.value(QStringLiteral("path")).toString());
    if (resolved.isEmpty())
        resolved = QDir::cleanPath(run->workdir);
    const bool inside = insideDir(run->workdir, resolved);
    if (outsideWorkdir)
        *outsideWorkdir = !inside;

    if (call.name == QLatin1String("write_file")
        || call.name == QLatin1String("download_file")
        || call.name == QLatin1String("restore_file"))
        return mode == QLatin1String("ask") || !inside;
    return !inside;
}

QString AppController::resolvePathFor(const QString &workdir, const QString &path)
{
    if (path.isEmpty())
        return QString();

    const QFileInfo info(path);
    if (info.isAbsolute())
        return QDir::cleanPath(path);
    if (workdir.isEmpty())
        return QDir::cleanPath(path);
    return QDir::cleanPath(QDir(workdir).absoluteFilePath(path));
}

bool AppController::insideDir(const QString &workdir, const QString &absolutePath)
{
    if (workdir.isEmpty() || absolutePath.isEmpty())
        return false;

    const QString base = QDir::cleanPath(QDir(workdir).absolutePath());
    const QString clean = QDir::cleanPath(absolutePath);
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return clean.compare(base, cs) == 0
        || clean.startsWith(base + QLatin1Char('/'), cs);
}

QJsonObject AppController::parseArguments(const ToolCall &call, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(call.arguments.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("Argumentos inválidos da ferramenta %1.").arg(call.name);
        return QJsonObject();
    }
    return doc.object();
}

bool AppController::runListFiles(AgentRun *run, const ToolCall &call, QString *result,
                                 QString *summary)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(error));
        *summary = QStringLiteral("⚙ list_files: argumentos inválidos");
        return false;
    }

    const QString requested = args.value(QStringLiteral("path")).toString();
    const bool recursive = args.value(QStringLiteral("recursive")).toBool(false);
    if (missingWorkdir(run, requested)) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("O projeto não tem pasta de trabalho definida. "
                           "Configure a pasta do projeto ou use um caminho absoluto.")));
        *summary = QStringLiteral("⚙ list_files: projeto sem pasta de trabalho");
        return false;
    }
    QString base = resolvePathFor(run->workdir, requested);
    if (base.isEmpty())
        base = QDir::cleanPath(run->workdir);
    base = QDir::cleanPath(base);

    const QFileInfo info(base);
    if (base.isEmpty() || !info.exists() || !info.isDir()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Pasta não encontrada: %1").arg(base)));
        *summary = QStringLiteral("⚙ list_files: pasta não encontrada");
        return false;
    }

    const QString workdir = run->workdir.isEmpty()
        ? QString()
        : QDir::cleanPath(QDir(run->workdir).absolutePath());

    QJsonArray entries;
    bool truncated = false;
    std::function<void(const QString &, int)> walk = [&](const QString &dir, int depth) {
        const QFileInfoList list = QDir(dir).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : list) {
            if (entries.size() >= kMaxListEntries) {
                truncated = true;
                return;
            }
            if (entry.fileName() == QLatin1String(".git")
                || entry.fileName() == QLatin1String("node_modules")
                || entry.fileName() == QLatin1String("__pycache__"))
                continue;

            QString display = entry.absoluteFilePath();
            if (!workdir.isEmpty() && insideDir(run->workdir, entry.absoluteFilePath()))
                display = QDir(workdir).relativeFilePath(entry.absoluteFilePath());
            display.replace(QLatin1Char('\\'), QLatin1Char('/'));

            QJsonObject item;
            item.insert(QStringLiteral("path"), display);
            item.insert(QStringLiteral("type"), entry.isDir() ? QStringLiteral("dir")
                                                              : QStringLiteral("file"));
            if (!entry.isDir())
                item.insert(QStringLiteral("size"), static_cast<double>(entry.size()));
            entries.append(item);

            if (entry.isDir() && recursive && depth < 3)
                walk(entry.absoluteFilePath(), depth + 1);
        }
    };
    walk(base, 0);

    QJsonObject out;
    out.insert(QStringLiteral("path"),
               requested.isEmpty() ? QStringLiteral(".") : requested);
    out.insert(QStringLiteral("entries"), entries);
    if (truncated)
        out.insert(QStringLiteral("truncated"), true);

    *result = QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    *summary = QStringLiteral("⚙ list_files: %1 item(ns) em %2")
                   .arg(entries.size())
                   .arg(requested.isEmpty() ? QStringLiteral(".") : requested);
    return true;
}

bool AppController::runReadFile(AgentRun *run, const ToolCall &call, QString *result,
                                QString *summary)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(error));
        *summary = QStringLiteral("⚙ read_file: argumentos inválidos");
        return false;
    }

    const QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        *result = QStringLiteral("{\"error\":\"Informe o caminho do arquivo.\"}");
        *summary = QStringLiteral("⚙ read_file: caminho não informado");
        return false;
    }
    if (missingWorkdir(run, path)) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("O projeto não tem pasta de trabalho definida. "
                           "Configure a pasta do projeto ou use um caminho absoluto.")));
        *summary = QStringLiteral("⚙ read_file: projeto sem pasta de trabalho");
        return false;
    }

    const QString resolved = resolvePathFor(run->workdir, path);
    const QFileInfo info(resolved);
    if (!info.exists() || !info.isFile()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Arquivo não encontrado: %1").arg(resolved)));
        *summary = QStringLiteral("⚙ read_file: não encontrado (%1)").arg(path);
        return false;
    }
    if (info.size() > kMaxReadBytes) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Arquivo muito grande (%1).").arg(formatSize(info.size()))));
        *summary = QStringLiteral("⚙ read_file: arquivo muito grande (%1)").arg(path);
        return false;
    }

    QFile file(resolved);
    if (!file.open(QIODevice::ReadOnly)) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Não foi possível ler: %1").arg(resolved)));
        *summary = QStringLiteral("⚙ read_file: falha ao ler (%1)").arg(path);
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    if (raw.contains('\0')) {
        *result = QStringLiteral("{\"error\":\"O arquivo parece ser binário.\"}");
        *summary = QStringLiteral("⚙ read_file: arquivo binário (%1)").arg(path);
        return false;
    }

    QString text = QString::fromUtf8(raw);
    int totalLines = text.count(QLatin1Char('\n')) + 1;
    const int startLine = args.value(QStringLiteral("start_line")).toInt(0);
    const int endLine = args.value(QStringLiteral("end_line")).toInt(0);
    if (startLine > 1 || endLine > 0) {
        const QStringList lines = text.split(QLatin1Char('\n'));
        totalLines = lines.size();
        const int from = qMax(1, startLine > 0 ? startLine : 1);
        const int to = qMin(endLine > 0 ? endLine : lines.size(), lines.size());
        if (from > to) {
            *result = QStringLiteral("{\"error\":\"Intervalo de linhas inválido.\"}");
            *summary = QStringLiteral("⚙ read_file: intervalo inválido (%1)").arg(path);
            return false;
        }
        text = lines.mid(from - 1, to - from + 1).join(QLatin1Char('\n'));
    }

    QJsonObject out;
    out.insert(QStringLiteral("path"), path);
    out.insert(QStringLiteral("content"), text);
    out.insert(QStringLiteral("total_lines"), totalLines);
    *result = QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    *summary = QStringLiteral("⚙ read_file: %1 (%2)").arg(path, formatSize(info.size()));
    return true;
}

bool AppController::runWriteFile(AgentRun *run, const ToolCall &call, QString *result,
                                 QString *summary)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(error));
        *summary = QStringLiteral("⚙ write_file: argumentos inválidos");
        return false;
    }

    const QString path = args.value(QStringLiteral("path")).toString();
    const QString content = args.value(QStringLiteral("content")).toString();
    if (path.isEmpty()) {
        *result = QStringLiteral("{\"error\":\"Informe o caminho do arquivo.\"}");
        *summary = QStringLiteral("⚙ write_file: caminho não informado");
        return false;
    }
    if (missingWorkdir(run, path)) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("O projeto não tem pasta de trabalho definida. "
                           "Configure a pasta do projeto ou use um caminho absoluto.")));
        *summary = QStringLiteral("⚙ write_file: projeto sem pasta de trabalho");
        return false;
    }
    const QByteArray data = content.toUtf8();
    if (data.size() > kMaxWriteBytes) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Conteúdo muito grande (%1).").arg(formatSize(data.size()))));
        *summary = QStringLiteral("⚙ write_file: conteúdo muito grande");
        return false;
    }

    const QString resolved = resolvePathFor(run->workdir, path);
    QDir().mkpath(QFileInfo(resolved).absolutePath());

    backupFile(resolved);

    QFile file(resolved);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Não foi possível gravar: %1").arg(resolved)));
        *summary = QStringLiteral("⚙ write_file: falha ao gravar (%1)").arg(path);
        return false;
    }
    file.write(data);
    file.close();

    m_api->logActivity(QStringLiteral("file_write"), resolved);
    if (displayingRun(run) && insideDir(run->workdir, resolved))
        refreshWorkdirFiles();

    QJsonObject out;
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("path"), path);
    out.insert(QStringLiteral("bytes"), data.size());
    *result = QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    *summary = QStringLiteral("⚙ write_file: %1 (%2)").arg(path, formatSize(data.size()));
    return true;
}

bool AppController::runFindFiles(AgentRun *run, const ToolCall &call, QString *result,
                                 QString *summary)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(error));
        *summary = QStringLiteral("⚙ find_files: argumentos inválidos");
        return false;
    }

    const QString glob = args.value(QStringLiteral("name_glob")).toString().trimmed();
    const QString requested = args.value(QStringLiteral("path")).toString();
    if (glob.isEmpty()) {
        *result = QStringLiteral("{\"error\":\"Informe name_glob (ex.: *.cpp).\"}");
        *summary = QStringLiteral("⚙ find_files: name_glob não informado");
        return false;
    }
    if (missingWorkdir(run, requested)) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("O projeto não tem pasta de trabalho definida. "
                           "Configure a pasta do projeto ou use um caminho absoluto.")));
        *summary = QStringLiteral("⚙ find_files: projeto sem pasta de trabalho");
        return false;
    }
    QString base = resolvePathFor(run->workdir, requested);
    if (base.isEmpty())
        base = QDir::cleanPath(run->workdir);
    if (!QFileInfo(base).isDir()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Pasta não encontrada: %1").arg(base)));
        *summary = QStringLiteral("⚙ find_files: pasta não encontrada");
        return false;
    }

    const int limit = 200;
    QJsonArray matches;
    QDirIterator it(base, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && matches.size() < limit) {
        it.next();
        const QFileInfo info = it.fileInfo();
        QString rel = QDir(base).relativeFilePath(info.absoluteFilePath());
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (rel.startsWith(QLatin1String(".git/"))
            || rel.contains(QLatin1String("/.git/"))
            || rel.contains(QLatin1String("node_modules/"))
            || rel.contains(QLatin1String("__pycache__/")))
            continue;
        if (QDir::match(glob, info.fileName()))
            matches.append(rel);
    }

    QJsonObject out;
    out.insert(QStringLiteral("matches"), matches);
    if (matches.size() >= limit)
        out.insert(QStringLiteral("truncated"), true);
    *result = QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    *summary = QStringLiteral("⚙ find_files: %1 arquivo(s)").arg(matches.size());
    return true;
}

bool AppController::runGrepFiles(AgentRun *run, const ToolCall &call, QString *result,
                                 QString *summary)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(error));
        *summary = QStringLiteral("⚙ grep_files: argumentos inválidos");
        return false;
    }

    const QString pattern = args.value(QStringLiteral("pattern")).toString();
    const QString requested = args.value(QStringLiteral("path")).toString();
    const bool ignoreCase = args.value(QStringLiteral("ignore_case")).toBool(false);
    int limit = args.value(QStringLiteral("max_results")).toInt(100);
    limit = qBound(1, limit, 500);

    if (pattern.isEmpty()) {
        *result = QStringLiteral("{\"error\":\"Informe pattern (expressão regular).\"}");
        *summary = QStringLiteral("⚙ grep_files: pattern não informado");
        return false;
    }
    if (missingWorkdir(run, requested)) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("O projeto não tem pasta de trabalho definida. "
                           "Configure a pasta do projeto ou use um caminho absoluto.")));
        *summary = QStringLiteral("⚙ grep_files: projeto sem pasta de trabalho");
        return false;
    }

    QRegularExpression regex(pattern, ignoreCase
                                           ? QRegularExpression::CaseInsensitiveOption
                                           : QRegularExpression::NoPatternOption);
    if (!regex.isValid()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Expressão regular inválida: %1").arg(regex.errorString())));
        *summary = QStringLiteral("⚙ grep_files: regex inválida");
        return false;
    }

    QString base = resolvePathFor(run->workdir, requested);
    if (base.isEmpty())
        base = QDir::cleanPath(run->workdir);
    if (!QFileInfo(base).isDir()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Pasta não encontrada: %1").arg(base)));
        *summary = QStringLiteral("⚙ grep_files: pasta não encontrada");
        return false;
    }

    QJsonArray matches;
    QDirIterator it(base, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && matches.size() < limit) {
        it.next();
        const QFileInfo info = it.fileInfo();
        QString rel = QDir(base).relativeFilePath(info.absoluteFilePath());
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (rel.startsWith(QLatin1String(".git/"))
            || rel.contains(QLatin1String("/.git/"))
            || rel.contains(QLatin1String("node_modules/"))
            || rel.contains(QLatin1String("__pycache__/")))
            continue;
        if (info.size() > 1024 * 1024)
            continue;

        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray raw = file.readAll();
        file.close();
        if (raw.contains('\0'))
            continue;

        const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
        for (int lineNo = 0; lineNo < lines.size() && matches.size() < limit; ++lineNo) {
            if (!regex.match(lines.at(lineNo)).hasMatch())
                continue;
            QJsonObject item;
            item.insert(QStringLiteral("path"), rel);
            item.insert(QStringLiteral("line"), lineNo + 1);
            item.insert(QStringLiteral("text"), lines.at(lineNo).trimmed().left(200));
            matches.append(item);
        }
    }

    QJsonObject out;
    out.insert(QStringLiteral("matches"), matches);
    *result = QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    *summary = QStringLiteral("⚙ grep_files: %1 ocorrência(s)").arg(matches.size());
    return true;
}

bool AppController::runRestoreFile(AgentRun *run, const ToolCall &call, QString *result,
                                   QString *summary)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(error));
        *summary = QStringLiteral("⚙ restore_file: argumentos inválidos");
        return false;
    }

    const QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        *result = QStringLiteral("{\"error\":\"Informe o caminho do arquivo.\"}");
        *summary = QStringLiteral("⚙ restore_file: caminho não informado");
        return false;
    }
    if (missingWorkdir(run, path)) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("O projeto não tem pasta de trabalho definida. "
                           "Configure a pasta do projeto ou use um caminho absoluto.")));
        *summary = QStringLiteral("⚙ restore_file: projeto sem pasta de trabalho");
        return false;
    }

    const QString resolved = resolvePathFor(run->workdir, path);
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(resolved.toUtf8(), QCryptographicHash::Sha1).toHex());
    const QDir dir(backupsRoot() + QLatin1Char('/') + key);
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Nenhum backup encontrado para: %1").arg(resolved)));
        *summary = QStringLiteral("⚙ restore_file: sem backup (%1)").arg(path);
        return false;
    }

    QDir().mkpath(QFileInfo(resolved).absolutePath());
    QFile::remove(resolved);
    if (!QFile::copy(files.first().absoluteFilePath(), resolved)) {
        *result = QStringLiteral("{\"error\":%1}").arg(jsonQuote(
            QStringLiteral("Não foi possível restaurar: %1").arg(resolved)));
        *summary = QStringLiteral("⚙ restore_file: falha ao restaurar (%1)").arg(path);
        return false;
    }

    m_api->logActivity(QStringLiteral("file_restore"), resolved);
    if (displayingRun(run) && insideDir(run->workdir, resolved))
        refreshWorkdirFiles();

    QJsonObject out;
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("path"), resolved);
    out.insert(QStringLiteral("backup"), files.first().fileName());
    *result = QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    *summary = QStringLiteral("⚙ restore_file: %1 restaurado").arg(path);
    return true;
}

void AppController::browseUrl(AgentRun *run, const ToolCall &call)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        finishToolCall(run, QStringLiteral("{\"error\":%1}").arg(jsonQuote(error)),
                       QStringLiteral("⚙ browse_url: argumentos inválidos"));
        return;
    }

    const QString url = args.value(QStringLiteral("url")).toString().trimmed();
    const QUrl target(url);
    if (!target.isValid() || (target.scheme() != QLatin1String("http")
                              && target.scheme() != QLatin1String("https"))) {
        finishToolCall(run, QStringLiteral("{\"error\":\"URL inválida.\"}"),
                       QStringLiteral("⚙ browse_url: URL inválida"));
        return;
    }

    QString shown = url;
    if (shown.size() > 60)
        shown = shown.left(57) + QStringLiteral("...");
    if (displayingRun(run))
        m_messages->updateLastTool(QStringLiteral("⚙ browse_url: abrindo %1…").arg(shown),
                                   QString());

    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << QStandardPaths::findExecutable(QStringLiteral("msedge"))
               << QStringLiteral("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe")
               << QStringLiteral("C:/Program Files/Microsoft/Edge/Application/msedge.exe")
               << QStandardPaths::findExecutable(QStringLiteral("chrome"))
               << QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe")
               << QStringLiteral("C:/Program Files (x86)/Google/Chrome/Application/chrome.exe");
#else
    candidates << QStandardPaths::findExecutable(QStringLiteral("chromium"))
               << QStandardPaths::findExecutable(QStringLiteral("chromium-browser"))
               << QStandardPaths::findExecutable(QStringLiteral("google-chrome"))
               << QStandardPaths::findExecutable(QStringLiteral("google-chrome-stable"))
               << QStandardPaths::findExecutable(QStringLiteral("microsoft-edge"));
#endif

    QString browser;
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            browser = candidate;
            break;
        }
    }
    if (browser.isEmpty()) {
        fetchUrl(run, call);
        return;
    }

    auto timedOut = QSharedPointer<bool>::create(false);
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    run->process = process;

    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process, timedOut] {
        *timedOut = true;
        process->kill();
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, run, process, timer, timedOut, url, shown, call](int exitCode, QProcess::ExitStatus) {
                timer->stop();
                if (run->process != process) {
                    process->deleteLater();
                    return;
                }
                run->process = nullptr;
                const QByteArray raw = process->readAll();
                process->deleteLater();

                if (exitCode != 0 || raw.trimmed().isEmpty()) {
                    fetchUrl(run, call);
                    return;
                }

                const QByteArray limited = raw.left(kMaxFetchBytes);
                const QString html = QString::fromUtf8(limited);
                const QRegularExpression titlePattern(
                    QStringLiteral("<title[^>]*>([\\s\\S]*?)</title>"),
                    QRegularExpression::CaseInsensitiveOption);
                const QString title = htmlToText(titlePattern.match(html).captured(1));
                QString body = htmlToText(html);
                if (raw.size() > limited.size())
                    body += QStringLiteral("\n\n[conteúdo truncado em %1]")
                                .arg(formatSize(limited.size()));

                QJsonObject out;
                out.insert(QStringLiteral("url"), url);
                out.insert(QStringLiteral("title"), title);
                out.insert(QStringLiteral("content"), body);
                finishToolCall(run,
                    QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)),
                    QStringLiteral("⚙ browse_url: %1 (%2)")
                        .arg(shown, formatSize(limited.size())));
            });

    connect(process, &QProcess::errorOccurred, this,
            [this, run, process, call](QProcess::ProcessError processError) {
                if (processError != QProcess::FailedToStart)
                    return;
                if (run->process == process)
                    run->process = nullptr;
                process->deleteLater();
                fetchUrl(run, call);
            });

    process->start(browser,
                   {QStringLiteral("--headless=new"), QStringLiteral("--disable-gpu"),
                    QStringLiteral("--no-first-run"),
                    QStringLiteral("--no-default-browser-check"),
                    QStringLiteral("--dump-dom"), url});
    timer->start(60000);
}

void AppController::searchWeb(AgentRun *run, const ToolCall &call)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    const QString query = args.value(QStringLiteral("query")).toString().trimmed();
    if (!error.isEmpty() || query.isEmpty() || query.size() > 300) {
        finishToolCall(run, QStringLiteral("{\"error\":\"Pesquisa inválida.\"}"),
                       QStringLiteral("⚙ search_web: pesquisa inválida"));
        return;
    }

    auto *manager = new QNetworkAccessManager(this);
    QUrl url(QStringLiteral("https://www.bing.com/search"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("format"), QStringLiteral("rss"));
    params.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(params);
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 DeepSeekChat/1.0"));
    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, run, reply, manager, query] {
        const QByteArray data = reply->readAll();
        const auto networkError = reply->error();
        const QString networkText = reply->errorString();
        reply->deleteLater();
        manager->deleteLater();
        if (networkError != QNetworkReply::NoError) {
            finishToolCall(run, QStringLiteral("{\"error\":%1}").arg(jsonQuote(networkText)),
                           QStringLiteral("⚙ search_web: falhou"));
            return;
        }

        QJsonArray results;
        QXmlStreamReader xml(data.left(512 * 1024));
        while (!xml.atEnd() && results.size() < 8) {
            xml.readNext();
            if (!xml.isStartElement() || xml.name() != QLatin1String("item"))
                continue;
            QJsonObject item;
            while (xml.readNextStartElement()) {
                const QString name = xml.name().toString();
                const QString value = xml.readElementText(QXmlStreamReader::SkipChildElements);
                if (name == QLatin1String("title"))
                    item.insert(QStringLiteral("title"), value.left(200));
                else if (name == QLatin1String("link"))
                    item.insert(QStringLiteral("url"), value);
                else if (name == QLatin1String("description"))
                    item.insert(QStringLiteral("summary"), htmlToText(value).left(500));
            }
            const QUrl resultUrl(item.value(QStringLiteral("url")).toString());
            if (resultUrl.isValid() && (resultUrl.scheme() == QLatin1String("https")
                                        || resultUrl.scheme() == QLatin1String("http")))
                results.append(item);
        }
        QJsonObject out;
        out.insert(QStringLiteral("query"), query);
        out.insert(QStringLiteral("results"), results);
        if (results.isEmpty())
            out.insert(QStringLiteral("note"), QStringLiteral("Nenhum resultado disponível."));
        finishToolCall(run,
            QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)),
            QStringLiteral("⚙ search_web: %1 resultado(s)").arg(results.size()));
    });
}

void AppController::fetchUrl(AgentRun *run, const ToolCall &call)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        finishToolCall(run, QStringLiteral("{\"error\":%1}").arg(jsonQuote(error)),
                       QStringLiteral("⚙ fetch_url: argumentos inválidos"));
        return;
    }

    const QString url = args.value(QStringLiteral("url")).toString().trimmed();
    const QUrl target(url);
    if (!target.isValid() || (target.scheme() != QLatin1String("http")
                              && target.scheme() != QLatin1String("https"))) {
        finishToolCall(run, QStringLiteral("{\"error\":\"URL inválida.\"}"),
                       QStringLiteral("⚙ fetch_url: URL inválida"));
        return;
    }

    if (displayingRun(run))
        m_messages->updateLastTool(QStringLiteral("⚙ fetch_url: baixando %1…").arg(url), QString());

    auto *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(target);
    request.setTransferTimeout(45000);
    request.setMaximumRedirectsAllowed(5);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DeepSeekChat/1.0"));

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, run, reply, manager, url] {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString contentType = reply->header(
            QNetworkRequest::ContentTypeHeader).toString();
        const QNetworkReply::NetworkError netError = reply->error();
        const QString netErrorText = reply->errorString();
        reply->deleteLater();
        manager->deleteLater();

        if (netError != QNetworkReply::NoError && status == 0) {
            finishToolCall(run,
                QStringLiteral("{\"error\":%1}").arg(jsonQuote(
                    QStringLiteral("Falha ao baixar: %1").arg(netErrorText))),
                QStringLiteral("⚙ fetch_url: falhou (%1)").arg(netErrorText));
            return;
        }

        const QByteArray limited = data.left(kMaxFetchBytes);
        QString body = QString::fromUtf8(limited);
        QString title;
        if (contentType.contains(QLatin1String("html"), Qt::CaseInsensitive)) {
            const QRegularExpression titlePattern(
                QStringLiteral("<title[^>]*>([\\s\\S]*?)</title>"),
                QRegularExpression::CaseInsensitiveOption);
            title = htmlToText(titlePattern.match(body).captured(1));
            body = htmlToText(body);
        }
        if (data.size() > limited.size())
            body += QStringLiteral("\n\n[conteúdo truncado em %1]")
                        .arg(formatSize(limited.size()));

        QJsonObject out;
        out.insert(QStringLiteral("url"), url);
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("status"), status);
        out.insert(QStringLiteral("content"), body);
        finishToolCall(run,
            QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)),
            QStringLiteral("⚙ fetch_url: HTTP %1 (%2)").arg(status).arg(formatSize(limited.size())));
    });
}

void AppController::downloadFile(AgentRun *run, const ToolCall &call)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        finishToolCall(run, QStringLiteral("{\"error\":%1}").arg(jsonQuote(error)),
                       QStringLiteral("⚙ download_file: argumentos inválidos"));
        return;
    }

    const QString url = args.value(QStringLiteral("url")).toString().trimmed();
    const QString path = args.value(QStringLiteral("path")).toString();
    const QUrl target(url);
    if (!target.isValid() || (target.scheme() != QLatin1String("http")
                              && target.scheme() != QLatin1String("https"))) {
        finishToolCall(run, QStringLiteral("{\"error\":\"URL inválida.\"}"),
                       QStringLiteral("⚙ download_file: URL inválida"));
        return;
    }
    if (path.isEmpty()) {
        finishToolCall(run, QStringLiteral("{\"error\":\"Informe o destino do download.\"}"),
                       QStringLiteral("⚙ download_file: destino não informado"));
        return;
    }
    if (missingWorkdir(run, path)) {
        finishToolCall(run, QStringLiteral("{\"error\":%1}").arg(jsonQuote(
                           QStringLiteral("O projeto não tem pasta de trabalho definida. "
                                          "Configure a pasta do projeto ou use um caminho "
                                          "absoluto."))),
                       QStringLiteral("⚙ download_file: projeto sem pasta de trabalho"));
        return;
    }

    const QString resolved = resolvePathFor(run->workdir, path);
    QDir().mkpath(QFileInfo(resolved).absolutePath());

    QString shown = url;
    if (shown.size() > 60)
        shown = shown.left(57) + QStringLiteral("...");
    if (displayingRun(run))
        m_messages->updateLastTool(
            QStringLiteral("⚙ download_file: baixando %1…").arg(shown), QString());

    auto *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(target);
    request.setTransferTimeout(600000);
    request.setMaximumRedirectsAllowed(5);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DeepSeekChat/1.0"));

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, run, reply, manager, resolved] {
                const QByteArray data = reply->readAll();
                const int status = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QNetworkReply::NetworkError netError = reply->error();
                const QString netErrorText = reply->errorString();
                reply->deleteLater();
                manager->deleteLater();

                if (netError != QNetworkReply::NoError && status == 0) {
                    finishToolCall(run,
                        QStringLiteral("{\"error\":%1}").arg(jsonQuote(
                            QStringLiteral("Falha ao baixar: %1").arg(netErrorText))),
                        QStringLiteral("⚙ download_file: falhou (%1)").arg(netErrorText));
                    return;
                }
                if (static_cast<qint64>(data.size()) > kMaxDownloadBytes) {
                    finishToolCall(run,
                        QStringLiteral("{\"error\":%1}").arg(jsonQuote(
                            QStringLiteral("Arquivo muito grande (%1); limite %2.")
                                .arg(formatSize(data.size()), formatSize(kMaxDownloadBytes)))),
                        QStringLiteral("⚙ download_file: arquivo muito grande"));
                    return;
                }

                backupFile(resolved);

                QFile file(resolved);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    finishToolCall(run,
                        QStringLiteral("{\"error\":%1}").arg(jsonQuote(
                            QStringLiteral("Não foi possível gravar: %1").arg(resolved))),
                        QStringLiteral("⚙ download_file: falha ao gravar"));
                    return;
                }
                file.write(data);
                file.close();

                m_api->logActivity(QStringLiteral("file_download"), resolved);
                if (displayingRun(run) && insideDir(run->workdir, resolved))
                    refreshWorkdirFiles();

                QJsonObject out;
                out.insert(QStringLiteral("ok"), true);
                out.insert(QStringLiteral("path"), resolved);
                out.insert(QStringLiteral("bytes"), data.size());
                finishToolCall(run,
                    QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)),
                    QStringLiteral("⚙ download_file: %1 (%2)")
                        .arg(QFileInfo(resolved).fileName(),
                             formatSize(data.size())));
            });
}

void AppController::runCommand(AgentRun *run, const ToolCall &call)
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (!error.isEmpty()) {
        finishToolCall(run, QStringLiteral("{\"error\":%1}").arg(jsonQuote(error)),
                       QStringLiteral("⚙ run_command: argumentos inválidos"));
        return;
    }

    const QString command = args.value(QStringLiteral("command")).toString().trimmed();
    if (command.isEmpty()) {
        finishToolCall(run, QStringLiteral("{\"error\":\"Informe o comando.\"}"),
                       QStringLiteral("⚙ run_command: comando não informado"));
        return;
    }

    QString cwd = args.value(QStringLiteral("cwd")).toString();
    if (cwd.isEmpty())
        cwd = run->workdir;
    else
        cwd = resolvePathFor(run->workdir, cwd);
    if (cwd.isEmpty())
        cwd = QDir::homePath();

    const QFileInfo cwdInfo(cwd);
    if (!cwdInfo.exists() || !cwdInfo.isDir()) {
        finishToolCall(run, QStringLiteral("{\"error\":%1}").arg(jsonQuote(
                           QStringLiteral("Pasta inválida: %1").arg(cwd))),
                       QStringLiteral("⚙ run_command: pasta inválida"));
        return;
    }

    int timeoutSeconds = args.value(QStringLiteral("timeout_seconds")).toInt(120);
    timeoutSeconds = qBound(5, timeoutSeconds, 600);

    QString shown = command.simplified();
    if (shown.size() > 60)
        shown = shown.left(57) + QStringLiteral("...");
    if (displayingRun(run))
        m_messages->updateLastTool(QStringLiteral("⚙ run_command: %1…").arg(shown),
                                   QString());

#ifdef Q_OS_WIN
    const QString program = QStringLiteral("cmd.exe");
    const QStringList shellArgs;
#else
    const QString program = QStringLiteral("/bin/sh");
    const QStringList shellArgs = {QStringLiteral("-c"), command};
#endif

    auto timedOut = QSharedPointer<bool>::create(false);
    auto *process = new QProcess(this);
    process->setWorkingDirectory(cwd);
    process->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    // cmd.exe does not parse the CRT backslash escaping used by QProcess arguments.
    process->setNativeArguments(QStringLiteral("/D /S /C \"") + command + QLatin1Char('"'));
#endif
    run->process = process;

    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process, timedOut] {
        *timedOut = true;
        process->kill();
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, run, process, timer, timedOut](int exitCode, QProcess::ExitStatus) {
                timer->stop();
                if (run->process != process) {
                    process->deleteLater();
                    return;
                }
                run->process = nullptr;
                const QByteArray raw = process->readAll();
                const bool wasTimeout = *timedOut;
                process->deleteLater();

                QString output = QString::fromUtf8(raw);
                if (output.contains(QChar::ReplacementCharacter)) {
#ifdef Q_OS_WIN
                    const int count = MultiByteToWideChar(GetOEMCP(), 0, raw.constData(),
                                                        int(raw.size()), nullptr, 0);
                    if (count > 0) {
                        std::wstring decoded(size_t(count), L'\0');
                        MultiByteToWideChar(GetOEMCP(), 0, raw.constData(), int(raw.size()),
                                            decoded.data(), count);
                        output = QString::fromStdWString(decoded);
                    }
#else
                    output = QString::fromLocal8Bit(raw);
#endif
                }
                if (output.size() > kMaxToolOutput) {
                    const QString head = output.left(kMaxToolOutput - 20 * 1024);
                    const QString tail = output.right(20 * 1024);
                    output = head + QStringLiteral("\n\n[... saída truncada ...]\n\n") + tail;
                }

                QJsonObject out;
                out.insert(QStringLiteral("exit_code"), exitCode);
                out.insert(QStringLiteral("output"), output);
                if (wasTimeout)
                    out.insert(QStringLiteral("timed_out"), true);
                finishToolCall(run,
                    QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)),
                    QStringLiteral("⚙ run_command: exit %1%2")
                        .arg(exitCode)
                        .arg(wasTimeout ? QStringLiteral(" (timeout)") : QString()));
            });

    connect(process, &QProcess::errorOccurred, this,
            [this, run, process](QProcess::ProcessError processError) {
                if (processError != QProcess::FailedToStart)
                    return;
                if (run->process == process)
                    run->process = nullptr;
                process->deleteLater();
                finishToolCall(run,
                    QStringLiteral("{\"error\":\"Não foi possível iniciar o processo.\"}"),
                    QStringLiteral("⚙ run_command: falha ao iniciar"));
            });

    process->start(program, shellArgs);
    timer->start(timeoutSeconds * 1000);
}

QString AppController::htmlToText(const QString &html)
{
    QString text = html;
    text.remove(QRegularExpression(QStringLiteral("<script[\\s\\S]*?</script>"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("<style[\\s\\S]*?</style>"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.replace(QRegularExpression(
                     QStringLiteral("<(br|/p|/div|/li|/h[1-6]|/tr)[^>]*>"),
                     QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QStringLiteral(" "));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QRegularExpression(QStringLiteral("[ \\t]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return text.trimmed();
}

QString AppController::toolApprovalTitle(const ToolCall &call)
{
    if (call.name == QLatin1String("fetch_url")
        || call.name == QLatin1String("browse_url")
        || call.name == QLatin1String("search_web"))
        return QStringLiteral("A IA quer acessar a internet");
    if (call.name == QLatin1String("download_file"))
        return QStringLiteral("A IA quer baixar um arquivo");
    if (call.name == QLatin1String("run_command")) {
        QString parseError;
        const QJsonObject args = parseArguments(call, &parseError);
        if (parseError.isEmpty()
            && isDangerousCommand(args.value(QStringLiteral("command")).toString()))
            return QStringLiteral("Comando potencialmente PERIGOSO");
        return QStringLiteral("A IA quer executar um comando");
    }
    if (call.name == QLatin1String("write_file"))
        return QStringLiteral("A IA quer gravar um arquivo");
    if (call.name == QLatin1String("read_file"))
        return QStringLiteral("A IA quer ler um arquivo");
    if (call.name == QLatin1String("list_files"))
        return QStringLiteral("A IA quer listar arquivos");
    return QStringLiteral("A IA quer executar uma ação");
}

QString AppController::toolApprovalDetail(const AgentRun *run, const ToolCall &call,
                                          bool outsideWorkdir) const
{
    QString error;
    const QJsonObject args = parseArguments(call, &error);
    if (call.name == QLatin1String("search_web"))
        return QStringLiteral("Pesquisa: %1")
            .arg(args.value(QStringLiteral("query")).toString());
    if (call.name == QLatin1String("fetch_url")
        || call.name == QLatin1String("browse_url"))
        return QStringLiteral("URL: %1").arg(args.value(QStringLiteral("url")).toString());

    if (call.name == QLatin1String("download_file")) {
        QString detail = QStringLiteral("URL: %1")
                             .arg(args.value(QStringLiteral("url")).toString());
        detail += QStringLiteral("\nDestino: %1")
                      .arg(resolvePathFor(run->workdir,
                                          args.value(QStringLiteral("path")).toString()));
        if (outsideWorkdir)
            detail += QStringLiteral("\n(fora da pasta do projeto)");
        return detail;
    }

    if (call.name == QLatin1String("run_command")) {
        QString detail = QStringLiteral("Comando: %1")
                             .arg(args.value(QStringLiteral("command")).toString());
        QString cwd = args.value(QStringLiteral("cwd")).toString();
        if (cwd.isEmpty())
            cwd = run->workdir;
        else
            cwd = resolvePathFor(run->workdir, cwd);
        if (cwd.isEmpty())
            cwd = QDir::homePath();
        detail += QStringLiteral("\nPasta: %1").arg(cwd);
        return detail;
    }

    const QString path = args.value(QStringLiteral("path")).toString();
    QString detail = QStringLiteral("Caminho: %1").arg(resolvePathFor(run->workdir, path));
    if (outsideWorkdir)
        detail += QStringLiteral("\n(fora da pasta do projeto)");
    return detail;
}

QJsonArray AppController::toolDefinitions()
{
    auto makeTool = [](const QString &name, const QString &description,
                       const QJsonObject &properties, const QJsonArray &required) {
        QJsonObject parameters;
        parameters.insert(QStringLiteral("type"), QStringLiteral("object"));
        parameters.insert(QStringLiteral("properties"), properties);
        parameters.insert(QStringLiteral("required"), required);

        QJsonObject function;
        function.insert(QStringLiteral("name"), name);
        function.insert(QStringLiteral("description"), description);
        function.insert(QStringLiteral("parameters"), parameters);

        QJsonObject tool;
        tool.insert(QStringLiteral("type"), QStringLiteral("function"));
        tool.insert(QStringLiteral("function"), function);
        return tool;
    };

    auto stringProp = [](const QString &description) {
        QJsonObject prop;
        prop.insert(QStringLiteral("type"), QStringLiteral("string"));
        prop.insert(QStringLiteral("description"), description);
        return prop;
    };
    auto boolProp = [](const QString &description) {
        QJsonObject prop;
        prop.insert(QStringLiteral("type"), QStringLiteral("boolean"));
        prop.insert(QStringLiteral("description"), description);
        return prop;
    };
    auto intProp = [](const QString &description) {
        QJsonObject prop;
        prop.insert(QStringLiteral("type"), QStringLiteral("integer"));
        prop.insert(QStringLiteral("description"), description);
        return prop;
    };

    QJsonArray tools;

    QJsonObject listProps;
    listProps.insert(QStringLiteral("path"), stringProp(QStringLiteral(
        "Pasta a listar (relativa à pasta do projeto ou absoluta). Vazio lista a raiz.")));
    listProps.insert(QStringLiteral("recursive"), boolProp(QStringLiteral(
        "Se true, lista subpastas até 3 níveis.")));
    tools.append(makeTool(QStringLiteral("list_files"),
                          QStringLiteral("Lista arquivos e pastas do computador."),
                          listProps, QJsonArray()));

    QJsonObject readProps;
    readProps.insert(QStringLiteral("path"), stringProp(QStringLiteral(
        "Caminho do arquivo (relativo à pasta do projeto ou absoluto).")));
    readProps.insert(QStringLiteral("start_line"), intProp(QStringLiteral(
        "Primeira linha a ler (1-based, opcional).")));
    readProps.insert(QStringLiteral("end_line"), intProp(QStringLiteral(
        "Última linha a ler (opcional).")));
    tools.append(makeTool(QStringLiteral("read_file"),
                          QStringLiteral("Lê o conteúdo de um arquivo de texto."),
                          readProps, QJsonArray{QStringLiteral("path")}));

    QJsonObject writeProps;
    writeProps.insert(QStringLiteral("path"), stringProp(QStringLiteral(
        "Caminho do arquivo a criar ou sobrescrever (relativo à pasta do projeto ou absoluto).")));
    writeProps.insert(QStringLiteral("content"), stringProp(QStringLiteral(
        "Conteúdo completo do arquivo.")));
    tools.append(makeTool(QStringLiteral("write_file"),
                          QStringLiteral("Cria ou sobrescreve um arquivo com o conteúdo dado."),
                          writeProps,
                          QJsonArray{QStringLiteral("path"), QStringLiteral("content")}));

    QJsonObject fetchProps;
    fetchProps.insert(QStringLiteral("url"), stringProp(QStringLiteral(
        "URL http(s) da página a baixar.")));
    tools.append(makeTool(QStringLiteral("fetch_url"),
                          QStringLiteral("Baixa o conteúdo de uma página da internet."),
                          fetchProps, QJsonArray{QStringLiteral("url")}));

    QJsonObject searchProps;
    searchProps.insert(QStringLiteral("query"), stringProp(QStringLiteral(
        "Termos da pesquisa na web.")));
    tools.append(makeTool(QStringLiteral("search_web"),
                          QStringLiteral("Pesquisa páginas na web. Retorna títulos, links e "
                                         "resumos; abra as fontes relevantes com fetch_url "
                                         "ou browse_url antes de citá-las."),
                          searchProps, QJsonArray{QStringLiteral("query")}));

    QJsonObject downloadProps;
    downloadProps.insert(QStringLiteral("url"), stringProp(QStringLiteral(
        "URL http(s) do arquivo a baixar.")));
    downloadProps.insert(QStringLiteral("path"), stringProp(QStringLiteral(
        "Destino do arquivo (relativo à pasta do projeto ou absoluto).")));
    tools.append(makeTool(QStringLiteral("download_file"),
                          QStringLiteral("Baixa um arquivo da internet para o disco "
                                         "(instaladores, imagens, zips, etc.)."),
                          downloadProps,
                          QJsonArray{QStringLiteral("url"), QStringLiteral("path")}));

    QJsonObject commandProps;
    commandProps.insert(QStringLiteral("command"), stringProp(QStringLiteral(
        "Comando de shell a executar (cmd.exe no Windows; /bin/sh nos demais).")));
    commandProps.insert(QStringLiteral("cwd"), stringProp(QStringLiteral(
        "Pasta de execução (relativa à pasta do projeto ou absoluta). "
        "Padrão: pasta de trabalho do projeto.")));
    commandProps.insert(QStringLiteral("timeout_seconds"), intProp(QStringLiteral(
        "Tempo máximo em segundos (padrão 120, máximo 600).")));
    tools.append(makeTool(QStringLiteral("run_command"),
                          QStringLiteral("Executa um comando/programa no computador e "
                                         "retorna a saída e o código de saída. Use para "
                                         "instalar, compilar, executar scripts, etc."),
                          commandProps, QJsonArray{QStringLiteral("command")}));

    QJsonObject findProps;
    findProps.insert(QStringLiteral("name_glob"), stringProp(QStringLiteral(
        "Máscara do nome do arquivo (ex.: *.cpp, *.md).")));
    findProps.insert(QStringLiteral("path"), stringProp(QStringLiteral(
        "Pasta base (relativa à pasta do projeto ou absoluta). Padrão: pasta do projeto.")));
    tools.append(makeTool(QStringLiteral("find_files"),
                          QStringLiteral("Procura arquivos pelo nome (glob) dentro da pasta."),
                          findProps, QJsonArray{QStringLiteral("name_glob")}));

    QJsonObject grepProps;
    grepProps.insert(QStringLiteral("pattern"), stringProp(QStringLiteral(
        "Expressão regular a procurar no conteúdo dos arquivos.")));
    grepProps.insert(QStringLiteral("path"), stringProp(QStringLiteral(
        "Pasta base (relativa à pasta do projeto ou absoluta). Padrão: pasta do projeto.")));
    grepProps.insert(QStringLiteral("ignore_case"), boolProp(QStringLiteral(
        "Ignorar maiúsculas/minúsculas.")));
    grepProps.insert(QStringLiteral("max_results"), intProp(QStringLiteral(
        "Máximo de ocorrências (padrão 100, máx 500).")));
    tools.append(makeTool(QStringLiteral("grep_files"),
                          QStringLiteral("Procura texto no conteúdo dos arquivos "
                                         "(retorna caminho, linha e trecho)."),
                          grepProps, QJsonArray{QStringLiteral("pattern")}));

    QJsonObject browseProps;
    browseProps.insert(QStringLiteral("url"), stringProp(QStringLiteral(
        "URL http(s) da página.")));
    tools.append(makeTool(QStringLiteral("browse_url"),
                          QStringLiteral("Abre uma página com navegador headless "
                                         "(JavaScript) e retorna o texto; se não houver "
                                         "navegador, baixa como fetch_url."),
                          browseProps, QJsonArray{QStringLiteral("url")}));

    QJsonObject restoreProps;
    restoreProps.insert(QStringLiteral("path"), stringProp(QStringLiteral(
        "Arquivo a restaurar (relativo à pasta do projeto ou absoluto).")));
    tools.append(makeTool(QStringLiteral("restore_file"),
                          QStringLiteral("Restaura a versão anterior de um arquivo "
                                         "gravado/baixado pela IA."),
                          restoreProps, QJsonArray{QStringLiteral("path")}));

    for (const McpTool &tool : m_mcpTools) {
        QJsonObject parameters = tool.schema;
        if (parameters.isEmpty()) {
            parameters.insert(QStringLiteral("type"), QStringLiteral("object"));
            parameters.insert(QStringLiteral("properties"), QJsonObject());
        }
        QString description = tool.description;
        if (description.isEmpty())
            description = QStringLiteral("Ferramenta do servidor MCP %1.").arg(tool.server);
        description += QStringLiteral(" (MCP: %1)").arg(tool.server);

        QJsonObject function;
        function.insert(QStringLiteral("name"), mcpPublicName(tool.server, tool.name));
        function.insert(QStringLiteral("description"), description);
        function.insert(QStringLiteral("parameters"), parameters);

        QJsonObject item;
        item.insert(QStringLiteral("type"), QStringLiteral("function"));
        item.insert(QStringLiteral("function"), function);
        tools.append(item);
    }

    for (const McpServer *server : m_mcpServers) {
        if (server->hasResources) {
            QJsonObject properties;
            properties.insert(QStringLiteral("uri"), stringProp(QStringLiteral(
                "URI do recurso MCP a ler.")));
            tools.append(makeTool(mcpPublicName(server->name, QStringLiteral("read_resource")),
                                  QStringLiteral("Lê um recurso (resource) do servidor MCP %1.")
                                      .arg(server->name),
                                  properties, QJsonArray{QStringLiteral("uri")}));
        }
        if (server->hasPrompts) {
            QJsonObject properties;
            properties.insert(QStringLiteral("name"), stringProp(QStringLiteral(
                "Nome do prompt MCP.")));
            properties.insert(QStringLiteral("arguments"), stringProp(QStringLiteral(
                "Argumentos do prompt em JSON (opcional).")));
            tools.append(makeTool(mcpPublicName(server->name, QStringLiteral("get_prompt")),
                                  QStringLiteral("Obtém um prompt do servidor MCP %1.")
                                      .arg(server->name),
                                  properties, QJsonArray{QStringLiteral("name")}));
        }
    }

    return tools;
}


void AppController::attachFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        nullptr, QStringLiteral("Anexar arquivos ou imagens"));
    QStringList added;
    for (const QString &path : files) {
        Attachment attachment;
        QString error;
        if (!makeAttachment(path, &attachment, &error)) {
            emit toast(error);
            continue;
        }
        m_pendingAttachments.append(attachment);
        added.append(attachment.name);
        QVariantMap map;
        map.insert(QStringLiteral("name"), attachment.name);
        map.insert(QStringLiteral("image"), attachment.isImage());
        map.insert(QStringLiteral("path"), attachment.path);
        map.insert(QStringLiteral("url"),
                   QUrl::fromLocalFile(attachment.path).toString());
        m_attachments.append(map);
    }
    if (!added.isEmpty())
        m_api->logActivity(QStringLiteral("attachment_add"),
                           added.join(QStringLiteral(", ")));
    emit attachmentsChanged();
}

void AppController::attachCapturedPixmap(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        emit toast(QStringLiteral("Não foi possível capturar a tela."));
        return;
    }
    const QString dirPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                            + QStringLiteral("/deepseek-chat-captures");
    if (!QDir().mkpath(dirPath)) {
        emit toast(QStringLiteral("Não foi possível preparar a captura."));
        return;
    }
    QString path = QDir(dirPath).filePath(
        QStringLiteral("captura-%1.png").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!pixmap.save(path, "PNG")) {
        emit toast(QStringLiteral("Não foi possível salvar a captura."));
        return;
    }
    if (QFileInfo(path).size() > 10LL * 1024 * 1024) {
        const QString jpeg = path.left(path.size() - 4) + QStringLiteral(".jpg");
        if (!pixmap.save(jpeg, "JPEG", 82)) {
            emit toast(QStringLiteral("Captura grande demais para anexar."));
            return;
        }
        path = jpeg;
    }
    if (!addAttachmentPath(path)) {
        emit toast(QStringLiteral("Não foi possível anexar a captura."));
        return;
    }
    bool imageSupported = true;
    for (const QVariant &value : m_modelsList) {
        const QVariantMap model = value.toMap();
        if (model.value(QStringLiteral("id")).toString() == m_currentModel) {
            imageSupported = model.value(QStringLiteral("supportsImages")).toBool();
            break;
        }
    }
    emit toast(imageSupported
        ? QStringLiteral("Captura anexada à mensagem.")
        : QStringLiteral("Captura anexada; selecione um modelo com visão para analisá-la."));
}

void AppController::captureScreenRegion()
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    new ScreenRegionPicker(screen, [this](const QPixmap &pixmap) {
        attachCapturedPixmap(pixmap);
    });
}

void AppController::captureAppWindow()
{
    if (!m_window || !m_window->screen())
        return;
    attachCapturedPixmap(m_window->screen()->grabWindow(m_window->winId()));
}

bool AppController::addAttachmentPath(const QString &path)
{
    Attachment attachment;
    QString error;
    if (!makeAttachment(path, &attachment, &error)) {
        emit toast(error);
        return false;
    }

    m_pendingAttachments.append(attachment);

    QVariantMap map;
    map.insert(QStringLiteral("name"), attachment.name);
    map.insert(QStringLiteral("image"), attachment.isImage());
    map.insert(QStringLiteral("path"), attachment.path);
    map.insert(QStringLiteral("url"),
               QUrl::fromLocalFile(attachment.path).toString());
    m_attachments.append(map);
    emit attachmentsChanged();

    m_api->logActivity(QStringLiteral("attachment_add"), attachment.name);
    return true;
}

bool AppController::pasteClipboard()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return false;

    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) {
            const QString dir = QStandardPaths::writableLocation(
                QStandardPaths::TempLocation);
            const QString file = dir + QStringLiteral("/deepseek-colar-%1.png")
                                           .arg(QDateTime::currentMSecsSinceEpoch());
            if (image.save(file, "PNG"))
                return addAttachmentPath(file);
        }
    }

    if (mime->hasUrls()) {
        bool added = false;
        const QList<QUrl> urls = mime->urls();
        for (const QUrl &url : urls) {
            if (!url.isLocalFile())
                continue;
            const QFileInfo info(url.toLocalFile());
            if (!info.isFile())
                continue;
            if (addAttachmentPath(info.absoluteFilePath()))
                added = true;
        }
        if (added)
            return true;
    }

    return false;
}

void AppController::attachDroppedUrls(const QVariantList &urls)
{
    for (const QVariant &value : urls) {
        const QUrl url(value.toString());
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        if (info.isFile()) {
            addAttachmentPath(info.absoluteFilePath());
        } else if (info.isDir()) {
            emit toast(QStringLiteral("Pasta não pode ser anexada: %1")
                           .arg(info.fileName()));
        }
    }
}

void AppController::removeAttachment(int index)
{
    if (index < 0 || index >= m_pendingAttachments.size())
        return;
    m_pendingAttachments.remove(index);
    m_attachments.removeAt(index);
    emit attachmentsChanged();
}

void AppController::clearAttachments()
{
    m_pendingAttachments.clear();
    m_attachments.clear();
    emit attachmentsChanged();
}

void AppController::addProjectFiles()
{
    if (m_activeProjectId <= 0) {
        emit toast(QStringLiteral("Abra um projeto primeiro."));
        return;
    }

    const QStringList files = QFileDialog::getOpenFileNames(
        nullptr, QStringLiteral("Adicionar arquivos ao projeto"));
    for (const QString &path : files) {
        const QFileInfo info(path);
        ProjectItem item;
        item.projectId = m_activeProjectId;
        item.kind = isImageSuffix(info.suffix()) ? QStringLiteral("image")
                                                 : QStringLiteral("file");
        item.name = info.fileName();
        item.path = info.absoluteFilePath();
        item.mime = isImageSuffix(info.suffix()) ? mimeForSuffix(info.suffix())
                                                 : QStringLiteral("text/plain");
        item.size = info.size();
        QString error;
        if (!m_api->addProjectItem(item, &error))
            emit toast(error);
    }
    loadProjectDetails(m_activeProjectId);
}

void AppController::removeProjectItem(qint64 id)
{
    QString error;
    if (!m_api->removeProjectItem(id, &error)) {
        emit toast(error);
        return;
    }
    loadProjectDetails(m_activeProjectId);
}

void AppController::attachProjectItem(qint64 id)
{
    QVector<ProjectItem> items;
    if (!m_api->projectItems(m_activeProjectId, &items))
        return;

    for (const ProjectItem &item : items) {
        if (item.id != id)
            continue;
        Attachment attachment;
        QString error;
        if (!makeAttachment(item.path, &attachment, &error)) {
            emit toast(error);
            return;
        }
        m_pendingAttachments.append(attachment);
        QVariantMap map;
        map.insert(QStringLiteral("name"), attachment.name);
        map.insert(QStringLiteral("image"), attachment.isImage());
        map.insert(QStringLiteral("path"), attachment.path);
        map.insert(QStringLiteral("url"),
                   QUrl::fromLocalFile(attachment.path).toString());
        m_attachments.append(map);
        emit attachmentsChanged();
        m_api->logActivity(QStringLiteral("project_attachment"), attachment.name);
        m_viewMode = 0;
        emit viewModeChanged();
        return;
    }
}

void AppController::openWorkdir()
{
    if (m_activeProject.workdir.isEmpty()) {
        emit toast(QStringLiteral("Nenhuma pasta de trabalho definida."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_activeProject.workdir));
}

void AppController::refreshWorkdirFiles()
{
    m_workdirFiles.clear();

    const QString workdir = m_activeProject.workdir;
    if (!workdir.isEmpty()) {
        const QFileInfo info(workdir);
        if (!info.exists() || !info.isDir()) {
            emit toast(QStringLiteral("Pasta não encontrada: %1").arg(workdir));
        } else {
            int count = 0;
            std::function<void(const QString &, int)> walk = [&](const QString &dir,
                                                                 int depth) {
                const QFileInfoList list = QDir(dir).entryInfoList(
                    QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
                for (const QFileInfo &entry : list) {
                    if (count >= kMaxListEntries)
                        return;
                    if (entry.fileName() == QLatin1String(".git")
                        || entry.fileName() == QLatin1String("node_modules")
                        || entry.fileName() == QLatin1String("__pycache__"))
                        continue;

                    QVariantMap map;
                    map.insert(QStringLiteral("name"), entry.fileName());
                    map.insert(QStringLiteral("path"), entry.absoluteFilePath());
                    QString rel = QDir(workdir).relativeFilePath(entry.absoluteFilePath());
                    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
                    map.insert(QStringLiteral("relPath"), rel);
                    map.insert(QStringLiteral("isDir"), entry.isDir());
                    map.insert(QStringLiteral("size"), entry.size());
                    map.insert(QStringLiteral("sizeText"),
                               entry.isDir() ? QString()
                                             : formatSize(entry.size()));
                    map.insert(QStringLiteral("kind"),
                               entry.isDir()
                                   ? QStringLiteral("folder")
                                   : (isImageSuffix(entry.suffix())
                                          ? QStringLiteral("image")
                                          : QStringLiteral("file")));
                    map.insert(QStringLiteral("depth"), depth);
                    m_workdirFiles.append(map);
                    ++count;

                    if (entry.isDir() && depth < 3)
                        walk(entry.absoluteFilePath(), depth + 1);
                    if (count >= kMaxListEntries)
                        return;
                }
            };
            walk(workdir, 0);
        }
    }

    emit workdirFilesChanged();
}

void AppController::attachWorkdirFile(const QString &path)
{
    Attachment attachment;
    QString error;
    if (!makeAttachment(path, &attachment, &error)) {
        emit toast(error);
        return;
    }
    m_pendingAttachments.append(attachment);

    QVariantMap map;
    map.insert(QStringLiteral("name"), attachment.name);
    map.insert(QStringLiteral("image"), attachment.isImage());
    map.insert(QStringLiteral("path"), attachment.path);
    map.insert(QStringLiteral("url"),
               QUrl::fromLocalFile(attachment.path).toString());
    m_attachments.append(map);
    emit attachmentsChanged();

    m_api->logActivity(QStringLiteral("attachment_add"), attachment.name);
    m_viewMode = 0;
    emit viewModeChanged();
}

void AppController::openPath(const QString &path)
{
    if (path.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void AppController::addProjectCommand(const QString &name, const QString &prompt)
{
    QString error;
    if (!m_api->addCommand(m_activeProjectId, name, prompt, &error)) {
        emit toast(error);
        return;
    }
    loadProjectDetails(m_activeProjectId);
}

void AppController::updateProjectCommand(qint64 id, const QString &name,
                                         const QString &prompt)
{
    QString error;
    if (!m_api->updateCommand(id, name, prompt, &error)) {
        emit toast(error);
        return;
    }
    loadProjectDetails(m_activeProjectId);
}

void AppController::removeProjectCommand(qint64 id)
{
    QString error;
    if (!m_api->removeCommand(id, &error)) {
        emit toast(error);
        return;
    }
    loadProjectDetails(m_activeProjectId);
}

void AppController::addGlobalCommand(const QString &name, const QString &prompt)
{
    QString error;
    if (!m_api->addCommand(0, name, prompt, &error)) {
        emit toast(error);
        return;
    }
    loadGlobalCommands();
}

void AppController::updateGlobalCommand(qint64 id, const QString &name,
                                        const QString &prompt)
{
    QString error;
    if (!m_api->updateCommand(id, name, prompt, &error)) {
        emit toast(error);
        return;
    }
    loadGlobalCommands();
}

void AppController::removeGlobalCommand(qint64 id)
{
    QString error;
    if (!m_api->removeCommand(id, &error)) {
        emit toast(error);
        return;
    }
    loadGlobalCommands();
}

void AppController::applyFile(const QString &path, const QString &content)
{
    if (m_activeProjectId <= 0 || m_activeProject.workdir.isEmpty()) {
        emit toast(QStringLiteral("O chat precisa estar em um projeto com pasta definida."));
        return;
    }

    const QDir base(m_activeProject.workdir);
    const QString target = QDir::cleanPath(base.absoluteFilePath(path));
    if (!target.startsWith(base.absolutePath() + QLatin1Char('/'))) {
        emit toast(QStringLiteral("Caminho fora da pasta do projeto: %1").arg(path));
        return;
    }

    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit toast(QStringLiteral("Não foi possível gravar %1").arg(target));
        return;
    }
    file.write(content.toUtf8());
    file.close();

    m_api->logActivity(QStringLiteral("file_write"), target);
    emit toast(QStringLiteral("Arquivo salvo: %1").arg(target));
}

void AppController::refreshActivities()
{
    QVector<Activity> activities;
    if (!m_api->activities(&activities)) {
        emit toast(QStringLiteral("Não foi possível carregar as atividades."));
        return;
    }
    m_activities.clear();
    for (const Activity &activity : activities) {
        QVariantMap map;
        map.insert(QStringLiteral("date"),
                   activity.createdAt.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")));
        map.insert(QStringLiteral("action"), activity.action);
        map.insert(QStringLiteral("details"), activity.details);
        m_activities.append(map);
    }
    emit activitiesChanged();
}

void AppController::logClientActivity(const QString &action, const QString &details)
{
    m_api->logActivity(action, details);
}

void AppController::saveSettings(const QVariantMap &data)
{
    Config &config = Config::instance();
    config.ui().sidebarFontSize = qBound(9, data.value(QStringLiteral("sidebarFontSize"), config.ui().sidebarFontSize).toInt(), 22);
    config.ui().codeFontSize = qBound(9, data.value(QStringLiteral("codeFontSize"), config.ui().codeFontSize).toInt(), 28);
    const QString uiFamily = data.value(QStringLiteral("fontFamily"), config.ui().fontFamily).toString().trimmed();
    const QString chatFamily = data.value(QStringLiteral("chatFontFamily"), config.ui().chatFontFamily).toString().trimmed();
    if (!uiFamily.isEmpty()) config.ui().fontFamily = uiFamily;
    if (!chatFamily.isEmpty()) config.ui().chatFontFamily = chatFamily;

    const QString newUrl = data.value(QStringLiteral("apiUrl"),
                                      config.api().baseUrl).toString().trimmed();
    if (!newUrl.isEmpty())
        config.api().baseUrl = newUrl;

    config.ui().theme = data.value(QStringLiteral("theme"), config.ui().theme).toString();
    config.ui().fontSize = data.value(QStringLiteral("fontSize"),
                                      config.ui().fontSize).toInt();
    config.ui().chatFontSize = data.value(QStringLiteral("chatFontSize"),
                                          config.ui().chatFontSize).toInt();
    config.ui().enterToSend = data.value(QStringLiteral("enterToSend"),
                                         config.ui().enterToSend).toBool();
    config.ui().showReasoning = data.value(QStringLiteral("showReasoning"),
                                           config.ui().showReasoning).toBool();
    config.ui().permissionMode = data.value(QStringLiteral("permissionMode"),
                                            config.ui().permissionMode).toString();
    config.ui().globalInstructions = data.value(QStringLiteral("globalInstructions"),
                                                config.ui().globalInstructions).toString();
    config.ui().notifyOnFinish = data.value(QStringLiteral("notifyOnFinish"),
                                            config.ui().notifyOnFinish).toBool();
    config.ui().closeToTray = data.value(QStringLiteral("closeToTray"),
                                         config.ui().closeToTray).toBool();

    config.deepseek().defaultModel = data.value(QStringLiteral("defaultModel"),
                                                config.deepseek().defaultModel).toString();

    config.audio().ttsEnabled = data.value(QStringLiteral("ttsEnabled"),
                                           config.audio().ttsEnabled).toBool();
    config.audio().ttsVoice = data.value(QStringLiteral("ttsVoice"),
                                         config.audio().ttsVoice).toString();
    config.audio().ttsRate = data.value(QStringLiteral("ttsRate"),
                                        config.audio().ttsRate).toInt();
    config.audio().sttEnabled = data.value(QStringLiteral("sttEnabled"),
                                           config.audio().sttEnabled).toBool();
    config.audio().sttCommand = data.value(QStringLiteral("sttCommand"),
                                           config.audio().sttCommand).toString();

    config.pricing().flashInputHit = data.value(QStringLiteral("flashInputHit"),
                                                config.pricing().flashInputHit).toDouble();
    config.pricing().flashInputMiss = data.value(QStringLiteral("flashInputMiss"),
                                                 config.pricing().flashInputMiss).toDouble();
    config.pricing().flashOutput = data.value(QStringLiteral("flashOutput"),
                                              config.pricing().flashOutput).toDouble();
    config.pricing().proInputHit = data.value(QStringLiteral("proInputHit"),
                                              config.pricing().proInputHit).toDouble();
    config.pricing().proInputMiss = data.value(QStringLiteral("proInputMiss"),
                                               config.pricing().proInputMiss).toDouble();
    config.pricing().proOutput = data.value(QStringLiteral("proOutput"),
                                            config.pricing().proOutput).toDouble();
    config.pricing().peakEnabled = data.value(QStringLiteral("peakEnabled"),
                                              config.pricing().peakEnabled).toBool();

    QString error;
    if (!config.save(&error)) {
        emit toast(error);
        return;
    }

    m_api->setBaseUrl(config.api().baseUrl);

    if (m_theme)
        m_theme->applyFromConfig();

    configureTts();
    refreshModels();
    refreshUsage();
    emit uiSettingsChanged();
    emit toast(QStringLiteral("Configurações salvas."));
}

void AppController::testConnection(const QString &url)
{
    auto *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(url.trimmed() + QStringLiteral("/health")));
    request.setTransferTimeout(15000);

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager] {
        const QByteArray data = reply->readAll();
        const auto error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        manager->deleteLater();

        if (error != QNetworkReply::NoError) {
            emit connectionTestDone(QStringLiteral("Falhou: ") + errorText);
            return;
        }
        emit connectionTestDone(QStringLiteral("OK — ") + QString::fromUtf8(data).trimmed());
    });
}

void AppController::changeDisplayName(const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    QString error;
    if (!m_api->updateProfile(name.trimmed(), &error)) {
        emit toast(error);
        return;
    }
    m_user.displayName = name.trimmed();
    Config::instance().api().displayName = name.trimmed();
    Config::instance().save();
    emit userChanged();
    emit toast(QStringLiteral("Nome atualizado."));
}

void AppController::changePassword(const QString &oldPassword,
                                   const QString &newPassword)
{
    QString error;
    if (!m_api->changePassword(oldPassword, newPassword, &error)) {
        emit toast(error);
        return;
    }
    emit toast(QStringLiteral("Senha alterada com sucesso."));
}

void AppController::openConfigFile()
{
    const QString path = Config::instance().filePath();
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

int AppController::sidebarWidth() const { return Config::instance().ui().sidebarWidth; }
bool AppController::sidebarCollapsed() const { return Config::instance().ui().sidebarCollapsed; }

void AppController::saveNavigationLayout(int width, bool collapsed)
{
    auto &config = Config::instance();
    config.ui().sidebarWidth = qBound(220, width, 480);
    config.ui().sidebarCollapsed = collapsed;
    QString error;
    if (!config.save(&error))
        emit toast(QStringLiteral("Falha ao salvar o tamanho da barra lateral: ") + error);
    emit uiSettingsChanged();
}

QVariantMap AppController::currentSettings() const
{
    const Config &config = Config::instance();
    QVariantMap map;
    map.insert(QStringLiteral("apiUrl"), config.api().baseUrl);
    map.insert(QStringLiteral("theme"), config.ui().theme);
    map.insert(QStringLiteral("fontSize"), config.ui().fontSize);
    map.insert(QStringLiteral("chatFontSize"), config.ui().chatFontSize);
    map.insert(QStringLiteral("sidebarFontSize"), config.ui().sidebarFontSize);
    map.insert(QStringLiteral("codeFontSize"), config.ui().codeFontSize);
    map.insert(QStringLiteral("fontFamily"), config.ui().fontFamily);
    map.insert(QStringLiteral("chatFontFamily"), config.ui().chatFontFamily);
    map.insert(QStringLiteral("enterToSend"), config.ui().enterToSend);
    map.insert(QStringLiteral("showReasoning"), config.ui().showReasoning);
    map.insert(QStringLiteral("permissionMode"), config.ui().permissionMode);
    map.insert(QStringLiteral("globalInstructions"), config.ui().globalInstructions);
    map.insert(QStringLiteral("notifyOnFinish"), config.ui().notifyOnFinish);
    map.insert(QStringLiteral("closeToTray"), config.ui().closeToTray);
    map.insert(QStringLiteral("defaultModel"), config.deepseek().defaultModel);
    map.insert(QStringLiteral("ttsEnabled"), config.audio().ttsEnabled);
    map.insert(QStringLiteral("ttsVoice"), config.audio().ttsVoice);
    map.insert(QStringLiteral("ttsRate"), config.audio().ttsRate);
    map.insert(QStringLiteral("sttEnabled"), config.audio().sttEnabled);
    map.insert(QStringLiteral("sttCommand"), config.audio().sttCommand);
    map.insert(QStringLiteral("flashInputHit"), config.pricing().flashInputHit);
    map.insert(QStringLiteral("flashInputMiss"), config.pricing().flashInputMiss);
    map.insert(QStringLiteral("flashOutput"), config.pricing().flashOutput);
    map.insert(QStringLiteral("proInputHit"), config.pricing().proInputHit);
    map.insert(QStringLiteral("proInputMiss"), config.pricing().proInputMiss);
    map.insert(QStringLiteral("proOutput"), config.pricing().proOutput);
    map.insert(QStringLiteral("peakEnabled"), config.pricing().peakEnabled);
    return map;
}

QString AppController::chooseDirectory(const QString &title)
{
    return QFileDialog::getExistingDirectory(nullptr, title);
}

QStringList AppController::ttsVoices() const
{
    QStringList names;
    const QList<QVoice> voices = m_tts->availableVoices();
    for (const QVoice &voice : voices)
        names.append(voice.name());
    return names;
}

void AppController::configureTts()
{
    const AudioConfig &audio = Config::instance().audio();
    m_tts->setRate(audio.ttsRate / 100.0);
    if (audio.ttsVoice.isEmpty())
        return;

    const QList<QVoice> voices = m_tts->availableVoices();
    for (const QVoice &voice : voices) {
        if (voice.name() == audio.ttsVoice) {
            m_tts->setVoice(voice);
            break;
        }
    }
}

void AppController::testVoice(const QString &text)
{
    m_tts->stop();
    m_tts->say(text);
}

void AppController::stopSpeaking()
{
    m_tts->stop();
}

void AppController::startRecording()
{
    if (m_recording)
        return;

    if (!Config::instance().audio().sttEnabled) {
        emit toast(QStringLiteral("Ative o ditado por voz em Configurações."));
        return;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        emit toast(QStringLiteral("Nenhum microfone encontrado."));
        return;
    }

    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        format = device.preferredFormat();
        format.setChannelCount(1);
        if (format.sampleFormat() != QAudioFormat::Int16) {
            format.setSampleFormat(QAudioFormat::Int16);
            if (!device.isFormatSupported(format)) {
                emit toast(QStringLiteral("O microfone não suporta gravação PCM 16 bits."));
                return;
            }
        }
    }

    m_recordSampleRate = format.sampleRate();
    m_recordChannels = format.channelCount();

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_recordFile = dir + QStringLiteral("/deepseek-ditado.wav");

    m_audioData.clear();
    m_audioBuffer = new QBuffer(&m_audioData, this);
    m_audioBuffer->open(QIODevice::WriteOnly);

    m_audioSource = new QAudioSource(device, format, this);
    m_audioSource->start(m_audioBuffer);

    m_recording = true;
    emit recordingChanged();
}

void AppController::stopRecording()
{
    if (!m_recording)
        return;

    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }
    if (m_audioBuffer) {
        m_audioBuffer->close();
        m_audioBuffer->deleteLater();
        m_audioBuffer = nullptr;
    }

    m_recording = false;
    emit recordingChanged();

    const QString file = m_recordFile;
    if (m_audioData.isEmpty()) {
        emit toast(QStringLiteral("Nada foi gravado."));
        return;
    }
    if (!writeWavFile(file, m_audioData, m_recordSampleRate, m_recordChannels)) {
        emit toast(QStringLiteral("Não foi possível gravar o áudio."));
        return;
    }

    transcribeRecording(file);
}

void AppController::transcribeRecording(const QString &file)
{
    QString command = Config::instance().audio().sttCommand;
    command.replace(QStringLiteral("{file}"), file);
    if (command.trimmed().isEmpty()) {
        emit toast(QStringLiteral("Configure o comando de transcrição em Configurações."));
        return;
    }

    const QStringList parts = QProcess::splitCommand(command);
    if (parts.isEmpty()) {
        emit toast(QStringLiteral("Comando de transcrição inválido."));
        return;
    }

    emit toast(QStringLiteral("Transcrevendo áudio..."));

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, file](int exitCode, QProcess::ExitStatus) {
                QString text = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
                if (text.isEmpty())
                    text = QString::fromUtf8(process->readAllStandardError()).trimmed();
                if (text.isEmpty()) {
                    QFile txtFile(file + QStringLiteral(".txt"));
                    if (txtFile.open(QIODevice::ReadOnly))
                        text = QString::fromUtf8(txtFile.readAll()).trimmed();
                }
                process->deleteLater();

                if (text.isEmpty() || exitCode != 0) {
                    emit toast(QStringLiteral("Transcrição falhou. Verifique o comando e "
                                              "o modelo do Whisper."));
                    return;
                }
                emit composerTextRequested(text);
                emit toast(QStringLiteral("Transcrição pronta."));
                m_api->logActivity(QStringLiteral("dictation"),
                                   QStringLiteral("%1 caracteres").arg(text.size()));
            });
    process->start(parts.first(), parts.mid(1));
}

void AppController::refreshBalance()
{
    if (m_user.id == 0) {
        m_balanceInfo.clear();
        emit balanceChanged();
        return;
    }

    QJsonObject object;
    QString error;
    if (!m_api->balance(&object, &error)) {
        emit toast(QStringLiteral("Erro ao consultar saldo: ") + error);
        return;
    }

    QStringList parts;
    for (const QJsonValue &value :
         object.value(QStringLiteral("balance_infos")).toArray()) {
        const QJsonObject info = value.toObject();
        parts.append(QStringLiteral("%1 %2")
                         .arg(info.value(QStringLiteral("total_balance")).toString(),
                              info.value(QStringLiteral("currency")).toString()));
    }

    QVariantMap map;
    map.insert(QStringLiteral("available"),
               object.value(QStringLiteral("is_available")).toBool());
    map.insert(QStringLiteral("text"),
               parts.isEmpty() ? QString() : parts.join(QStringLiteral(" / ")));
    m_balanceInfo = map;
    emit balanceChanged();
}

QString AppController::estimateMessageCost(const QString &text) const
{
    const qint64 inputTokens = qMax<qint64>(1, text.toUtf8().size() / 4);
    // A short answer is the least surprising estimate before the model replies.
    const qint64 outputTokens = qMax<qint64>(100, inputTokens / 2);
    const QString model = m_currentModel.isEmpty()
        ? Config::instance().deepseek().defaultModel : m_currentModel;
    const double cost = costForModel(model, inputTokens, outputTokens, 0);
    return QStringLiteral("Estimativa: ~%1 tokens · US$ %2")
        .arg(QLocale().toString(inputTokens + outputTokens))
        .arg(QLocale().toString(cost, 'f', 4));
}

void AppController::refreshUsage()
{
    if (m_user.id == 0) {
        m_usage.clear();
        emit usageChanged();
        return;
    }

    UsageTotals conversation;
    QVector<UsageTotals> byModel;
    if (!m_api->usage(m_currentConversationId, &conversation, &byModel)) {
        emit usageChanged();
        return;
    }

    QString model = m_currentModel;
    if (model.isEmpty())
        model = Config::instance().deepseek().defaultModel;

    const double conversationCost = costForModel(model, conversation.prompt,
                                                 conversation.completion,
                                                 conversation.cached);

    double totalCost = 0;
    qint64 totalTokens = 0;
    for (const UsageTotals &totals : byModel) {
        totalCost += costForModel(totals.model, totals.prompt, totals.completion,
                                  totals.cached);
        totalTokens += totals.tokens();
    }

    int contextWindow = 1000000;
    for (const QVariant &value : m_modelsList) {
        const QVariantMap info = value.toMap();
        if (info.value(QStringLiteral("id")).toString() == model) {
            const int value = info.value(QStringLiteral("contextWindow")).toInt();
            if (value > 0)
                contextWindow = value;
            break;
        }
    }

    const double percent = contextWindow > 0
        ? (static_cast<double>(conversation.tokens()) / contextWindow) * 100.0
        : 0.0;

    QVariantMap map;
    map.insert(QStringLiteral("cost"), conversationCost);
    map.insert(QStringLiteral("costText"),
               QStringLiteral("US$ %1").arg(conversationCost, 0, 'f', 4));
    map.insert(QStringLiteral("totalCost"), totalCost);
    map.insert(QStringLiteral("totalCostText"),
               QStringLiteral("US$ %1").arg(totalCost, 0, 'f', 2));
    map.insert(QStringLiteral("tokens"), conversation.tokens());
    map.insert(QStringLiteral("tokensText"),
               QLocale().toString(conversation.tokens()));
    map.insert(QStringLiteral("inputTokens"), conversation.prompt);
    map.insert(QStringLiteral("outputTokens"), conversation.completion);
    map.insert(QStringLiteral("cachedTokens"), conversation.cached);
    map.insert(QStringLiteral("percent"), percent);
    map.insert(QStringLiteral("percentText"),
               QString::number(percent, 'f', 1) + QStringLiteral("%"));
    map.insert(QStringLiteral("totalTokens"), totalTokens);
    map.insert(QStringLiteral("totalTokensText"), QLocale().toString(totalTokens));
    map.insert(QStringLiteral("peak"), isPeakNow());
    map.insert(QStringLiteral("contextWindow"), contextWindow);

    m_usage = map;
    emit usageChanged();
}

double AppController::costForModel(const QString &model, qint64 prompt,
                                   qint64 completion, qint64 cached) const
{
    const PricingConfig &pricing = Config::instance().pricing();

    double inputHit = pricing.flashInputHit;
    double inputMiss = pricing.flashInputMiss;
    double output = pricing.flashOutput;
    if (model.contains(QLatin1String("pro"), Qt::CaseInsensitive)) {
        inputHit = pricing.proInputHit;
        inputMiss = pricing.proInputMiss;
        output = pricing.proOutput;
    }

    const qint64 missTokens = qMax<qint64>(0, prompt - cached);
    double cost = (static_cast<double>(cached) / 1e6) * inputHit
        + (static_cast<double>(missTokens) / 1e6) * inputMiss
        + (static_cast<double>(completion) / 1e6) * output;

    if (pricing.peakEnabled && isPeakNow())
        cost *= pricing.peakMultiplier;
    return cost;
}

bool AppController::isPeakNow() const
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const int hour = now.time().hour();
    const int day = now.date().dayOfWeek();
    if (day < 1 || day > 5)
        return false;
    return (hour >= 1 && hour < 4) || (hour >= 6 && hour < 10);
}
