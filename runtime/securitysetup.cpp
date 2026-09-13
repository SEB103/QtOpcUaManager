#include "securitysetup.h"

#include <string>
#include <vector>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

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
 * \brief Creates the server PKI folder skeleton under \a pkiDir if missing.
 *
 * The layout mirrors the OPC UA convention: own/ for the server key pair,
 * trusted/ for accepted client certificates, issuers/ for intermediate CA
 * certificates, and rejected/ for certificates the server refused. Each store
 * has certs/ and crl/ subfolders.
 */
void ensurePkiLayout(const QString &pkiDir)
{
    const QStringList subDirs = {
        QStringLiteral("own"),
        QStringLiteral("trusted/certs"),  QStringLiteral("trusted/crl"),
        QStringLiteral("issuers/certs"),  QStringLiteral("issuers/crl"),
        QStringLiteral("rejected/certs"), QStringLiteral("rejected/crl"),
    };
    for (const QString &sub : subDirs)
        QDir().mkpath(pkiDir + QLatin1Char('/') + sub);
}

/*!
 * \internal
 * \brief Reads every file matching \a filters in \a dir as a UA_ByteString.
 *
 * The caller owns the returned byte strings and must clear each one.
 */
std::vector<UA_ByteString> loadCertificateList(const QString &dir, const QStringList &filters)
{
    std::vector<UA_ByteString> list;
    const QDir directory(dir);
    if (!directory.exists())
        return list;
    const QStringList files = directory.entryList(filters, QDir::Files);
    for (const QString &name : files) {
        const UA_ByteString bytes = readByteString(directory.filePath(name));
        if (bytes.length > 0)
            list.push_back(bytes);
    }
    return list;
}

/*!
 * \internal
 * \brief Context for the rejected-certificate-capturing verifier wrapper.
 *
 * On Windows only the in-memory UA_CertificateVerification_Trustlist verifier
 * is available (the folder-based variant that writes rejected certificates is
 * Linux-only), so this wraps the trust-list verifier to persist a rejected
 * client certificate to disk before returning the original failure.
 */
struct RejectedCapture
{
    UA_StatusCode (*origVerify)(const UA_CertificateVerification *, const UA_ByteString *) = nullptr;
    void (*origClear)(UA_CertificateVerification *) = nullptr;
    void *origContext = nullptr;
    std::string rejectedDir;
};

/*!
 * \internal
 * \brief Writes a rejected client \a certificate to the rejected/certs store.
 *
 * The file is named by the certificate's SHA-1 fingerprint, so re-offering the
 * same certificate does not create duplicates.
 */
void saveRejectedCertificate(const std::string &rejectedDir, const UA_ByteString *certificate)
{
    if (!certificate || certificate->length == 0)
        return;
    const QByteArray der(reinterpret_cast<const char *>(certificate->data),
                         int(certificate->length));
    const QString fingerprint =
        QString::fromLatin1(QCryptographicHash::hash(der, QCryptographicHash::Sha1).toHex());
    const QString path = QString::fromStdString(rejectedDir) + QLatin1Char('/')
                         + fingerprint + QStringLiteral(".der");
    if (QFileInfo::exists(path))
        return;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(der);
}

/*!
 * \internal
 * \brief Verifier wrapper: delegates to the trust-list verifier and, on
 *        failure, saves the offered certificate to the rejected store.
 */
UA_StatusCode verifyAndCaptureRejected(const UA_CertificateVerification *cv,
                                       const UA_ByteString *certificate)
{
    auto *capture = static_cast<RejectedCapture *>(cv->context);
    UA_CertificateVerification inner = *cv;
    inner.context = capture->origContext;
    inner.verifyCertificate = capture->origVerify;
    inner.clear = capture->origClear;
    const UA_StatusCode status = capture->origVerify(&inner, certificate);
    if (status != UA_STATUSCODE_GOOD)
        saveRejectedCertificate(capture->rejectedDir, certificate);
    return status;
}

/*!
 * \internal
 * \brief Clear wrapper: clears the wrapped verifier and frees the context.
 */
void clearCapturedVerifier(UA_CertificateVerification *cv)
{
    auto *capture = static_cast<RejectedCapture *>(cv->context);
    if (!capture)
        return;
    if (capture->origClear) {
        UA_CertificateVerification inner = *cv;
        inner.context = capture->origContext;
        inner.clear = capture->origClear;
        capture->origClear(&inner);
    }
    delete capture;
    cv->context = nullptr;
    cv->verifyCertificate = nullptr;
    cv->clear = nullptr;
}

/*!
 * \internal
 * \brief Wraps \a cv so rejected client certificates are saved to \a rejectedDir.
 */
