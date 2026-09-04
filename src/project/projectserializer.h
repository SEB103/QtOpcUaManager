#ifndef PROJECTSERIALIZER_H
#define PROJECTSERIALIZER_H

#include <QString>

#include "project/projectdata.h"

QT_BEGIN_NAMESPACE
class QJsonObject;
QT_END_NAMESPACE

/**
 * Reads and writes ProjectData to and from .uaproj JSON files.
 *
 * All functions are static; the class has no state. Loading validates the file
 * and reports a typed error instead of throwing, so a missing or malformed
 * project file never crashes the caller.
 */
class ProjectSerializer
{
public:
    /** Reason a load() call failed. */
    enum class Error {
        /** No error; the load succeeded. */
        None,
        /** The project file does not exist. */
        FileNotFound,
        /** The project file could not be opened for reading. */
        ReadFailed,
        /** The file content is not a valid JSON object. */
        InvalidJson,
        /** The JSON is well-formed but does not describe a project. */
        InvalidSchema,
        /** The file uses a schema version this build cannot read. */
        UnsupportedVersion,
        /** The project file could not be written. */
        WriteFailed
    };

    /** Outcome of a load() call: parsed data plus success and error details. */
    struct LoadResult
    {
        /** Whether the load succeeded. */
        bool ok {false};
        /** Failure reason, or Error::None on success. */
        Error error {Error::None};
        /** Human-readable error text, empty on success. */
        QString errorString;
        /** Parsed project data; only meaningful when ok is true. */
        ProjectData data;
    };

    /** Loads and validates the project file at \a filePath. */
    static LoadResult load(const QString &filePath);

    /**
     * Writes \a data as pretty-printed JSON to \a filePath, creating parent
     * directories as needed. Returns true on success; on failure returns false
     * and sets \a errorString when it is not null.
     */
    static bool save(const QString &filePath, const ProjectData &data, QString *errorString = nullptr);

    /** Serializes \a data into a JSON object. */
    static QJsonObject toJson(const ProjectData &data);

    /** Returns a human-readable description of \a error. */
    static QString errorToString(Error error);
};

#endif // PROJECTSERIALIZER_H
