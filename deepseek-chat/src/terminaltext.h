#pragma once
#include <QByteArray>
#include <QStringConverter>

// Linear terminal transcript: omit ANSI/OSC control envelopes, decode UTF-8
// incrementally, and leave screen-oriented applications to a full emulator.
class TerminalText {
public:
    QString consume(const QByteArray &input) {
        QByteArray plain;
        for (const unsigned char ch : input) {
            if (m_state == Escape) {
                m_state = ch == '[' ? Csi : ch == ']' ? Osc : Normal;
            } else if (m_state == Csi) {
                if (ch >= 0x40 && ch <= 0x7e) m_state = Normal;
            } else if (m_state == Osc) {
                if (ch == 7) m_state = Normal;
                else if (ch == 27) m_state = OscEscape;
            } else if (m_state == OscEscape) {
                m_state = ch == '\\' ? Normal : Osc;
            } else if (ch == 27) {
                m_state = Escape;
            } else if (ch == '\n' || ch == '\t' || ch >= 32) {
                if (ch != 127) plain.append(char(ch));
            }
        }
        return m_decoder(plain);
    }
    void reset() { m_state = Normal; m_decoder.resetState(); }
private:
    enum State { Normal, Escape, Csi, Osc, OscEscape } m_state = Normal;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
};