void wrapRejectedCapture(UA_CertificateVerification *cv, const QString &rejectedDir)
{
    auto *capture = new RejectedCapture;
    capture->origVerify = cv->verifyCertificate;
    capture->origClear = cv->clear;
    capture->origContext = cv->context;
    capture->rejectedDir = rejectedDir.toStdString();

    cv->context = capture;
    cv->verifyCertificate = &verifyAndCaptureRejected;
    cv->clear = &clearCapturedVerifier;
}

/*!
 * \internal
 * \brief Builds a real trust-list verifier for \a cv from the server PKI.
 *
 * Loads trusted and issuer certificates and CRLs from \a pkiDir, installs the
 * trust-list verifier, then wraps it to persist rejected certificates. Returns
 * the trust-list construction status.
 */
UA_StatusCode applyTrustList(UA_CertificateVerification *cv, const QString &pkiDir,
                             std::vector<UA_ByteString> &trusted,
                             std::vector<UA_ByteString> &issuers,
                             std::vector<UA_ByteString> &crls)
{
    const UA_StatusCode status = UA_CertificateVerification_Trustlist(
        cv, trusted.data(), trusted.size(), issuers.data(), issuers.size(),
        crls.data(), crls.size());
    if (status != UA_STATUSCODE_GOOD)
        return status;
    wrapRejectedCapture(cv, pkiDir + QStringLiteral("/rejected/certs"));
    return UA_STATUSCODE_GOOD;
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

        // Align the server's ApplicationURI with the URI in its certificate's
        // SubjectAltName. A real trust-list verifier enforces this match at
        // endpoint startup (BadCertificateUriInvalid otherwise); the AcceptAll
        // path tolerates a mismatch but keeping them consistent is correct.
        const QByteArray appUriUtf8 = applicationUri.toUtf8();
        UA_String_clear(&config->applicationDescription.applicationUri);
        config->applicationDescription.applicationUri = UA_STRING_ALLOC(appUriUtf8.constData());
        for (size_t i = 0; i < config->endpointsSize; ++i) {
            UA_String_clear(&config->endpoints[i].server.applicationUri);
            config->endpoints[i].server.applicationUri = UA_STRING_ALLOC(appUriUtf8.constData());
        }

        if (project.security.acceptAllClientCerts) {
            // Convenience mode: accept any client certificate so secure sessions
            // succeed without a manual trust exchange.
            UA_CertificateVerification_AcceptAll(&config->secureChannelPKI);
            UA_CertificateVerification_AcceptAll(&config->sessionPKI);
        } else {
            // Strict mode: enforce a real trust list from the server PKI. Unknown
            // client certificates are rejected and saved to the rejected store.
            ensurePkiLayout(pkiDir);
            const QStringList certFilters{QStringLiteral("*.der"), QStringLiteral("*.crt")};
            const QStringList crlFilters{QStringLiteral("*.crl"), QStringLiteral("*.der")};
            std::vector<UA_ByteString> trusted =
                loadCertificateList(pkiDir + QStringLiteral("/trusted/certs"), certFilters);
            std::vector<UA_ByteString> issuers =
                loadCertificateList(pkiDir + QStringLiteral("/issuers/certs"), certFilters);
            std::vector<UA_ByteString> crls =
                loadCertificateList(pkiDir + QStringLiteral("/trusted/crl"), crlFilters);
            for (UA_ByteString &crl : loadCertificateList(pkiDir + QStringLiteral("/issuers/crl"),
                                                          crlFilters))
                crls.push_back(crl);

            const UA_StatusCode channelStatus =
                applyTrustList(&config->secureChannelPKI, pkiDir, trusted, issuers, crls);
            const UA_StatusCode sessionStatus =
                applyTrustList(&config->sessionPKI, pkiDir, trusted, issuers, crls);

            for (UA_ByteString &cert : trusted)
                UA_ByteString_clear(&cert);
            for (UA_ByteString &cert : issuers)
                UA_ByteString_clear(&cert);
            for (UA_ByteString &crl : crls)
                UA_ByteString_clear(&crl);

            if (channelStatus != UA_STATUSCODE_GOOD || sessionStatus != UA_STATUSCODE_GOOD) {
                error = QStringLiteral("Failed to build the certificate trust list: %1")
                            .arg(QString::fromUtf8(UA_StatusCode_name(
                                channelStatus != UA_STATUSCODE_GOOD ? channelStatus
                                                                    : sessionStatus)));
                return channelStatus != UA_STATUSCODE_GOOD ? channelStatus : sessionStatus;
            }
        }

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
