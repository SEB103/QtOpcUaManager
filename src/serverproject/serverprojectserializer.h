#ifndef SERVERPROJECTSERIALIZER_H
#define SERVERPROJECTSERIALIZER_H

#include <QString>

#include "serverprojectdata.h"

QT_BEGIN_NAMESPACE
class QJsonObject;
QT_END_NAMESPACE

namespace ServerProject {

/**
 * Loads and saves .uaserver project files as JSON.
 *
 * Mirrors the client-side ProjectSerializer: it never throws, reports typed
 * errors, and writes atomically with QSaveFile so a failed write never leaves a
 * partial file. Unknown-but-newer minor fields are ignored on load; missing
 * fields fall back to defaults so older files keep loading.
 */
class Serializer
{
public:
    /** Failure reason for a load or save operation. */
    enum class Error {
        None,
        FileNotFound,
        ReadFailed,
        InvalidJson,
        InvalidSchema,
        UnsupportedVersion,
        WriteFailed
    };

    /** Outcome of a load: \c ok with data, or an error with a message. */
    struct LoadResult {
        bool ok = false;
        Error error = Error::None;
        QString errorString;
        ProjectData data;
    };

    /** Outcome of a save: \c ok, or an error with a message. */
    struct SaveResult {
        bool ok = false;
        Error error = Error::None;
        QString errorString;
    };

    /** Loads the project at \a filePath. */
    static LoadResult load(const QString &filePath);

    /** Saves \a data to \a filePath atomically. */
    static SaveResult save(const QString &filePath, const ProjectData &data);

    /** Serializes \a data to a JSON object (exposed for tests). */
    static QJsonObject toJson(const ProjectData &data);

private:
    /** Parses a JSON \a root object into \a data, returning false on schema errors. */
    static bool fromJson(const QJsonObject &root, ProjectData &data, QString &errorString);
};

} // namespace ServerProject

#endif // SERVERPROJECTSERIALIZER_H
