#ifndef SNAPSHOTVALUECODEC_H
#define SNAPSHOTVALUECODEC_H

#include <QJsonValue>
#include <QString>
#include <QVariant>

/**
 * Encodes OPC UA values read by Qt OPC UA as typed JSON for address-space snapshots.
 *
 * The encoding never loses information silently: 64-bit integers are strings,
 * DateTime is ISO-8601 UTC with milliseconds, ByteString is Base64,
 * LocalizedText and QualifiedName are objects, multi-dimensional arrays carry
 * their dimensions, and a value of a type without a JSON form is marked as
 * unsupported together with its text.
 */
namespace SnapshotValueCodec {

/** Returns the JSON form of \a value; an invalid value yields null. */
QJsonValue encode(const QVariant &value);

/**
 * Returns the JSON form of \a value for a variable with \a valueRank.
 * A one-element array that Qt OPC UA delivers as a scalar is restored as an array.
 */
QJsonValue encodeForValueRank(const QVariant &value, int valueRank);

/**
 * Returns the OPC UA built-in type name of \a value as received, e.g. "String";
 * for arrays the element type. Empty for an invalid or empty value.
 */
QString variantTypeName(const QVariant &value);

} // namespace SnapshotValueCodec

#endif // SNAPSHOTVALUECODEC_H
