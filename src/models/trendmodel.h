#ifndef TRENDMODEL_H
#define TRENDMODEL_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/**
 * Bounded sample history of monitored values, and the geometry a trend plots.
 *
 * A subscription reports data changes, so the samples of one node are unevenly
 * spaced in time. The model keeps a capped ring of them per node — memory stays
 * flat in a long session — and converts a requested time window into widget
 * coordinates on demand.
 *
 * The coordinate mapping lives here rather than in the view because it runs over
 * thousands of points on every repaint, and because a pure function of samples,
 * window, and size can be tested, unlike the same arithmetic spread through QML
 * bindings.
 */
class TrendModel : public QObject
{
    Q_OBJECT

    /** Width of the plotted time window in milliseconds. */
    Q_PROPERTY(int windowMs READ windowMs WRITE setWindowMs NOTIFY windowMsChanged)

    /**
     * Whether the time axis is frozen.
     *
     * Pausing stops the axis from scrolling so a shape can be examined; samples
     * keep arriving and are still recorded, so nothing is lost while paused.
     */
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)

    /** Maximum number of samples kept per node. */
    Q_PROPERTY(int maximumSamples READ maximumSamples CONSTANT)

public:
    /** Creates an empty trend keeping at most \a maximumSamples samples per node. */
    explicit TrendModel(int maximumSamples = 3000, QObject *parent = nullptr);

    /** Returns the plotted time window in milliseconds. */
    int windowMs() const { return m_windowMs; }

    /** Sets the plotted time window to \a windowMs milliseconds. */
    void setWindowMs(int windowMs);

    /** Returns whether the time axis is frozen. */
    bool paused() const { return m_paused; }

    /** Sets whether the time axis is frozen. */
    void setPaused(bool paused);

    /** Returns the maximum number of samples kept per node. */
    int maximumSamples() const { return m_maximumSamples; }

    /**
     * Records \a value for \a nodeId at \a timeMs milliseconds since the epoch.
     *
     * Samples are expected in arrival order. One that is older than the newest
     * sample of its node is ignored, because a series that is not monotonic in
     * time cannot be drawn as a single line.
     */
    void appendSample(const QString &nodeId, qint64 timeMs, double value);

    /** Removes the series of \a nodeId. */
    void dropNode(const QString &nodeId);

    /** Removes every series. */
    void clear();

    /** Returns the right edge of the time axis in milliseconds since the epoch. */
    Q_INVOKABLE qint64 referenceTimeMs() const;

    /** Returns the number of samples currently kept for \a nodeId. */
    Q_INVOKABLE int sampleCountFor(const QString &nodeId) const;

    /**
     * Returns the value range covering every series in \a nodeIds inside the
     * current window, as a map with \c min, \c max, and \c hasData.
     *
     * A degenerate range — a constant value, or a single sample — is widened
     * symmetrically so the line is drawn across the middle of the plot instead
     * of collapsing onto one edge.
     */
    Q_INVOKABLE QVariantMap rangeFor(const QStringList &nodeIds) const;

    /**
     * Returns the polyline of \a nodeId in widget coordinates.
     *
     * \param nodeId Series to convert.
     * \param width Plot width in pixels; x grows to the right, now is at \a width.
     * \param height Plot height in pixels; y grows downward, \a yMax is at 0.
     * \param yMin Value mapped to the bottom edge.
     * \param yMax Value mapped to the top edge.
     * \param stepped Whether to insert the corner points of a square wave.
     *
     * The last sample before the window is included so the line enters from the
     * left edge rather than starting wherever the first visible sample happens
     * to be, and the last sample is extended to the right edge because a value
     * that reported no change is still that value. \a stepped exists for
     * booleans: a sloped segment between 0 and 1 would show states the variable
     * never had.
     */
    Q_INVOKABLE QList<QPointF> polylineFor(const QString &nodeId,
                                           qreal width,
                                           qreal height,
                                           double yMin,
                                           double yMax,
                                           bool stepped) const;

signals:
    /** Emitted when the plotted time window changes. */
    void windowMsChanged();

    /** Emitted when the frozen state of the time axis changes. */
    void pausedChanged();

    /** Emitted when samples were added, dropped, or cleared. */
    void seriesChanged();

private:
    /** One recorded value of one node. */
    struct Sample
    {
        /** Arrival time in milliseconds since the epoch. */
        qint64 timeMs {0};
        /** Recorded numeric value. */
        double value {0.0};
    };

    /** Samples per node id, oldest first. */
    QHash<QString, QList<Sample>> m_series;

    /** Maximum number of samples kept per node. */
    int m_maximumSamples;

    /** Plotted time window in milliseconds. */
    int m_windowMs {30000};

    /** Whether the time axis is frozen. */
    bool m_paused {false};

    /** Right edge of the time axis while paused. */
    qint64 m_frozenNowMs {0};
};

#endif // TRENDMODEL_H
