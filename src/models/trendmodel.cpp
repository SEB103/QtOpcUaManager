#include "trendmodel.h"

#include <QDateTime>
#include <cmath>

namespace {

/*!
 * \internal
 * \brief Fraction of the capacity dropped when a series is full.
 *
 * Trimming a batch keeps the amortized cost of one sample constant; removing a
 * single sample per arrival would move the whole backing array every time once
 * the cap is reached.
 */
constexpr int kTrimDivisor = 10;

/*!
 * \internal
 * \brief Half-width added to a value range that has collapsed to a single value.
 */
constexpr double kDegenerateRangePadding = 0.5;

/*!
 * \internal
 * \brief Smallest range treated as non-degenerate.
 */
constexpr double kRangeEpsilon = 1e-9;

} // namespace

/*!
 * \property TrendModel::windowMs
 * \brief Width of the plotted time window in milliseconds.
 */

/*!
 * \property TrendModel::paused
 * \brief Whether the time axis is frozen.
 */

/*!
 * \property TrendModel::maximumSamples
 * \brief Maximum number of samples kept per node.
 */

/*!
 * \brief Creates an empty trend.
 * \param maximumSamples Samples kept per node; clamped to at least 10.
 * \param parent Optional QObject parent.
 */
TrendModel::TrendModel(int maximumSamples, QObject *parent)
    : QObject(parent)
    , m_maximumSamples(qMax(10, maximumSamples))
{}

/*!
 * \brief Sets the plotted time window to \a windowMs milliseconds.
 */
void TrendModel::setWindowMs(int windowMs)
{
    const int clamped = qMax(1000, windowMs);
    if (m_windowMs == clamped)
        return;

    m_windowMs = clamped;
    emit windowMsChanged();
    emit seriesChanged();
}

/*!
 * \brief Sets whether the time axis is frozen.
 */
void TrendModel::setPaused(bool paused)
{
    if (m_paused == paused)
        return;

    m_paused = paused;
    // Capture the edge on the way in so the visible window stops moving; on the
    // way out the axis resumes from the real clock and the samples recorded in
    // between are already there.
    if (m_paused)
        m_frozenNowMs = QDateTime::currentMSecsSinceEpoch();

    emit pausedChanged();
    emit seriesChanged();
}

/*!
 * \brief Records \a value for \a nodeId at \a timeMs.
 */
void TrendModel::appendSample(const QString &nodeId, qint64 timeMs, double value)
{
    if (nodeId.isEmpty() || !std::isfinite(value))
        return;

    QList<Sample> &samples = m_series[nodeId];

    // A sample older than the newest one would fold the line back on itself.
    if (!samples.isEmpty() && timeMs < samples.last().timeMs)
        return;

    if (samples.size() >= m_maximumSamples) {
        const int trimCount = qMax(1, m_maximumSamples / kTrimDivisor);
        samples.remove(0, trimCount);
    }

    samples.push_back(Sample{timeMs, value});
    emit seriesChanged();
}

/*!
 * \brief Removes the series of \a nodeId.
 */
void TrendModel::dropNode(const QString &nodeId)
{
    if (m_series.remove(nodeId) > 0)
        emit seriesChanged();
}

/*!
 * \brief Removes every series.
 */
void TrendModel::clear()
{
    if (m_series.isEmpty())
        return;

    m_series.clear();
    emit seriesChanged();
}

/*!
 * \brief Returns the right edge of the time axis in milliseconds since the epoch.
 */
qint64 TrendModel::referenceTimeMs() const
{
    return m_paused ? m_frozenNowMs : QDateTime::currentMSecsSinceEpoch();
}

/*!
 * \brief Returns the number of samples currently kept for \a nodeId.
 */
int TrendModel::sampleCountFor(const QString &nodeId) const
{
    const auto it = m_series.constFind(nodeId);
    return it == m_series.constEnd() ? 0 : int(it.value().size());
}

/*!
 * \brief Returns the value range covering \a nodeIds inside the current window.
 */
QVariantMap TrendModel::rangeFor(const QStringList &nodeIds) const
{
    const qint64 reference = referenceTimeMs();
    const qint64 start = reference - m_windowMs;

    bool hasData = false;
    double minimum = 0.0;
    double maximum = 0.0;

    for (const QString &nodeId : nodeIds) {
        const auto it = m_series.constFind(nodeId);
        if (it == m_series.constEnd())
            continue;

        for (const Sample &sample : it.value()) {
            if (sample.timeMs < start)
                continue;
            if (!hasData) {
                minimum = sample.value;
                maximum = sample.value;
                hasData = true;
                continue;
            }
            minimum = qMin(minimum, sample.value);
            maximum = qMax(maximum, sample.value);
        }
    }

    if (hasData && (maximum - minimum) < kRangeEpsilon) {
        minimum -= kDegenerateRangePadding;
        maximum += kDegenerateRangePadding;
    }

    return QVariantMap {
        {QStringLiteral("min"), minimum},
        {QStringLiteral("max"), maximum},
        {QStringLiteral("hasData"), hasData}
    };
}

/*!
 * \brief Returns the polyline of \a nodeId in widget coordinates.
 */
QList<QPointF> TrendModel::polylineFor(const QString &nodeId,
                                       qreal width,
                                       qreal height,
                                       double yMin,
                                       double yMax,
                                       bool stepped) const
{
    QList<QPointF> points;

    const auto it = m_series.constFind(nodeId);
    if (it == m_series.constEnd() || it.value().isEmpty())
        return points;
    if (width <= 0.0 || height <= 0.0 || (yMax - yMin) < kRangeEpsilon)
        return points;

    const QList<Sample> &samples = it.value();
    const qint64 reference = referenceTimeMs();
    const qint64 start = reference - m_windowMs;

    const auto toPoint = [&](const Sample &sample) {
        const qreal x = qreal(sample.timeMs - start) / qreal(m_windowMs) * width;
        const qreal y = height - qreal((sample.value - yMin) / (yMax - yMin)) * height;
        return QPointF(x, y);
    };

    // Start one sample before the window so the line enters from the left edge
    // instead of beginning wherever the first visible sample happens to fall.
    int firstIndex = 0;
    for (int i = 0; i < samples.size(); ++i) {
        if (samples.at(i).timeMs >= start) {
            firstIndex = qMax(0, i - 1);
            break;
        }
        firstIndex = i;
    }

    for (int i = firstIndex; i < samples.size(); ++i) {
        const QPointF point = toPoint(samples.at(i));
        if (stepped && !points.isEmpty()) {
            // Hold the previous level until the instant the value changed.
            points.append(QPointF(point.x(), points.last().y()));
        }
        points.append(point);
    }

    if (points.isEmpty())
        return points;

    // A subscription reports changes, so silence means the value still holds.
    // Extending to the right edge shows that, instead of a line that stops.
    const qreal rightEdge = width;
    if (points.last().x() < rightEdge)
        points.append(QPointF(rightEdge, points.last().y()));

    return points;
}
