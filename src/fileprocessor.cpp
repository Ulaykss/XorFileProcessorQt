#include "fileprocessor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <QThread>

#include <algorithm>
#include <cstdint>

namespace {
constexpr qint64 kChunkSize = 8LL * 1024LL * 1024LL;
constexpr qint64 kProgressGranularity = 1024LL * 1024LL;
}

FileProcessor::FileProcessor(QObject *parent)
    : QObject(parent)
{
}


void FileProcessor::configure(const QString &inputDir,
                              const QStringList &nameFilters,
                              const QString &outputDir,
                              bool removeSource,
                              ConflictPolicy conflictPolicy,
                              const QByteArray &xorKey,
                              bool periodic,
                              int intervalMs)
{
    QMutexLocker locker(&m_stateMutex);
    m_inputDir = QDir::cleanPath(QDir::fromNativeSeparators(inputDir));
    m_nameFilters = nameFilters;
    m_outputDir = QDir::cleanPath(QDir::fromNativeSeparators(outputDir));
    m_removeSource = removeSource;
    m_conflictPolicy = conflictPolicy;
    m_xorKey = xorKey;
    m_periodic = periodic;
    m_intervalMs = std::max(intervalMs, 250);
    m_processedSignatures.clear();
    m_generatedOutputPaths.clear();
}

void FileProcessor::start()
{
    m_stopRequested.store(false);
    m_pauseRequested.store(false);
    emit started();
    emit pausedChanged(false);

    if (m_inputDir.isEmpty() || m_outputDir.isEmpty() || m_xorKey.size() != 8) {
        emit error(QStringLiteral("Некорректная конфигурация обработки."));
        emit finished();
        return;
    }

    QDir outDir(m_outputDir);
    if (!outDir.exists() && !outDir.mkpath(QStringLiteral("."))) {
        emit error(QStringLiteral("Не удалось создать каталог назначения: %1").arg(m_outputDir));
        emit finished();
        return;
    }

    emit status(m_periodic
                    ? QStringLiteral("Мониторинг входного каталога запущен.")
                    : QStringLiteral("Однократное сканирование запущено."));

    bool firstScan = true;
    while (!m_stopRequested.load()) {
        if (!firstScan && !m_periodic) {
            break;
        }

        const QList<QFileInfo> files = discoverFiles();
        int eligible = 0;

        for (const QFileInfo &info : files) {
            if (m_stopRequested.load()) {
                break;
            }

            const QString normalizedPath = QDir::cleanPath(info.absoluteFilePath());
            if (m_generatedOutputPaths.contains(normalizedPath)) {
                continue;
            }

            const QString signature = QStringLiteral("%1|%2|%3")
                                          .arg(normalizedPath)
                                          .arg(info.size())
                                          .arg(info.lastModified().toMSecsSinceEpoch());
            if (m_processedSignatures.contains(signature)) {
                continue;
            }

            ++eligible;
            if (!processFile(info)) {
                if (m_stopRequested.load()) {
                    break;
                }
            }
        }

        emit scanStats(files.size(), eligible);
        firstScan = false;

        if (!m_periodic || m_stopRequested.load()) {
            break;
        }

        emit status(QStringLiteral("Ожидание следующего опроса: %1 мс.").arg(m_intervalMs));
        if (!waitForNextScan()) {
            break;
        }
    }

    emit status(QStringLiteral("Обработка остановлена."));
    emit finished();
}

void FileProcessor::pause()
{
    if (!m_stopRequested.load()) {
        m_pauseRequested.store(true);
        emit pausedChanged(true);
        emit status(QStringLiteral("Обработка приостановлена."));
        m_controlWait.wakeAll();
    }
}

void FileProcessor::resume()
{
    m_pauseRequested.store(false);
    emit pausedChanged(false);
    emit status(QStringLiteral("Обработка продолжена."));
    m_controlWait.wakeAll();
}

void FileProcessor::stop()
{
    m_stopRequested.store(true);
    m_pauseRequested.store(false);
    m_controlWait.wakeAll();
}

bool FileProcessor::shouldStop() const
{
    return m_stopRequested.load();
}

bool FileProcessor::waitIfPausedOrStopped()
{
    QMutexLocker locker(&m_stateMutex);
    while (m_pauseRequested.load() && !m_stopRequested.load()) {
        m_controlWait.wait(&m_stateMutex);
    }
    return !m_stopRequested.load();
}

