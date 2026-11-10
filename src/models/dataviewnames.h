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
 * Splits \a nodePath into naming tokens: one per path segment, with every
 * trailing array index ("[1]", "[1,2]") as a token of its own. Falls back to
 * \a displayName when the path is empty.
 */
QStringList tokens(const QString &nodePath, const QString &displayName);

/** Returns the structured name of every entry, in the same order as \a entries. */
QList<Name> structuredNames(const QList<Entry> &entries);

} // namespace DataViewNames

#endif // DATAVIEWNAMES_H
