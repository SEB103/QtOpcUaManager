#ifndef NODECONTEXT_H
#define NODECONTEXT_H

#include <QString>

/**
 * Context text for the Data Access View: where a monitored node lives.
 *
 * The context is the path of the node's parent in IEC 61131-3 notation, e.g.
 * "V_HMI.astTestPrepContainer[1].stRefOffset", derived by one fixed rule from
 * the node itself and therefore independent of the other rows in the table.
 */
namespace NodeContext {

/**
 * Returns the parent path of the node \a nodeId with browse path \a nodePath
 * and display name \a displayName.
 *
 * A CODESYS symbol node id ("…|var|<device>.Application.<symbol>") yields its
 * symbol path without the last member; any other node yields its browse path
 * without the leading "Objects" segment and without its own segment, joined
 * with dots. Empty when the node has no parent below that root.
 */
QString contextOf(const QString &nodeId, const QString &nodePath, const QString &displayName);

} // namespace NodeContext

#endif // NODECONTEXT_H
