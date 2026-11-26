#include "browseprogresstext.h"

#include <QCoreApplication>

/*!
    \namespace BrowseProgressText
    \inmodule OpcUaManager
    \brief Builds the progress text of an address-space browse shared by clone and snapshot.
*/

/*!
 * \brief Returns the multi-line, translated progress text for \a progress.
 *
 * The breakdown shows where a slow browse waits: browsing, metadata or value
 * reads, or DataType lookups. The text is built in the active UI language.
 */
QString BrowseProgressText::render(const CloneProgress &progress)
{
    QString text =
        QCoreApplication::translate("BrowseProgressText", "%1 node(s) captured")
            .arg(progress.captured);
    text += QLatin1Char('\n')
            + QCoreApplication::translate("BrowseProgressText",
                                          "Queued: %1 to browse, %2 metadata reads, %3 value "
                                          "reads, %4 waiting for data types")
                  .arg(progress.browseQueued)
                  .arg(progress.metadataQueued)
                  .arg(progress.valuesQueued)
                  .arg(progress.waitingForType);
    text += QLatin1Char('\n')
            + QCoreApplication::translate("BrowseProgressText",
                                          "In progress: %1 browse(s), %2 read request(s)")
                  .arg(progress.browsing)
                  .arg(progress.readRequests);
    if (progress.typeLookups > 0) {
        text += QLatin1Char('\n')
                + QCoreApplication::translate("BrowseProgressText", "Looking up %1 data type(s)")
                      .arg(progress.typeLookups);
    }
    return text;
}
