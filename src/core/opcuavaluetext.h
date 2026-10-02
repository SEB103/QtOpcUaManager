#ifndef OPCUAVALUETEXT_H
#define OPCUAVALUETEXT_H

#include <QStringList>
#include <QVariant>

#include "core/opcuavaluedata.h"

/**
 * Returns the coarse kind of the OPC UA value \a value.
 *
 * Booleans and numeric types map to their own kinds, an invalid value or one
 * without text maps to OpcUaValueKind::Empty, and everything else is
 * OpcUaValueKind::Text. An array value reports the kind of its first non-empty
 * element, because OPC UA arrays hold a single data type.
 */
OpcUaValueKind opcUaValueKind(const QVariant &value);

/**
 * Returns the display text of every element of the array value \a value.
 *
 * Returns an empty list for a scalar value, a string, or an empty array.
 */
QStringList opcUaArrayElementTexts(const QVariant &value);

#endif // OPCUAVALUETEXT_H
