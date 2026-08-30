#pragma once

#include <QObject>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QWaitCondition>
#include <QSet>
#include <QStringList>

#include <atomic>

class FileProcessor final : public QObject
{
    Q_OBJECT

public:
    enum class ConflictPolicy {
        Overwrite,
        AddCounter
    };
    Q_ENUM(ConflictPolicy)

    explicit FileProcessor(QObject *parent = nullptr);

public slots:
    void configure(const QString &inputDir,
                   const QStringList &nameFilters,
                   const QString &outputDir,
                   bool removeSource,
                   ConflictPolicy conflictPolicy,
                   const QByteArray &xorKey,
                   bool periodic,
                   int intervalMs);

    void start();
    void pause();
    void resume();
    void stop();

signals:
    void started();
    void finished();
    void pausedChanged(bool paused);
    void status(const QString &text);
    void error(const QString &text);
    void fileStarted(const QString &inputPath, const QString &outputPath, qint64 totalBytes);
    void fileProgress(qint64 processedBytes, qint64 totalBytes);
    void fileFinished(const QString &inputPath, const QString &outputPath, qint64 bytesProcessed);
    void scanStats(int discovered, int eligible);

private:
    bool shouldStop() const;
    bool waitIfPausedOrStopped();
    bool waitForNextScan();
    QList<QFileInfo> discoverFiles() const;
    bool processFile(const QFileInfo &inputInfo);

    QString makeOutputPath(const QFileInfo &inputInfo);
    QString uniqueCounterPath(const QString &basePath) const;
    bool writeAll(class QFile &file, const char *data, qint64 size, QString *errorText);
    bool xorBuffer(char *data, qint64 size);

    static bool isLikelySamePath(const QString &a, const QString &b);
    static QString formatBytes(qint64 bytes);

    QString m_inputDir;
    QStringList m_nameFilters;
    QString m_outputDir;
    bool m_removeSource = false;
    ConflictPolicy m_conflictPolicy = ConflictPolicy::AddCounter;
    QByteArray m_xorKey;
    bool m_periodic = false;
    int m_intervalMs = 5000;

    mutable QMutex m_stateMutex;
    QWaitCondition m_controlWait;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_pauseRequested{false};

    QSet<QString> m_processedSignatures;
    QSet<QString> m_generatedOutputPaths;
};
