#ifndef BROWSEPROGRESSTEXT_H
#define BROWSEPROGRESSTEXT_H

#include <QString>

#include "core/clonebrowser.h"

/** Builds the progress text of an address-space browse shared by clone and snapshot. */
namespace BrowseProgressText {

/** Returns the multi-line, translated progress text for \a progress. */
QString render(const CloneProgress &progress);

} // namespace BrowseProgressText

#endif // BROWSEPROGRESSTEXT_H