bool FileProcessor::waitForNextScan()
{
    QMutexLocker locker(&m_stateMutex);
    const bool wokeNormally = m_controlWait.wait(&m_stateMutex, m_intervalMs);
    Q_UNUSED(wokeNormally);
    return !m_stopRequested.load();
}

QList<QFileInfo> FileProcessor::discoverFiles() const
{
    QDir dir(m_inputDir);
    if (!dir.exists()) {
        emit const_cast<FileProcessor *>(this)->error(
            QStringLiteral("Входной каталог не существует: %1").arg(m_inputDir));
        return {};
    }

    QDir::Filters filters = QDir::Files | QDir::Readable | QDir::NoSymLinks;
    return dir.entryInfoList(m_nameFilters, filters, QDir::Name | QDir::IgnoreCase);
}

bool FileProcessor::processFile(const QFileInfo &inputInfo)
{
    if (!waitIfPausedOrStopped()) {
        return false;
    }

    QFile input(inputInfo.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly)) {
        emit error(QStringLiteral("Не удалось открыть входной файл: %1 (%2)")
                       .arg(inputInfo.absoluteFilePath(), input.errorString()));
        return false;
    }

    // A same-path destination plus source deletion would remove the newly produced file.
    const QString finalPath = makeOutputPath(inputInfo);
    if (finalPath.isEmpty()) {
        return false;
    }
    if (isLikelySamePath(inputInfo.absoluteFilePath(), finalPath) && m_removeSource) {
        emit error(QStringLiteral("Входной и выходной файл совпадают. Удаление входного файла в этом режиме запрещено: %1")
                       .arg(finalPath));
        return false;
    }

    const QString tempPath = QDir(QFileInfo(finalPath).absolutePath())
                                 .filePath(QStringLiteral(".%1.%2.part")
                                               .arg(QFileInfo(finalPath).fileName())
                                               .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    QFile output(tempPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit error(QStringLiteral("Не удалось создать временный файл: %1 (%2)")
                       .arg(tempPath, output.errorString()));
        return false;
    }

    const qint64 total = input.size();
    emit fileStarted(inputInfo.absoluteFilePath(), finalPath, total);

    QByteArray buffer;
    buffer.resize(static_cast<int>(kChunkSize));

    qint64 processed = 0;
    qint64 lastReported = 0;
    bool ok = true;
    QString failure;

    while (processed < total) {
        if (!waitIfPausedOrStopped()) {
            ok = false;
            failure = QStringLiteral("Операция остановлена пользователем.");
            break;
        }

        const qint64 toRead = std::min(kChunkSize, total - processed);
        const qint64 readBytes = input.read(buffer.data(), toRead);
        if (readBytes <= 0) {
            ok = false;
            failure = QStringLiteral("Ошибка чтения: %1").arg(input.errorString());
            break;
        }

        xorBuffer(buffer.data(), readBytes);
        if (!writeAll(output, buffer.constData(), readBytes, &failure)) {
            ok = false;
            break;
        }

        processed += readBytes;
        if (processed - lastReported >= kProgressGranularity || processed == total) {
            emit fileProgress(processed, total);
            lastReported = processed;
        }
    }

    if (!output.flush() && ok) {
        ok = false;
        failure = QStringLiteral("Ошибка записи: %1").arg(output.errorString());
    }
    output.close();
    input.close();

    if (m_stopRequested.load() && !ok && failure.isEmpty()) {
        failure = QStringLiteral("Операция остановлена пользователем.");
    }

    if (!ok) {
        QFile::remove(tempPath);
        if (!m_stopRequested.load()) {
            emit error(QStringLiteral("Файл '%1' не обработан: %2")
                           .arg(inputInfo.fileName(), failure));
        }
        return false;
    }

    // Finalize in the same directory so rename is atomic on the same volume.
    if (isLikelySamePath(inputInfo.absoluteFilePath(), finalPath)) {
        if (!QFile::remove(finalPath)) {
            emit error(QStringLiteral("Не удалось заменить исходный файл '%1'.").arg(finalPath));
            QFile::remove(tempPath);
            return false;
        }
    } else if (m_conflictPolicy == ConflictPolicy::Overwrite && QFile::exists(finalPath)) {
        if (!QFile::remove(finalPath)) {
            emit error(QStringLiteral("Не удалось перезаписать существующий файл '%1'.").arg(finalPath));
            QFile::remove(tempPath);
            return false;
        }
    }

    if (!QFile::rename(tempPath, finalPath)) {
        emit error(QStringLiteral("Не удалось переместить временный файл в '%1'.").arg(finalPath));
        QFile::remove(tempPath);
        return false;
    }
    m_generatedOutputPaths.insert(QDir::cleanPath(QFileInfo(finalPath).absoluteFilePath()));

    if (m_removeSource && !isLikelySamePath(inputInfo.absoluteFilePath(), finalPath)) {
        if (!QFile::remove(inputInfo.absoluteFilePath())) {
            emit error(QStringLiteral("Файл обработан, но не удалось удалить исходник '%1': %2")
                           .arg(inputInfo.absoluteFilePath(), QFile(inputInfo.absoluteFilePath()).errorString()));
        }
    }

    // Mark the source as consumed. For same-path processing we use the resulting file signature,
    // otherwise the input signature is sufficient to avoid duplicate timer processing.
    QFileInfo resultingInfo(finalPath);
    const QString consumedSignature = QStringLiteral("%1|%2|%3")
                                           .arg(QDir::cleanPath(inputInfo.absoluteFilePath()))
                                           .arg(isLikelySamePath(inputInfo.absoluteFilePath(), finalPath)
                                                    ? resultingInfo.size()
                                                    : inputInfo.size())
                                           .arg(isLikelySamePath(inputInfo.absoluteFilePath(), finalPath)
                                                    ? resultingInfo.lastModified().toMSecsSinceEpoch()
                                                    : inputInfo.lastModified().toMSecsSinceEpoch());
    m_processedSignatures.insert(consumedSignature);

    emit fileFinished(inputInfo.absoluteFilePath(), finalPath, total);
    emit status(QStringLiteral("Готово: %1 (%2)").arg(inputInfo.fileName(), formatBytes(total)));
    return true;
}

