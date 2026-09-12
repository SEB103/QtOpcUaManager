#include "securitysetup.h"

#include <vector>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/plugin/create_certificate.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/pki_default.h>
#include <open62541/server_config_default.h>

using ServerProject::ProjectData;
using ServerProject::UserCredential;

namespace {

/*!
 * \internal
 * \brief Reads \a path into a newly allocated UA_ByteString (empty on failure).
 */
UA_ByteString readByteString(const QString &path)
{
    UA_ByteString result = UA_BYTESTRING_NULL;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return result;
    const QByteArray bytes = file.readAll();
    if (UA_ByteString_allocBuffer(&result, bytes.size()) != UA_STATUSCODE_GOOD)
        return UA_BYTESTRING_NULL;
    memcpy(result.data, bytes.constData(), bytes.size());
    return result;
}

/*!
 * \internal
 * \brief Writes the bytes of \a value to \a path.
 */
bool writeByteString(const QString &path, const UA_ByteString &value)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(reinterpret_cast<const char *>(value.data), value.length);
    return true;
}

/*!
 * \internal
 * \brief Loads the server certificate/key from \a pkiDir, generating them once.
 */
bool loadOrCreateCertificate(const QString &pkiDir, const QString &applicationUri,
                             UA_ByteString &certificate, UA_ByteString &privateKey,
                             QString &error)
{
    const QDir ownDir(pkiDir + QStringLiteral("/own"));
    const QString certPath = ownDir.filePath(QStringLiteral("server_cert.der"));
    const QString keyPath = ownDir.filePath(QStringLiteral("server_key.der"));

    if (QFileInfo::exists(certPath) && QFileInfo::exists(keyPath)) {
        certificate = readByteString(certPath);
        privateKey = readByteString(keyPath);
        if (certificate.length > 0 && privateKey.length > 0)
            return true;
        UA_ByteString_clear(&certificate);
        UA_ByteString_clear(&privateKey);
    }

    if (!QDir().mkpath(ownDir.absolutePath())) {
        error = QStringLiteral("Cannot create server PKI directory: %1")
                    .arg(ownDir.absolutePath());
        return false;
    }

    UA_String subject[3] = {UA_STRING(const_cast<char *>("C=DE")),
                            UA_STRING(const_cast<char *>("O=OpcUaManager")),
                            UA_STRING(const_cast<char *>("CN=OpcUaManager Test Server"))};
    const QByteArray uriSan = (QStringLiteral("URI:") + applicationUri).toUtf8();
    UA_String subjectAltName[2] = {
        UA_STRING(const_cast<char *>("DNS:localhost")),
        UA_String{static_cast<size_t>(uriSan.size()),
                  reinterpret_cast<UA_Byte *>(const_cast<char *>(uriSan.constData()))}};

    const UA_StatusCode status =
        UA_CreateCertificate(UA_Log_Stdout, subject, 3, subjectAltName, 2,
                             UA_CERTIFICATEFORMAT_DER, nullptr, &privateKey, &certificate);
    if (status != UA_STATUSCODE_GOOD) {
        error = QStringLiteral("Certificate generation failed: %1")
                    .arg(QString::fromUtf8(UA_StatusCode_name(status)));
        return false;
    }

    writeByteString(certPath, certificate);
    writeByteString(keyPath, privateKey);
    return true;
}

/*!
 * \internal
 * \brief Removes SecurityPolicy#None endpoints from \a config in place.
 */
void pruneNoneEndpoints(UA_ServerConfig *config)
{
    size_t kept = 0;
    for (size_t i = 0; i < config->endpointsSize; ++i) {
        UA_EndpointDescription *endpoint = &config->endpoints[i];
        const QString policy = QString::fromUtf8(
            reinterpret_cast<const char *>(endpoint->securityPolicyUri.data),
            int(endpoint->securityPolicyUri.length));
        if (policy.endsWith(QLatin1String("#None"))) {
            UA_EndpointDescription_clear(endpoint);
        } else {
            if (kept != i)
                config->endpoints[kept] = *endpoint;
            ++kept;
        }
    }
    config->endpointsSize = kept;
}

} // namespace

namespace SecuritySetup {

/*!
 * \brief Applies \a project's security to \a config.
 */
UA_StatusCode apply(UA_ServerConfig *config, const ProjectData &project, quint16 port,
                    const QString &pkiDir, QString &error)
{
    const QString applicationUri = project.server.applicationUri.isEmpty()
                                       ? QStringLiteral("urn:opcuamanager:serverruntime")
                                       : project.server.applicationUri;

    // Configure the transport endpoints.
    if (project.security.enableSecurity) {
        UA_ByteString certificate = UA_BYTESTRING_NULL;
        UA_ByteString privateKey = UA_BYTESTRING_NULL;
        if (!loadOrCreateCertificate(pkiDir, applicationUri, certificate, privateKey, error))
            return UA_STATUSCODE_BADCONFIGURATIONERROR;

        const UA_StatusCode status = UA_ServerConfig_setDefaultWithSecurityPolicies(
            config, port, &certificate, &privateKey, nullptr, 0, nullptr, 0, nullptr, 0);
        UA_ByteString_clear(&certificate);
        UA_ByteString_clear(&privateKey);
        if (status != UA_STATUSCODE_GOOD) {
            error = QStringLiteral("Failed to configure secure endpoints: %1")
                        .arg(QString::fromUtf8(UA_StatusCode_name(status)));
            return status;
        }

        // Test lab: accept any client certificate so secure sessions succeed
        // without a manual trust exchange.
        UA_CertificateVerification_AcceptAll(&config->secureChannelPKI);
        UA_CertificateVerification_AcceptAll(&config->sessionPKI);

        if (!project.security.allowNone)
            pruneNoneEndpoints(config);
    } else {
        const UA_StatusCode status = UA_ServerConfig_setMinimal(config, port, nullptr);
        if (status != UA_STATUSCODE_GOOD) {
            error = QStringLiteral("Failed to configure the endpoint: %1")
                        .arg(QString::fromUtf8(UA_StatusCode_name(status)));
            return status;
        }
    }

    // Build the username/password login list; UA_AccessControl_default copies
    // the strings, so the allocated temporaries are cleared afterwards.
    std::vector<UA_UsernamePasswordLogin> logins;
    logins.reserve(project.security.users.size());
    for (const UserCredential &user : project.security.users) {
        UA_UsernamePasswordLogin login;
        login.username = UA_STRING_ALLOC(user.username.toUtf8().constData());
        login.password = UA_STRING_ALLOC(user.password.toUtf8().constData());
        logins.push_back(login);
    }

    const UA_StatusCode acStatus =
        UA_AccessControl_default(config, project.security.allowAnonymous, nullptr,
                                 logins.size(), logins.data());

    for (UA_UsernamePasswordLogin &login : logins) {
        UA_String_clear(&login.username);
        UA_String_clear(&login.password);
    }

    if (acStatus != UA_STATUSCODE_GOOD) {
        error = QStringLiteral("Failed to configure access control: %1")
                    .arg(QString::fromUtf8(UA_StatusCode_name(acStatus)));
    }
    return acStatus;
}

} // namespace SecuritySetup
