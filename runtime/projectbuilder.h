#ifndef PROJECTBUILDER_H
#define PROJECTBUILDER_H

#include <QString>

#include <open62541/server.h>

#include "serverproject/serverprojectdata.h"

/**
 * Builds an open62541 address space from a ServerProject::ProjectData.
 *
 * Registers the project's custom namespaces (remapping project-relative
 * namespace indices to the indices open62541 actually assigns), then adds the
 * folders, objects and variables. This is the runtime half of the shared
 * project model; the editor produces the same model.
 */
namespace ProjectBuilder {

/**
 * Adds every namespace and node from \a project to \a server.
 *
 * Returns \c true on success. On failure \a error carries a human-readable
 * reason and the address space may be partially built; the caller should treat
 * a failure as fatal and not open the endpoint.
 */
bool build(UA_Server *server, const ServerProject::ProjectData &project, QString &error);

} // namespace ProjectBuilder

#endif // PROJECTBUILDER_H
