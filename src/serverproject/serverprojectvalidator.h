#ifndef SERVERPROJECTVALIDATOR_H
#define SERVERPROJECTVALIDATOR_H

#include <QStringList>

#include "serverprojectdata.h"

namespace ServerProject {

/**
 * Validates a ProjectData before it is used to build a server.
 *
 * The runtime runs this before opening a socket so a malformed project fails
 * fast with a clear message instead of producing a broken address space. The
 * editor uses it to warn the user. Validation is pure and has no side effects.
 */
class Validator
{
public:
    /** Outcome of validation: \c ok plus a list of human-readable errors. */
    struct Result {
        bool ok = false;
        QStringList errors;
    };

    /** Validates \a data and returns the collected errors. */
    static Result validate(const ProjectData &data);
};

} // namespace ServerProject

#endif // SERVERPROJECTVALIDATOR_H
