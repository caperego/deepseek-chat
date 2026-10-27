#include "appcontroller.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

// Agent tools that drive the integrated browser shown in the side panel, so the
// user watches every page the agent opens, clicks or fills. The controller builds
// the scripts; BrowserPane.qml runs them and replies with agentBrowserReply().

namespace {

constexpr int kBrowserTimeoutMs = 45000;
constexpr int kPageTextLimit = 20000;

// A JavaScript string literal (JSON escaping is valid JavaScript).
QString jsString(const QString &text)
{
    const QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

// Visible text, links and form fields, with selectors the agent can reuse.
const char *kSnapshotScript = R"JS((function () {
  function selectorFor(e) {
    if (e.id) return '#' + CSS.escape(e.id);
    var tag = e.tagName.toLowerCase();
    if (e.name) return tag + '[name="' + e.name.replace(/"/g, '\\"') + '"]';
    if (document.querySelectorAll(tag).length === 1) return tag;
    // Mark the element so the selector stays valid until the page changes.
    window.__agentFields = window.__agentFields || 0;
    if (!e.dataset.agentField) e.dataset.agentField = String(++window.__agentFields);
    return '[data-agent-field="' + e.dataset.agentField + '"]';
  }
  function visible(e) { var r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0; }
  var links = [], seen = {};
  document.querySelectorAll('a[href]').forEach(function (a) {
    var text = (a.innerText || a.title || a.getAttribute('aria-label') || '').trim().replace(/\s+/g, ' ');
    if (!text || seen[a.href] || links.length >= 80 || !visible(a)) return;
    seen[a.href] = true;
    links.push({ text: text.slice(0, 80), href: a.href });
  });
  var fields = [];
  document.querySelectorAll('input:not([type=hidden]), textarea, select, button').forEach(function (e) {
    if (fields.length >= 40 || !visible(e)) return;
    fields.push({ selector: selectorFor(e), tag: e.tagName.toLowerCase(), type: e.type || '',
                  label: (e.getAttribute('aria-label') || e.placeholder || e.innerText || e.value || e.name || '').trim().slice(0, 60) });
  });
  return { content: document.body ? document.body.innerText : '', links: links, fields: fields };
})())JS";

QString clickScript(const QString &selector, const QString &text)
{
    return QStringLiteral(R"JS((function (selector, text) {
  var target = null;
  if (selector) target = document.querySelector(selector);
  if (!target && text) {
    var wanted = text.trim().toLowerCase(), partial = null;
    var nodes = document.querySelectorAll('a, button, input[type=submit], input[type=button], [role=button], [role=link], [role=tab], [onclick], summary, label');
    for (var i = 0; i < nodes.length; ++i) {
      var e = nodes[i], r = e.getBoundingClientRect();
      if (r.width <= 0 || r.height <= 0) continue;
      var label = (e.innerText || e.value || e.getAttribute('aria-label') || e.title || '').trim().replace(/\s+/g, ' ').toLowerCase();
      if (label === wanted) { target = e; break; }
      if (!partial && label.indexOf(wanted) >= 0) partial = e;
    }
    target = target || partial;
  }
  if (!target) return { clicked: false, error: 'Elemento não encontrado.' };
  target.scrollIntoView({ block: 'center' });
  target.click();
  return { clicked: true, tag: target.tagName.toLowerCase(), text: (target.innerText || target.value || '').trim().slice(0, 80) };
})(%1, %2))JS").arg(jsString(selector), jsString(text));
}

QString typeScript(const QString &selector, const QString &text, bool submit)
{
    return QStringLiteral(R"JS((function (selector, text, submit) {
  var e = document.querySelector(selector);
  if (!e) return { typed: false, error: 'Campo não encontrado: ' + selector };
  e.scrollIntoView({ block: 'center' });
  e.focus();
  // The native setter keeps frameworks that track value changes in sync.
  var setter = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(e), 'value');
  if (setter && setter.set) setter.set.call(e, text); else e.value = text;
  e.dispatchEvent(new Event('input', { bubbles: true }));
  e.dispatchEvent(new Event('change', { bubbles: true }));
  if (submit) {
    if (e.form) { if (e.form.requestSubmit) e.form.requestSubmit(); else e.form.submit(); }
    else {
      e.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', code: 'Enter', keyCode: 13, bubbles: true }));
      e.dispatchEvent(new KeyboardEvent('keyup', { key: 'Enter', code: 'Enter', keyCode: 13, bubbles: true }));
    }
  }
  return { typed: true, submitted: !!submit };
})(%1, %2, %3))JS").arg(jsString(selector), jsString(text), submit ? QStringLiteral("true") : QStringLiteral("false"));
}

} // namespace

