#pragma once

#include <QByteArray>

// Drain process pipes continuously while retaining a bounded head and tail.
class BoundedOutput {
public:
    explicit BoundedOutput(int limit = 100 * 1024, int tail = 20 * 1024)
        : m_headLimit(limit - tail), m_tailLimit(tail) {}

    void append(const QByteArray &bytes)
    {
        m_total += bytes.size();
        const int toHead = qMin(qMax(0, m_headLimit - int(m_head.size())), int(bytes.size()));
        m_head.append(bytes.constData(), toHead);
        m_tail.append(bytes.mid(toHead));
        if (m_tail.size() > m_tailLimit)
            m_tail = m_tail.right(m_tailLimit);
    }
    bool truncated() const { return m_total > m_headLimit + m_tailLimit; }
    QByteArray data() const
    {
        return m_head + (truncated() ? QByteArray("\n\n[... saída truncada ...]\n\n") : QByteArray()) + m_tail;
    }
private:
    int m_headLimit;
    int m_tailLimit;
    qint64 m_total = 0;
    QByteArray m_head;
    QByteArray m_tail;
};
