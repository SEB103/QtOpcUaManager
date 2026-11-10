#include "nodecontext.h"

#include <QStringList>

namespace {

/*!
 * \internal
 * \brief Marker in front of the symbol path of a CODESYS variable node id.
 */
constexpr QLatin1StringView kCodesysVariableMarker("|var|");

/*!
 * \internal
 * \brief Separator between the CODESYS device name and the application's symbols.
 */
constexpr QLatin1StringView kCodesysApplication(".Application.");

/*!
 * \internal
 * \brief Returns the parent of the CODESYS \a symbol whose own member is \a displayName.
 *
 * The node's own member is cut off as the known display name when the symbol
 * ends with it, so a member whose name contains brackets, such as an array
 * element "astItems[1]", is removed as a whole. Otherwise the last
 * dot-separated member is removed.
 */
QString codesysParent(const QString &symbol, const QString &displayName)
{
    const QString ownMember = QLatin1Char('.') + displayName;
    if (!displayName.isEmpty() && symbol.endsWith(ownMember))
        return symbol.left(symbol.size() - ownMember.size());

    const qsizetype lastDot = symbol.lastIndexOf(QLatin1Char('.'));
    return lastDot < 0 ? QString() : symbol.left(lastDot);
}

} // namespace

namespace NodeContext {

/*!
 * \brief Returns the parent path of the node \a nodeId at \a nodePath named \a displayName.
 *
 * CODESYS string node ids carry the variable's full symbol path in IEC
 * notation, which is exactly what a PLC programmer reads, so they are used
 * whenever present. The device name in front of the symbols is skipped: it is
 * the same for every variable of one controller.
 *
 * For other servers the browse path is used. Its leading "Objects" segment is
 * common to every node and therefore dropped, as is the node's own segment.
 */
QString contextOf(const QString &nodeId, const QString &nodePath, const QString &displayName)
{
    const qsizetype marker = nodeId.indexOf(kCodesysVariableMarker);
    if (marker >= 0) {
        QString symbol = nodeId.mid(marker + kCodesysVariableMarker.size());
        const qsizetype application = symbol.indexOf(kCodesysApplication);
        if (application >= 0) {
            symbol = symbol.mid(application + kCodesysApplication.size());
        } else {
            const qsizetype firstDot = symbol.indexOf(QLatin1Char('.'));
            symbol = firstDot < 0 ? QString() : symbol.mid(firstDot + 1);
        }
        return codesysParent(symbol, displayName);
    }

    QStringList segments = nodePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!segments.isEmpty() && segments.first() == QLatin1String("Objects"))
        segments.removeFirst();
    if (!segments.isEmpty())
        segments.removeLast();
    return segments.join(QLatin1Char('.'));
}

} // namespace NodeContext
