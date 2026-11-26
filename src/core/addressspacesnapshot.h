#ifndef ADDRESSSPACESNAPSHOT_H
#define ADDRESSSPACESNAPSHOT_H

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "clonebrowser.h"

/** What an address-space snapshot covers. */
struct SnapshotScope
{
    /** Scope kind: "Objects", "Node" or "Server". */
    QString kind;
    /** Node id of the browse root. */
    QString rootNodeId;
    /** Browse path of the root, put in front of every node path; empty when the root is captured. */
    QString rootBrowsePath;
    /** Whether variable values were read. */
    bool includeValues = true;
};

/** Everything one snapshot file is built from. */
struct SnapshotInput
{
    /** Name of the application that wrote the snapshot. */
    QString application;
    /** Version of that application. */
    QString applicationVersion;
    /** Time the snapshot was taken. */
    QDateTime created;
    /** What the snapshot covers. */
    SnapshotScope scope;
    /** Captured nodes, parents before children. */
    QList<CloneNode> nodes;
    /** NamespaceArray of the server. */
    QStringList namespaceUris;
    /** Outcome of the traversal, including the server identity. */
    CloneCompletion completion;
};

/**
 * Builds and writes the JSON address-space snapshot (`*.uasnapshot.json`).
 *
 * A snapshot is a faithful, read-only record of a server's address space:
 * original NodeIds and namespaces, DataTypes with their base types, ValueRank,
 * ArrayDimensions, access levels, typed values with status and timestamps, and
 * whether the traversal was complete. Nodes are sorted by browse path, so two
 * snapshots of the same server compare cleanly with a text diff.
 */
namespace AddressSpaceSnapshot {

/** Version of the snapshot file format written by toJson(). */
inline constexpr int kFormatVersion = 1;

/** Returns the format identifier stored in every snapshot. */
QString formatName();

/** Returns the snapshot document for \a input. */
QJsonObject toJson(const SnapshotInput &input);

/** Outcome of write(). */
struct WriteResult
{
    /** Whether the file was written completely. */
    bool ok = false;
    /** Reason of a failure; empty on success. */
    QString errorString;
    /** Size of the written file in bytes. */
    qint64 bytes = 0;
};

/** Writes the snapshot of \a input to \a filePath atomically, as indented JSON. */
WriteResult write(const QString &filePath, const SnapshotInput &input);

} // namespace AddressSpaceSnapshot

#endif // ADDRESSSPACESNAPSHOT_H
