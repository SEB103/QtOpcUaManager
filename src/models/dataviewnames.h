#ifndef DATAVIEWNAMES_H
#define DATAVIEWNAMES_H

#include <QList>
#include <QString>
#include <QStringList>

/**
 * Distinguishable row names for the Data Access View.
 *
 * Rows whose display name occurs more than once are shown with the shortest
 * tail of their browse path that no other row with the same name shares,
 * written in IEC 61131-3 notation (e.g. "[1].stRefOffset.value"). All
 * functions are pure.
 */
namespace DataViewNames {

/** Identity of one Data View row as far as naming is concerned. */
struct Entry
{
    /** Server the node belongs to. */
    QString server;
    /** Browse path with segments joined by '/'; may be empty. */
    QString nodePath;
    /** Display name reported by the server for the node itself. */
    QString displayName;
};

/** Structured name of one row, split for rendering with a muted qualifier. */
struct Name
{
    /** Qualifier in front of the leaf, e.g. "[1].stRefOffset."; empty when unique. */
    QString prefix;
    /** The row's own display name. */
    QString leaf;
    /** Server disambiguation, e.g. " (PLC-2)"; usually empty. */
    QString suffix;

    /** Returns the complete name as shown in the table. */
    QString text() const { return prefix + leaf + suffix; }
};

/**
 * An entry prepared for naming: its tokens and their tail hashes, computed once
 * so that names can be recomputed after every row change without re-parsing.
 */
struct TokenizedEntry
{
    /** Server the node belongs to. */
    QString server;
    /** Display name reported by the server for the node itself. */
    QString displayName;
    /** Naming tokens of the path; see tokens(). */
    QStringList tokens;
    /** Number of trailing tokens that make up the node's own name. */
    qsizetype leafCount = 0;
    /** Hash of the last k tokens at index k - 1; equal tails have equal hashes. */
    QList<size_t> tailHashes;
};

/** How much of its path a row shows, before the name is rendered as text. */
struct Qualifier
{
    /** Number of trailing tokens shown; 0 when the display name is unique. */
    qsizetype tail = 0;
    /** Whether the server name is appended because an identical path exists elsewhere. */
    bool serverSuffix = false;

    /** Returns whether \a other describes the same qualification. */
    bool operator==(const Qualifier &other) const = default;
};

/**
 * Splits \a nodePath into naming tokens: one per path segment, with every
 * trailing array index ("[1]", "[1,2]") as a token of its own. Falls back to
 * \a displayName when the path is empty.
 */
QStringList tokens(const QString &nodePath, const QString &displayName);

/** Prepares \a entry for naming. */
TokenizedEntry tokenize(const Entry &entry);

/** Returns the structured name of every entry, in the same order as \a entries. */
QList<Name> structuredNames(const QList<Entry> &entries);

/**
 * Returns the structured name of every prepared entry, in the same order as
 * \a entries. Runs in time linear in the number of entries times path depth.
 */
QList<Name> structuredNames(const QList<TokenizedEntry> &entries);

/**
 * Returns how much of its path every prepared entry shows, in the same order
 * as \a entries, without rendering any text. Lets a caller re-render only the
 * names whose qualification changed.
 */
QList<Qualifier> qualifiers(const QList<TokenizedEntry> &entries);

/** Renders the name of \a entry qualified by \a qualifier. */
Name render(const TokenizedEntry &entry, const Qualifier &qualifier);

} // namespace DataViewNames

#endif // DATAVIEWNAMES_H
