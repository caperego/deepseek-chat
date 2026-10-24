#pragma once

#include <QRegularExpression>
#include <QString>

namespace ToolProtocol {
// Recognize leaked execution envelopes, not documented examples inside fences.
// This is diagnostic only: never interpret or execute commands from this text.
inline int leakedCallStart(const QString &text)
{
    static const QRegularExpression tag(QStringLiteral(
        R"(<(?:｜|\|)DSML(?:｜|\|)\s*(?:function_calls|tool_calls|calls|invoke)(?=\s|>))"));
    bool fenced = false;
    QString fence;
    int offset = 0;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!fenced && (trimmed.startsWith(QLatin1String("```"))
                        || trimmed.startsWith(QLatin1String("~~~")))) {
            fenced = true;
            fence = trimmed.left(3);
        } else if (fenced && trimmed.startsWith(fence)) {
            fenced = false;
        } else if (!fenced) {
            auto matches = tag.globalMatch(line);
            while (matches.hasNext()) {
                const auto match = matches.next();
                // Inline examples delimited by backticks are also literal.
                if (line.left(match.capturedStart()).count(QLatin1Char('`')) % 2 == 0)
                    return offset + match.capturedStart();
            }
        }
        offset += line.size() + 1;
    }
    return -1;
}

inline QString safeAssistantContent(const QString &text)
{
    const int start = leakedCallStart(text);
    if (start < 0) return text;
    return text.left(start).trimmed() + QStringLiteral(
        "\n[Uma tentativa anterior de chamada fora do formato não foi executada. "
        "Use as ferramentas declaradas pela API para verificar o estado real.]");
}
}
