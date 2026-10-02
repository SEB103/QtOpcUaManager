#include "valuehighlight.h"

namespace {

/*!
 * \internal
 * \brief Returns the token that colors a value of \a kind.
 *
 * Matches the JSON grammar of the structured Value panel: booleans are JSON
 * literals, numbers are numbers, and every other value is written as a string.
 */
ValueHighlightToken tokenForKind(OpcUaValueKind kind)
{
    switch (kind) {
    case OpcUaValueKind::Boolean:
        return ValueHighlightToken::Keyword;
    case OpcUaValueKind::Number:
        return ValueHighlightToken::Number;
    case OpcUaValueKind::Text:
    case OpcUaValueKind::Empty:
        break;
    }
    return ValueHighlightToken::String;
}

/*!
 * \internal
 * \brief Returns \a text escaped for StyledText with its whitespace preserved.
 *
 * StyledText collapses runs of spaces and ignores raw line breaks like HTML, so
 * spaces become non-breaking spaces and line breaks become \c <br> tags.
 */
QString escapeStyledText(const QString &text)
{
    QString escaped = text.toHtmlEscaped();
    escaped.replace(QLatin1Char(' '), QStringLiteral("&nbsp;"));
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return escaped;
}

/*!
 * \internal
 * \brief Returns the already escaped \a escapedText wrapped in a font tag of \a colorName.
 */
QString colored(const QString &escapedText, QLatin1StringView colorName)
{
    return QStringLiteral("<font color=\"%1\">%2</font>").arg(colorName, escapedText);
}

} // namespace

/*!
 * \brief Returns the \c #rrggbb color of \a token in the dark or light palette.
 *
 * These are the colors of the structured Value panel highlighter; the Data
 * Access View reuses them so both views color values identically.
 */
QLatin1StringView valueHighlightColorName(ValueHighlightToken token, bool dark)
{
    switch (token) {
    case ValueHighlightToken::Key:
        return dark ? QLatin1StringView("#9cdcfe") : QLatin1StringView("#0451a5");
    case ValueHighlightToken::String:
        return dark ? QLatin1StringView("#ce9178") : QLatin1StringView("#a31515");
    case ValueHighlightToken::Number:
        return dark ? QLatin1StringView("#b5cea8") : QLatin1StringView("#098658");
    case ValueHighlightToken::Keyword:
        return dark ? QLatin1StringView("#569cd6") : QLatin1StringView("#0000ff");
    case ValueHighlightToken::Punctuation:
        return dark ? QLatin1StringView("#d4d4d4") : QLatin1StringView("#000000");
    case ValueHighlightToken::Tag:
        return dark ? QLatin1StringView("#569cd6") : QLatin1StringView("#800000");
    case ValueHighlightToken::Attribute:
        return dark ? QLatin1StringView("#9cdcfe") : QLatin1StringView("#0451a5");
    }
    return dark ? QLatin1StringView("#d4d4d4") : QLatin1StringView("#000000");
}

/*!
 * \brief Returns Qt Quick StyledText that colors the value \a text by its \a kind.
 *
 * A scalar is one colored run. An array (\a arrayElements not empty) colors
 * each element and joins them with the punctuation-colored separator used by
 * the plain value text, so the visible characters match \a text. Only the
 * first valueHighlightMaxArrayElements elements are written because the table
 * cell elides long values anyway, which bounds the work per update.
 */
QString valueHighlightMarkup(const QString &text, const QStringList &arrayElements,
                             OpcUaValueKind kind, bool dark)
{
    if (kind == OpcUaValueKind::Empty)
        return escapeStyledText(text);

    const QLatin1StringView valueColor = valueHighlightColorName(tokenForKind(kind), dark);
    if (arrayElements.isEmpty())
        return colored(escapeStyledText(text), valueColor);

    const QLatin1StringView punctuationColor =
        valueHighlightColorName(ValueHighlightToken::Punctuation, dark);
    const QString separator = colored(QStringLiteral(",&nbsp;"), punctuationColor);

    const qsizetype count =
        qMin<qsizetype>(arrayElements.size(), valueHighlightMaxArrayElements);
    QString markup;
    for (qsizetype i = 0; i < count; ++i) {
        if (i > 0)
            markup += separator;
        markup += colored(escapeStyledText(arrayElements.at(i)), valueColor);
    }
    if (arrayElements.size() > count)
        markup += colored(QStringLiteral(",&nbsp;…"), punctuationColor);
    return markup;
}
