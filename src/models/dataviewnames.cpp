#include "dataviewnames.h"

#include <QHash>
#include <QHashFunctions>

#include <limits>

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

/*!
 * \internal
 * \brief Qualifies the rows of one display-name \a group that collide.
 * \param entries All prepared entries.
 * \param result The qualifiers to fill in, indexed like \a entries.
 *
 * For each tail length k, the tails of all rows of the group are counted by
 * their precomputed hash; a row whose tail occurs once is resolved at that k.
 * Equal tails always hash equally, so a hash collision can only make a name one
 * token longer than necessary, never make two names look unique when they are
 * not. Rows still unresolved after their whole path show the full path, and a
 * row whose identical path comes from another server gets the server name.
 */
void qualifyCollidingGroup(const QList<DataViewNames::TokenizedEntry> &entries,
                           const QList<qsizetype> &group,
                           QList<DataViewNames::Qualifier> &result)
{
    qsizetype longest = 0;
    qsizetype shortestLeaf = std::numeric_limits<qsizetype>::max();
    for (const qsizetype index : group) {
        longest = qMax(longest, entries.at(index).tokens.size());
        shortestLeaf = qMin(shortestLeaf, entries.at(index).leafCount);
    }

    QList<qsizetype> unresolved = group;
    QHash<size_t, int> counts;
    counts.reserve(group.size());

    for (qsizetype tail = shortestLeaf + 1; tail <= longest && !unresolved.isEmpty(); ++tail) {
        counts.clear();
        for (const qsizetype index : group) {
            const DataViewNames::TokenizedEntry &entry = entries.at(index);
            if (entry.tokens.size() >= tail)
                ++counts[entry.tailHashes.at(tail - 1)];
        }

        QList<qsizetype> still;
        for (const qsizetype index : std::as_const(unresolved)) {
            const DataViewNames::TokenizedEntry &entry = entries.at(index);
            if (tail > entry.leafCount && entry.tokens.size() >= tail
                && counts.value(entry.tailHashes.at(tail - 1)) == 1) {
                result[index].tail = tail;
            } else {
                still.append(index);
            }
        }
        unresolved = still;
    }

    // The whole path is shared: show it, and name the server when an identical
    // path comes from a different one.
    for (const qsizetype index : std::as_const(unresolved)) {
        const DataViewNames::TokenizedEntry &entry = entries.at(index);
        result[index].tail = entry.tokens.size();
        for (const qsizetype other : std::as_const(unresolved)) {
            if (other != index && entries.at(other).tokens == entry.tokens
                && entries.at(other).server != entry.server) {
                result[index].serverSuffix = true;
                break;
            }
        }
    }
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
 * \brief Prepares \a entry for naming.
 *
 * Tokenizes the path once, counts the tokens of the node's own name (its last
 * path segment), and hashes every tail so that tails can be compared by value
 * without building strings.
 */
TokenizedEntry tokenize(const Entry &entry)
{
    TokenizedEntry result;
    result.server = entry.server;
    result.displayName = entry.displayName;
    result.tokens = tokens(entry.nodePath, entry.displayName);

    const QString lastSegment =
        entry.nodePath.isEmpty()
            ? entry.displayName
            : entry.nodePath.section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    result.leafCount = qMin(segmentTokens(lastSegment).size(), result.tokens.size());

    const qsizetype count = result.tokens.size();
    result.tailHashes.reserve(count);
    size_t hash = 0;
    for (qsizetype tail = 1; tail <= count; ++tail) {
        hash = qHashMulti(0, hash, result.tokens.at(count - tail));
        result.tailHashes.append(hash);
    }
    return result;
}

/*!
 * \brief Returns the structured name of every entry in \a entries.
 *
 * Convenience overload that prepares every entry first.
 */
QList<Name> structuredNames(const QList<Entry> &entries)
{
    QList<TokenizedEntry> prepared;
    prepared.reserve(entries.size());
    for (const Entry &entry : entries)
        prepared.append(tokenize(entry));
    return structuredNames(prepared);
}

/*!
 * \brief Returns the structured name of every prepared entry in \a entries.
 *
 * Rows are grouped by display name. A row whose name is unique keeps it. A
 * colliding row takes the shortest tail of its tokens, longer than its own
 * leaf, that no other row of the group shares; the tail is contiguous so it
 * reads like a variable reference. When the whole path is exhausted, the row
 * shows its full path, and a row with an identical path on another server is
 * told apart by the server name.
 */
QList<Name> structuredNames(const QList<TokenizedEntry> &entries)
{
    const QList<Qualifier> qualified = qualifiers(entries);
    QList<Name> names;
    names.reserve(entries.size());
    for (qsizetype i = 0; i < entries.size(); ++i)
        names.append(render(entries.at(i), qualified.at(i)));
    return names;
}

/*!
 * \brief Returns how much of its path every prepared entry in \a entries shows.
 *
 * Groups rows by display name and qualifies only groups with more than one
 * row; see structuredNames() for the rule.
 */
QList<Qualifier> qualifiers(const QList<TokenizedEntry> &entries)
{
    QList<Qualifier> result(entries.size());
    QHash<QString, QList<qsizetype>> groups;
    for (qsizetype i = 0; i < entries.size(); ++i)
        groups[entries.at(i).displayName].append(i);

    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        if (it.value().size() > 1)
            qualifyCollidingGroup(entries, it.value(), result);
    }
    return result;
}

/*!
 * \brief Renders the name of \a entry qualified by \a qualifier.
 *
 * The prefix holds the shown tokens in front of the leaf, ending with the
 * separator the leaf needs; the leaf is always the display name itself.
 */
Name render(const TokenizedEntry &entry, const Qualifier &qualifier)
{
    Name name {QString(), entry.displayName, QString()};
    const qsizetype count = entry.tokens.size();
    if (qualifier.tail > entry.leafCount)
        name.prefix = renderPrefix(entry.tokens, count - qualifier.tail, count - entry.leafCount);
    if (qualifier.serverSuffix)
        name.suffix = QStringLiteral(" (%1)").arg(entry.server);
    return name;
}

} // namespace DataViewNames