bool AppController::isAgentBrowserTool(const QString &name)
{
    return name == QLatin1String("browser_open") || name == QLatin1String("browser_read")
        || name == QLatin1String("browser_click") || name == QLatin1String("browser_type")
        || name == QLatin1String("browser_back");
}

QVariantMap AppController::agentBrowserRequestFor(const QString &tool, const QJsonObject &args,
                                                 QString *error, QString *detail)
{
    QVariantMap request{{"snapshot", QString::fromUtf8(kSnapshotScript)}};
    if (tool == QLatin1String("browser_open")) {
        const QUrl url(args.value(QStringLiteral("url")).toString().trimmed());
        if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
            *error = QStringLiteral("URL inválida: use http ou https.");
        request.insert(QStringLiteral("url"), url.toString());
        request.insert(QStringLiteral("newTab"), args.value(QStringLiteral("new_tab")).toBool());
        *detail = url.toString();
    } else if (tool == QLatin1String("browser_click")) {
        const QString selector = args.value(QStringLiteral("selector")).toString();
        const QString text = args.value(QStringLiteral("text")).toString();
        if (selector.trimmed().isEmpty() && text.trimmed().isEmpty())
            *error = QStringLiteral("Informe text ou selector.");
        request.insert(QStringLiteral("script"), clickScript(selector, text));
        *detail = text.isEmpty() ? selector : text;
    } else if (tool == QLatin1String("browser_type")) {
        const QString selector = args.value(QStringLiteral("selector")).toString();
        if (selector.trimmed().isEmpty())
            *error = QStringLiteral("Informe o selector do campo.");
        request.insert(QStringLiteral("script"), typeScript(selector, args.value(QStringLiteral("text")).toString(),
                                                            args.value(QStringLiteral("submit")).toBool()));
        *detail = selector;
    } else if (tool == QLatin1String("browser_back")) {
        request.insert(QStringLiteral("back"), true);
    }
    return request;
}

void AppController::agentBrowserCall(AgentRun *run, const ToolCall &call)
{
    QString error;
    QString detail;
    const QJsonObject args = parseArguments(call, &error);
    const QVariantMap request = error.isEmpty()
        ? agentBrowserRequestFor(call.name, args, &error, &detail) : QVariantMap();
    if (!error.isEmpty()) {
        finishToolCall(run, QString::fromUtf8(QJsonDocument(QJsonObject{{"error", error}}).toJson(QJsonDocument::Compact)),
                       QStringLiteral("⚙ %1: %2").arg(call.name, error));
        return;
    }

    const int id = ++m_browserCallId;
    m_browserCalls.insert(id, {run, call.name});
    if (displayingRun(run))
        m_messages->updateLastTool(QStringLiteral("⚙ %1: %2…").arg(call.name,
            detail.isEmpty() ? QStringLiteral("navegador integrado") : detail), QString());
    QTimer::singleShot(kBrowserTimeoutMs, this, [this, id] {
        if (!m_browserCalls.contains(id)) return;
        agentBrowserReply(id, {{"error", QStringLiteral("O navegador integrado não respondeu a tempo.")}});
    });
    emit agentBrowserRequest(id, request);
}

void AppController::agentBrowserReply(int requestId, const QVariantMap &reply)
{
    emit agentBrowserReplied(requestId, reply);
    const auto pending = m_browserCalls.take(requestId);
    AgentRun *run = pending.first;
    if (!run || !m_runs.contains(run) || run->aborted)
        return;

    QJsonObject result = QJsonObject::fromVariantMap(reply);
    QString content = result.value(QStringLiteral("content")).toString();
    if (content.size() > kPageTextLimit) {
        content = content.left(kPageTextLimit) + QStringLiteral("\n[texto truncado]");
        result.insert(QStringLiteral("content"), content);
    }
    const QString title = result.value(QStringLiteral("title")).toString();
    const QString errorText = result.value(QStringLiteral("error")).toString();
    const QString summary = errorText.isEmpty()
        ? QStringLiteral("⚙ %1: %2").arg(pending.second,
              title.isEmpty() ? result.value(QStringLiteral("url")).toString() : title)
        : QStringLiteral("⚙ %1: %2").arg(pending.second, errorText);
    finishToolCall(run, QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)), summary);
}

void AppController::dropBrowserCallsFor(AgentRun *run)
{
    for (auto it = m_browserCalls.begin(); it != m_browserCalls.end();) {
        if (it.value().first == run) it = m_browserCalls.erase(it);
        else ++it;
    }
}
