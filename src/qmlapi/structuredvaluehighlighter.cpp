#include "structuredvaluehighlighter.h"

#include <QColor>
#include <QList>
#include <QPair>

/*!
 * \class StructuredValueHighlighter
 * \brief Colors the JSON or XML text of the structured Value panel.
 */

namespace {

/*!
 * \internal
 * \brief Returns whether \a index falls inside any [start, end) span in \a spans.
 *
 * Used by the JSON pass to skip number, keyword, and punctuation matches that lie
 * inside a quoted string (for example the digits of a type name such as "Int32").
 */
bool isInsideSpan(int index, const QList<QPair<int, int>> &spans)
{
    for (const QPair<int, int> &span : spans) {
        if (index >= span.first && index < span.second)
            return true;
    }
    return false;
}

} // namespace

/*!
 * \brief Creates a highlighter attached to \a document with the JSON grammar active.
 *
 * The QTextCharFormat colors approximate a Visual Studio Code dark theme and the
 * QRegularExpression rules are built once here because highlightBlock() runs for
 * every line on each content change.
 */
StructuredValueHighlighter::StructuredValueHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    m_keyFormat.setForeground(QColor(0x9C, 0xDC, 0xFE));
    m_stringFormat.setForeground(QColor(0xCE, 0x91, 0x78));
    m_numberFormat.setForeground(QColor(0xB5, 0xCE, 0xA8));
    m_keywordFormat.setForeground(QColor(0x56, 0x9C, 0xD6));
    m_punctuationFormat.setForeground(QColor(0xD4, 0xD4, 0xD4));
    m_tagFormat.setForeground(QColor(0x56, 0x9C, 0xD6));
    m_attributeFormat.setForeground(QColor(0x9C, 0xDC, 0xFE));

    m_stringRegex = QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")"));
    m_numberRegex =
        QRegularExpression(QStringLiteral(R"(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)"));
    m_keywordRegex = QRegularExpression(QStringLiteral(R"(\b(?:true|false|null)\b)"));
    m_punctuationRegex = QRegularExpression(QStringLiteral(R"([\{\}\[\]:,])"));

    m_xmlTagRegex = QRegularExpression(QStringLiteral(R"(</?[A-Za-z_?][\w.:\-]*|\??/?>)"));
    m_xmlAttributeRegex = QRegularExpression(QStringLiteral(R"([A-Za-z_][\w.:\-]*(?=\s*=))"));
    m_xmlValueRegex = QRegularExpression(QStringLiteral(R"("[^"]*")"));
}

/*!
 * \brief Switches the active highlighting language and re-highlights on change.
 */
void StructuredValueHighlighter::setLanguage(Language language)
{
    if (m_language == language)
        return;

    m_language = language;
    rehighlight();
}

/*!
 * \brief Returns the active highlighting language.
 */
StructuredValueHighlighter::Language StructuredValueHighlighter::language() const
{
    return m_language;
}

/*!
 * \brief Dispatches highlighting of \a text to the active language grammar.
 */
void StructuredValueHighlighter::highlightBlock(const QString &text)
{
    switch (m_language) {
    case Language::Xml:
        highlightXml(text);
        return;
    case Language::Json:
        break;
    }
    highlightJson(text);
}

/*!
 * \brief Highlights the JSON \a text, distinguishing keys from string values.
 *
 * Strings are colored first and their spans recorded; a string immediately
 * followed by a colon is a property name, otherwise a string value. Numbers,
 * literals, and punctuation are then colored only where they do not fall inside a
 * recorded string span, so digits inside a quoted type name are not mis-colored.
 */
void StructuredValueHighlighter::highlightJson(const QString &text)
{
    QList<QPair<int, int>> stringSpans;

    QRegularExpressionMatchIterator stringIt = m_stringRegex.globalMatch(text);
    while (stringIt.hasNext()) {
        const QRegularExpressionMatch match = stringIt.next();
        const int start = match.capturedStart();
        const int length = match.capturedLength();
        stringSpans.append({start, start + length});

        int next = start + length;
        while (next < text.size() && text.at(next).isSpace())
            ++next;
        const bool isKey = next < text.size() && text.at(next) == QLatin1Char(':');
        setFormat(start, length, isKey ? m_keyFormat : m_stringFormat);
    }

    QRegularExpressionMatchIterator numberIt = m_numberRegex.globalMatch(text);
    while (numberIt.hasNext()) {
        const QRegularExpressionMatch match = numberIt.next();
        if (!isInsideSpan(match.capturedStart(), stringSpans))
            setFormat(match.capturedStart(), match.capturedLength(), m_numberFormat);
    }

    QRegularExpressionMatchIterator keywordIt = m_keywordRegex.globalMatch(text);
    while (keywordIt.hasNext()) {
        const QRegularExpressionMatch match = keywordIt.next();
        if (!isInsideSpan(match.capturedStart(), stringSpans))
            setFormat(match.capturedStart(), match.capturedLength(), m_keywordFormat);
    }

    QRegularExpressionMatchIterator punctuationIt = m_punctuationRegex.globalMatch(text);
    while (punctuationIt.hasNext()) {
        const QRegularExpressionMatch match = punctuationIt.next();
        if (!isInsideSpan(match.capturedStart(), stringSpans))
            setFormat(match.capturedStart(), match.capturedLength(), m_punctuationFormat);
    }
}

/*!
 * \brief Highlights the XML \a text: tags, attribute names, and attribute values.
 *
 * Tag delimiters with their element names are colored first, then attribute names
 * preceding an equals sign, then quoted attribute values. Element text content
 * keeps the default color.
 */
void StructuredValueHighlighter::highlightXml(const QString &text)
{
    QRegularExpressionMatchIterator tagIt = m_xmlTagRegex.globalMatch(text);
    while (tagIt.hasNext()) {
        const QRegularExpressionMatch match = tagIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), m_tagFormat);
    }

    QRegularExpressionMatchIterator attributeIt = m_xmlAttributeRegex.globalMatch(text);
    while (attributeIt.hasNext()) {
        const QRegularExpressionMatch match = attributeIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), m_attributeFormat);
    }

    QRegularExpressionMatchIterator valueIt = m_xmlValueRegex.globalMatch(text);
    while (valueIt.hasNext()) {
        const QRegularExpressionMatch match = valueIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), m_stringFormat);
    }
}
