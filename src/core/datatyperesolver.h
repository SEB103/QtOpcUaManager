#ifndef DATATYPERESOLVER_H
#define DATATYPERESOLVER_H

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QOpcUaClient;
class QOpcUaNode;
class QTimer;

/**
 * Resolution of one OPC UA DataType node id to its OPC UA base type.
 *
 * Keeps the original DataType id and name apart from the base type, so a
 * vendor alias such as a CODESYS STRING type stays identifiable while its base
 * ("String") drives any conversion.
 */
struct ResolvedDataType
{
    /** How the base type was determined. */
    enum class Resolution {
        /** Not resolved yet. */
        None = 0,
        /** The id itself is a namespace-0 base type. */
        BuiltIn,
        /** The id is a well-known namespace-0 subtype of a base type. */
        KnownSubtype,
        /** The base type was found by following inverse HasSubtype references. */
        Hierarchy,
        /** The base type could not be determined; see \c reason. */
        Unresolved
    };

    /** Normalized original DataType node id, for example "ns=3;i=3013". */
    QString typeId;
    /** BrowseName of the DataType node when known, for example "STRING". */
    QString typeName;
    /**
     * Name of the OPC UA base type: a built-in type ("String", "UInt16", ...),
     * an abstract base ("Structure", "Enumeration", "Number", ...); empty when
     * unresolved.
     */
    QString builtInBase;
    /** Supertype ids from the direct supertype up to the base type. */
    QStringList superTypeIds;
    /** How the base type was determined. */
    Resolution resolution = Resolution::None;
    /** Reason why the type is unresolved; empty otherwise. */
    QString reason;

    /** Returns whether a base type was determined. */
    bool isResolved() const
    {
        return resolution == Resolution::BuiltIn || resolution == Resolution::KnownSubtype
               || resolution == Resolution::Hierarchy;
    }
};

Q_DECLARE_METATYPE(ResolvedDataType)

/** Server-independent helpers for OPC UA DataType node ids. */
namespace DataTypeIds {

/** Returns \a nodeId in the "ns=<n>;<t>=<id>" form used by Qt OPC UA ("i=12" -> "ns=0;i=12"). */
QString normalize(const QString &nodeId);

/** Returns the name of namespace-0 base type \a typeId, or an empty string. */
QString baseTypeName(const QString &typeId);

/** Returns the base type name of well-known namespace-0 subtype \a typeId, or an empty string. */
QString knownSubtypeBase(const QString &typeId);

/** Resolves \a typeId from the namespace-0 tables only; Resolution::None when not covered. */
ResolvedDataType resolveLocally(const QString &typeId);

/**
 * Returns the type name embedded in a CODESYS DataType id such as
 * "ns=4;s=|type|PLC.Application.ST_HMI_Button", or an empty string.
 */
QString nameFromTypeId(const QString &typeId);

} // namespace DataTypeIds

/**
 * DataType supertype relations collected from a server.
 *
 * Pure value logic without server access: each entry records a type's direct
 * supertype as found through an inverse HasSubtype browse. resolve() walks the
 * chain until it reaches a namespace-0 type covered by DataTypeIds.
 */
class DataTypeHierarchy
{
public:
    /** Records that \a typeId (BrowseName \a typeName) is a direct subtype of \a superTypeId. */
    void addType(const QString &typeId, const QString &superTypeId, const QString &typeName);

    /** Records that the supertype of \a typeId could not be determined, with \a reason. */
    void addFailure(const QString &typeId, const QString &typeName, const QString &reason);

    /** Returns whether \a typeId has an entry (resolved or failed). */
    bool contains(const QString &typeId) const;

    /** Resolves \a typeId to its base type using the namespace-0 tables and the entries. */
    ResolvedDataType resolve(const QString &typeId) const;

private:
    /** One recorded type. */
    struct Entry
    {
        QString superTypeId; //!< Direct supertype; empty on failure.
        QString typeName;    //!< BrowseName of the type, when known.
        QString failure;     //!< Failure reason; empty on success.
    };

    QHash<QString, Entry> m_entries; //!< Entries keyed by normalized type id.
};

/**
 * Resolves DataType ids of a connected server to their OPC UA base types.
 *
 * Runs in the OPC UA worker thread that owns \a client. For every id the
 * namespace-0 tables do not cover, it reads the DataType node's BrowseName and
 * browses its inverse HasSubtype reference, repeating for the supertype until a
 * known base type is reached. Requests are bounded and guarded by a timeout;
 * finished() is emitted exactly once.
 */
class DataTypeResolver : public QObject
{
    Q_OBJECT

public:
    /** Creates a resolver that uses \a client. */
    explicit DataTypeResolver(QOpcUaClient *client, QObject *parent = nullptr);
    ~DataTypeResolver() override;

    /** Starts resolving \a typeIds; finished() is emitted when all are settled. */
    void resolve(const QStringList &typeIds);

    /** Returns the resolution of \a typeId with the data collected so far. */
    ResolvedDataType result(const QString &typeId) const;

signals:
    /** Emitted once when every requested id is resolved, failed or timed out. */
    void finished();

private:
    /** In-flight lookup of one DataType node. */
    struct Lookup
    {
        QOpcUaNode *node = nullptr; //!< Transient protocol node.
        QString typeName;           //!< BrowseName once read.
        QString superTypeId;        //!< Supertype once browsed.
        QString failure;            //!< Failure reason, if any.
        bool nameDone = false;      //!< BrowseName read settled.
        bool browseDone = false;    //!< Inverse browse settled.
    };

    void enqueue(const QString &typeId);
    void startNext();
    void startLookup(const QString &typeId);
    void completeLookup(const QString &typeId);
    void finishIfIdle();
    void finish();

    QOpcUaClient *m_client = nullptr;       //!< Client owning the protocol nodes.
    DataTypeHierarchy m_hierarchy;          //!< Collected supertype relations.
    QHash<QString, Lookup> m_inFlight;      //!< Lookups waiting for responses.
    QStringList m_queue;                    //!< Ids waiting for a free request slot.
    QSet<QString> m_seen;                   //!< Ids already queued or recorded.
    QTimer *m_timeout = nullptr;            //!< Safety timeout forcing completion.
    bool m_finished = false;                //!< Guards against double emission.
};

#endif // DATATYPERESOLVER_H
