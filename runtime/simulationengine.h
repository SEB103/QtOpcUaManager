#ifndef SIMULATIONENGINE_H
#define SIMULATIONENGINE_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <open62541/server.h>

#include "serverproject/serverprojectdata.h"

/**
 * Drives simulated variable values on a running open62541 server.
 *
 * For every scalar variable whose project simulation is not Manual, the engine
 * computes a new value on a timer and writes it with UA_Server_writeValue, which
 * emits ordinary DataChange notifications to subscribed clients. The engine owns
 * the value generation entirely; the GUI only edits the definitions.
 */
class SimulationEngine : public QObject
{
    Q_OBJECT

public:
    /** Creates the engine for \a project's simulated variables on \a server. */
    SimulationEngine(UA_Server *server, const ServerProject::ProjectData &project,
                     QObject *parent = nullptr);
    ~SimulationEngine() override;

    /** Returns whether any variable is being simulated. */
    bool hasWork() const { return !m_vars.isEmpty(); }

private:
    /** Per-variable simulation state. */
    struct SimVar {
        UA_NodeId nodeId;                        /**< Owned runtime node id. */
        QString dataType;                        /**< Built-in data type name. */
        ServerProject::SimulationDefinition def; /**< Value-source definition. */
        qint64 lastUpdateMs = 0;                 /**< Engine-clock time of last write. */
        double counter = 0.0;                    /**< Running Counter value. */
        bool toggleState = false;                /**< Running Toggle state. */
    };

    /** Updates every variable whose interval has elapsed. */
    void tick();

    /** Computes the next value for \a var at engine time \a nowMs. */
    double nextValue(SimVar &var, qint64 nowMs) const;

    /** Writes \a value to \a nodeId coerced to \a dataType. */
    void writeScalar(const UA_NodeId &nodeId, const QString &dataType, double value);

    /** The server whose values are driven; not owned. */
    UA_Server *m_server = nullptr;

    /** Simulated variables. */
    QList<SimVar> m_vars;

    /** Master tick timer. */
    QTimer m_timer;

    /** Monotonic engine clock. */
    QElapsedTimer m_clock;
};

#endif // SIMULATIONENGINE_H
