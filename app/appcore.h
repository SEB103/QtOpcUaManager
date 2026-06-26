#ifndef APPCORE_H
#define APPCORE_H

#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>
#include <QString>
#include <QVariant>

#include <limits>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

/*! QGuiApplication subclass with small helpers for QSettings and logging output. */
class AppCore : public QGuiApplication
{
    Q_OBJECT
public:
    /*! Creates the application object. */
    explicit AppCore(int& argc, char** argv);
    /*! Destroys the application object. */
    ~AppCore();

    /*! Blocks the current thread for \a msec milliseconds. */
    void delay(int msec);

    /*! Returns a random unsigned integer in the inclusive range [\a min, \a max]. */
    unsigned int randint(unsigned int min = 0, unsigned int max = (std::numeric_limits<unsigned int>::max)());

    /*! Creates an INI-based QSettings instance (lazy; only once). */
    bool createSettings(const QString& appName, const QString& iniFileDir);

    /*! Writes one setting value under \a group and optional \a subdir. */
    void writeSettings(const QVariant& value, const QString& name, const QString& group, const QString& subdir = "");

    /*! Removes one setting value under \a group and optional \a subdir. */
    void removeSettings(const QString& name, const QString& group, const QString& subdir = "");

    /*! Reads one setting value under \a group and optional \a subdir. */
    QVariant readSettings(const QVariant& defaultValue, const QString& name, const QString& group, const QString& subdir = "") const;

    /*! Reads a value; if missing, writes and returns \a defaultValue. */
    QVariant readOrSetSettings(const QVariant &defaultValue, const QString &name, const QString &group, const QString &subdir = "");

    /*! Returns the underlying QSettings instance (may be nullptr). */
    QSettings* settings();

    /*! Installs the default Qt message pattern for console output. */
    static void setMessagePattern();

private:
    QSettings* m_settings {};
};

#endif // APPCORE_H
