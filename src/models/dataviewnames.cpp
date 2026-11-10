#include "dataviewnames.h"

#include <QHash>

namespace {

/*!
 * \internal
 * \brief Splits one path \a segment into its name and trailing array index tokens.
 *
 * "astTestPrepContainer[1]" yields "astTestPrepContainer" and "[1]"; nested
 * indexes ("m[1][2]") yield one token per bracket group; a multi-dimensional
 * index ("[1,2]") stays one token; a segment that is only an index yields just
 * that index.
 */
QStringList segmentTokens(const QString &segment)
{
    QStringList indexes;
    QString rest = segment;
    while (rest.endsWith(QLatin1Char(']'))) {
        const qsizetype open = rest.lastIndexOf(QLatin1Char('['));
        if (open < 0)
            break;
        indexes.prepend(rest.mid(open));
        rest.truncate(open);
    }

    QStringList result;
    if (!rest.isEmpty())
        result.append(rest);
    result.append(indexes);
    return result;
}

/*!
 * \internal
 * \brief Returns the separator written in front of \a token inside a name.
 *
 * Array indexes attach directly to what precedes them; every other token is
 * separated by a dot, as in an IEC 61131-3 variable reference.
 */
QString separatorBefore(const QString &token)
{
    return token.startsWith(QLatin1Char('[')) ? QString() : QStringLiteral(".");
}

/*!
 * \internal
 * \brief Naming input of one row: its tokens and how many of them form the leaf.
 */
struct RowTokens
{
    QStringList tokens;
    qsizetype leafCount = 0;
};

/*!
 * \internal
 * \brief Returns whether the last \a count tokens of \a a and \a b are equal.
 *
 * A row with fewer than \a count tokens has no such tail and never matches.
 */
bool sameTail(const QStringList &a, const QStringList &b, qsizetype count)
{
    if (a.size() < count || b.size() < count)
        return false;
    for (qsizetype i = 1; i <= count; ++i) {
        if (a.at(a.size() - i) != b.at(b.size() - i))
            return false;
    }
    return true;
}

/*!
 * \internal
 * \brief Renders the qualifier formed by tokens [\a from, \a to) of \a tokens.
 *
 * The result ends with the separator the first leaf token needs, so prefix and
 * leaf can be concatenated directly.
 */
QString renderPrefix(const QStringList &tokens, qsizetype from, qsizetype to)
{
    QString prefix;
    for (qsizetype i = from; i < to; ++i) {
        if (i > from)
            prefix += separatorBefore(tokens.at(i));
        prefix += tokens.at(i);
    }
    if (!prefix.isEmpty() && to < tokens.size())
        prefix += separatorBefore(tokens.at(to));
    return prefix;
}

} // namespace

namespace DataViewNames {

/*!
 * \brief Splits \a nodePath into naming tokens, falling back to \a displayName.
 *
 * Empty path segments are ignored. Every trailing array index of a segment is
 * a token of its own, so the index can qualify a name without the array name.
 */
QStringList tokens(const QString &nodePath, const QString &displayName)
{
    QStringList segments = nodePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.isEmpty() && !displayName.isEmpty())
        segments.append(displayName);

    QStringList result;
    for (const QString &segment : std::as_const(segments))
        result.append(segmentTokens(segment));
    return result;
}

/*!
 * \brief Returns the structured name of every entry in \a entries.
 *
 * Rows are grouped by display name. A row whose name is unique keeps it. A
 * colliding row takes the shortest tail of its tokens, longer than its own
 * leaf, that no other row of the group shares; the tail is contiguous so it
 * reads like a variable reference. When the whole path is exhausted, the row
 * shows its full path, and a row with an identical path on another server is
 * told apart by the server name.
 */
QList<Name> structuredNames(const QList<Entry> &entries)
{
    QList<RowTokens> rows;
    rows.reserve(entries.size());
    QHash<QString, QList<qsizetype>> groups;

    for (qsizetype i = 0; i < entries.size(); ++i) {
        const Entry &entry = entries.at(i);
        RowTokens row;
        row.tokens = tokens(entry.nodePath, entry.displayName);
        const QString lastSegment = entry.nodePath.isEmpty()
                                        ? entry.displayName
                                        : entry.nodePath.section(QLatin1Char('/'), -1,
                                                                 -1, QString::SectionSkipEmpty);
        row.leafCount = qMin(segmentTokens(lastSegment).size(), row.tokens.size());
        rows.append(row);
        groups[entry.displayName].append(i);
    }

    QList<Name> names;
    names.reserve(entries.size());
    for (const Entry &entry : entries)
        names.append(Name {QString(), entry.displayName, QString()});

    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const QList<qsizetype> &group = it.value();
        if (group.size() < 2)
            continue;

        for (const qsizetype index : group) {
            const QStringList &own = rows.at(index).tokens;
            const qsizetype count = own.size();

            qsizetype tail = rows.at(index).leafCount + 1;
            for (; tail <= count; ++tail) {
                bool collides = false;
                for (const qsizetype other : group) {
                    if (other != index && sameTail(own, rows.at(other).tokens, tail)) {
                        collides = true;
                        break;
                    }
                }
                if (!collides)
                    break;
            }

            const qsizetype leafStart = count - rows.at(index).leafCount;
            if (tail <= count) {
                names[index].prefix = renderPrefix(own, count - tail, leafStart);
                continue;
            }

            // The whole path is shared: show it, and name the server when an
            // identical path comes from a different one.
            names[index].prefix = renderPrefix(own, 0, leafStart);
            for (const qsizetype other : group) {
                if (other != index && rows.at(other).tokens == own
                    && entries.at(other).server != entries.at(index).server) {
                    names[index].suffix =
                        QStringLiteral(" (%1)").arg(entries.at(index).server);
                    break;
                }
            }
        }
    }

    return names;
}

} // namespace DataViewNames
