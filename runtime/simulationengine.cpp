#include "simulationengine.h"

#include <cmath>

#include <QRandomGenerator>

#include "projectbuilder.h"

using ServerProject::Node;
using ServerProject::NodeKind;
using ServerProject::ProjectData;
using ServerProject::SimulationKind;

namespace {
/*! Base tick resolution; per-variable intervals are honored on top of this. */
constexpr int kTickResolutionMs = 50;
/*! Pi, defined locally because M_PI is not portable (absent on MSVC by default). */
constexpr double kPi = 3.14159265358979323846;
} // namespace

/*!
 * \brief Collects the simulated variables and starts the tick timer.
 */
SimulationEngine::SimulationEngine(UA_Server *server, const ProjectData &project, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
    const QHash<quint16, UA_UInt16> nsMap = ProjectBuilder::registerNamespaces(server, project);

    for (const Node &node : project.nodes) {
        if (node.kind != NodeKind::Variable || node.valueRank != -1)
            continue;
        if (node.simulation.kind == SimulationKind::Manual)
            continue;
        if (node.dataType == QLatin1String("String"))
            continue; // string values are not driven numerically

        SimVar var;
        var.nodeId = ProjectBuilder::toRuntimeNodeId(node.nodeId, nsMap);
        var.dataType = node.dataType;
        var.def = node.simulation;
        var.counter = node.simulation.min;
        m_vars.append(var);
    }

    if (!m_vars.isEmpty()) {
        m_clock.start();
        m_timer.setInterval(kTickResolutionMs);
        connect(&m_timer, &QTimer::timeout, this, &SimulationEngine::tick);
        m_timer.start();
    }
}

/*!
 * \brief Clears the owned runtime node ids.
 */
SimulationEngine::~SimulationEngine()
{
    for (SimVar &var : m_vars)
        UA_NodeId_clear(&var.nodeId);
}

/*!
 * \brief Updates every variable whose interval has elapsed.
 */
void SimulationEngine::tick()
{
    const qint64 now = m_clock.elapsed();
    for (SimVar &var : m_vars) {
        if (now - var.lastUpdateMs < static_cast<qint64>(var.def.intervalMs))
            continue;
        var.lastUpdateMs = now;
        writeScalar(var.nodeId, var.dataType, nextValue(var, now));
    }
}

/*!
 * \brief Computes the next value for \a var at engine time \a nowMs.
 */
double SimulationEngine::nextValue(SimVar &var, qint64 nowMs) const
{
    const ServerProject::SimulationDefinition &def = var.def;
    switch (def.kind) {
    case SimulationKind::Constant:
        return def.min;
    case SimulationKind::Counter:
        var.counter += def.step;
        if (var.counter > def.max)
            var.counter = def.min;
        return var.counter;
    case SimulationKind::Toggle:
        var.toggleState = !var.toggleState;
        return var.toggleState ? def.max : def.min;
    case SimulationKind::Random:
        return def.min + QRandomGenerator::global()->generateDouble() * (def.max - def.min);
    case SimulationKind::Sine: {
        const double period = def.periodMs > 0.0 ? def.periodMs : 1.0;
        const double phase = 2.0 * kPi * std::fmod(static_cast<double>(nowMs), period) / period;
        return def.min + (def.max - def.min) / 2.0 * (1.0 + std::sin(phase));
    }
    case SimulationKind::Ramp: {
        const double period = def.periodMs > 0.0 ? def.periodMs : 1.0;
        const double position = std::fmod(static_cast<double>(nowMs), period) / period;
        return def.min + (def.max - def.min) * position;
    }
    case SimulationKind::Manual:
        break;
    }
    return def.min;
}

/*!
 * \brief Writes \a value to \a nodeId coerced to \a dataType.
 */
void SimulationEngine::writeScalar(const UA_NodeId &nodeId, const QString &dataType, double value)
{
    UA_Variant variant;
    UA_Variant_init(&variant);

    if (dataType == QLatin1String("Boolean")) {
        UA_Boolean x = value >= 0.5;
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_BOOLEAN]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("SByte")) {
        UA_SByte x = static_cast<UA_SByte>(value);
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_SBYTE]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("Byte")) {
        UA_Byte x = static_cast<UA_Byte>(value);
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_BYTE]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("Int16")) {
        UA_Int16 x = static_cast<UA_Int16>(value);
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_INT16]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("UInt16")) {
        UA_UInt16 x = static_cast<UA_UInt16>(value);
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_UINT16]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("Int32")) {
        UA_Int32 x = static_cast<UA_Int32>(value);
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_INT32]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("UInt32")) {
        UA_UInt32 x = static_cast<UA_UInt32>(value);
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_UINT32]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("Int64")) {
        UA_Int64 x = static_cast<UA_Int64>(value);
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_INT64]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("UInt64")) {
        UA_UInt64 x = static_cast<UA_UInt64>(value);
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_UINT64]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("Float")) {
        UA_Float x = static_cast<UA_Float>(value);
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_FLOAT]);
        UA_Server_writeValue(m_server, nodeId, variant);
    } else if (dataType == QLatin1String("Double")) {
        UA_Double x = value;
        UA_Variant_setScalar(&variant, &x, &UA_TYPES[UA_TYPES_DOUBLE]);
        UA_Server_writeValue(m_server, nodeId, variant);
    }
}
