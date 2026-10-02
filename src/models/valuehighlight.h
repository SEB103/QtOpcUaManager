#ifndef VALUEHIGHLIGHT_H
#define VALUEHIGHLIGHT_H

#include <QLatin1StringView>
#include <QString>
#include <QStringList>

#include "core/opcuavaluedata.h"

/**
 * Token classes colored by the value views.
 *
 * The structured Value panel and the Data Access View share one palette so a
 * string, number, or boolean looks the same in both places.
 */
enum class ValueHighlightToken {
    /** JSON property name. */
    Key,
    /** String value (JSON) or attribute value (XML). */
    String,
    /** Numeric value. */
    Number,
    /** The literals \c true, \c false, and \c null. */
    Keyword,
    /** Structural punctuation, brackets, and list separators. */
    Punctuation,
    /** XML tag delimiters and element names. */
    Tag,
    /** XML attribute names. */
    Attribute
};

/**
 * Returns the \c #rrggbb color of \a token in the dark (\a dark true) or light palette.
 *
 * The dark palette approximates the Visual Studio Code dark theme and the light
 * palette the Light+ theme.
 */
QLatin1StringView valueHighlightColorName(ValueHighlightToken token, bool dark);

/** Maximum number of array elements written into value markup. */
inline constexpr int valueHighlightMaxArrayElements = 200;

/**
 * Returns Qt Quick StyledText that colors the value \a text by its \a kind.
 *
 * When \a arrayElements is not empty, each element is colored separately and
 * joined with punctuation-colored separators; at most
 * valueHighlightMaxArrayElements elements are written, followed by an ellipsis.
 * All text is HTML-escaped and keeps its spaces and line breaks. An Empty kind
 * yields the escaped text without a color, so the view's base color applies.
 */
QString valueHighlightMarkup(const QString &text, const QStringList &arrayElements,
                             OpcUaValueKind kind, bool dark);

#endif // VALUEHIGHLIGHT_H
