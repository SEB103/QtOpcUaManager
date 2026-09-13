#ifndef SERVERPROJECTNODESET_H
#define SERVERPROJECTNODESET_H

#include <QHash>
#include <QString>

#include "serverprojectdata.h"

namespace ServerProject {

/**
 * Imports and exports the project address space as OPC UA NodeSet2 XML.
 *
 * This is model-level interoperability implemented with Qt XML only (no
 * open62541 dependency): the designed folders, objects, variables and
 * enumeration types are written as a standard UANodeSet document and parsed
 * back. It covers the subset Server Studio models (Folders/Objects/Variables
 * with built-in scalar and one-dimensional array values, plus enumeration data
 * types); unsupported node classes in a third-party file are skipped on import.
 */
class NodeSet
{
public:
    /** Outcome of an export. */
    struct Result {
        bool ok = false;
        QString errorString;
    };

    /** Outcome of an import: the parsed address space, or an error. */
    struct ImportResult {
        bool ok = false;
        QString errorString;
        QList<Namespace> namespaces;
        QList<EnumType> enumTypes;
        QList<Node> nodes;

        /** Total number of top-level nodes the model cannot represent. */
        int skippedCount = 0;

        /**
         * Per-element-name counts of skipped nodes (for example "UAMethod" or
         * "UAObjectType"), so the import can honestly report what it dropped.
         */
        QHash<QString, int> skippedKinds;
    };

    /** Writes \a data's address space to \a filePath as NodeSet2 XML. */
    static Result exportToFile(const QString &filePath, const ProjectData &data);

    /** Parses the NodeSet2 XML at \a filePath into an address space. */
    static ImportResult importFromFile(const QString &filePath);
};

} // namespace ServerProject

#endif // SERVERPROJECTNODESET_H