QString FileProcessor::makeOutputPath(const QFileInfo &inputInfo)
{
    QDir outDir(m_outputDir);
    QString finalPath = outDir.filePath(inputInfo.fileName());

    if (m_conflictPolicy == ConflictPolicy::AddCounter &&
        QFile::exists(finalPath) && !isLikelySamePath(inputInfo.absoluteFilePath(), finalPath)) {
        finalPath = uniqueCounterPath(finalPath);
    }
    return QDir::cleanPath(finalPath);
}

QString FileProcessor::uniqueCounterPath(const QString &basePath) const
{
    QFileInfo fi(basePath);
    const QString stem = fi.completeBaseName();
    const QString suffix = fi.suffix().isEmpty() ? QString() : QStringLiteral(".") + fi.suffix();

    for (int counter = 1; counter < 1000000; ++counter) {
        const QString candidate = fi.dir().filePath(QStringLiteral("%1_%2%3").arg(stem).arg(counter).arg(suffix));
        if (!QFile::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool FileProcessor::writeAll(QFile &file, const char *data, qint64 size, QString *errorText)
{
    qint64 written = 0;
    while (written < size) {
        if (shouldStop()) {
            if (errorText) {
                *errorText = QStringLiteral("Операция остановлена пользователем.");
            }
            return false;
        }

        const qint64 n = file.write(data + written, size - written);
        if (n <= 0) {
            if (errorText) {
                *errorText = QStringLiteral("Ошибка записи: %1").arg(file.errorString());
            }
            return false;
        }
        written += n;
    }
    return true;
}

bool FileProcessor::xorBuffer(char *data, qint64 size)
{
    if (m_xorKey.size() != 8) {
        return false;
    }

    for (qint64 i = 0; i < size; ++i) {
        data[i] = static_cast<char>(static_cast<unsigned char>(data[i]) ^
                                    static_cast<unsigned char>(m_xorKey.at(static_cast<int>(i % 8))));
    }
    return true;
}

bool FileProcessor::isLikelySamePath(const QString &a, const QString &b)
{
    const QString left = QDir::cleanPath(QFileInfo(a).absoluteFilePath());
    const QString right = QDir::cleanPath(QFileInfo(b).absoluteFilePath());
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

QString FileProcessor::formatBytes(qint64 bytes)
{
    static const QStringList units = {QStringLiteral("B"), QStringLiteral("KiB"),
                                      QStringLiteral("MiB"), QStringLiteral("GiB"), QStringLiteral("TiB")};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(QString::number(value, 'f', unit == 0 ? 0 : 2), units.at(unit));
}
