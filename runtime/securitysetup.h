#ifndef SECURITYSETUP_H
#define SECURITYSETUP_H

#include <QString>

#include <open62541/server.h>

#include "serverproject/serverprojectdata.h"

/**
 * Applies a project's security configuration to an open62541 server config.
 *
 * Configures the offered endpoints (SecurityPolicy#None and/or the encrypted
 * policies), the server certificate (generated into the server PKI on first
 * use), and the access control (anonymous and username/password logins). For a
 * controlled test lab the certificate verification accepts any client
 * certificate so secure sessions succeed without manual trust exchange.
 */
namespace SecuritySetup {

/**
 * Applies \a project's security to \a config for endpoint \a port, using
 * \a pkiDir for the server certificate store. Returns the open62541 status;
 * on failure \a error carries a human-readable reason.
 */
UA_StatusCode apply(UA_ServerConfig *config, const ServerProject::ProjectData &project,
                    quint16 port, const QString &pkiDir, QString &error);

} // namespace SecuritySetup

#endif // SECURITYSETUP_H
