#pragma once

#include "fileprocessor.h"

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QLabel;
class QTextEdit;
class QThread;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void browseInputDirectory();
    void browseOutputDirectory();
    void togglePeriodic(bool checked);
    void startProcessing();
    void pauseProcessing();
    void resumeProcessing();
    void stopProcessing();

    void onProcessorStarted();
    void onProcessorFinished();
    void onProcessorPausedChanged(bool paused);
    void onStatus(const QString &text);
    void onError(const QString &text);
    void onFileStarted(const QString &inputPath, const QString &outputPath, qint64 totalBytes);
    void onFileProgress(qint64 processedBytes, qint64 totalBytes);
    void onFileFinished(const QString &inputPath, const QString &outputPath, qint64 bytesProcessed);
    void onScanStats(int discovered, int eligible);

private:
    void buildUi();
    void setRunningUi(bool running);
    void setControlsEnabled(bool enabled);
    void appendLog(const QString &text, bool isError = false);
    bool readConfiguration(QString *errorText);
    QByteArray parseXorKey(bool *ok) const;
    QStringList parseFilters() const;

    QLineEdit *m_inputDirEdit = nullptr;
    QLineEdit *m_filtersEdit = nullptr;
    QCheckBox *m_removeSourceCheck = nullptr;
    QLineEdit *m_outputDirEdit = nullptr;
    QComboBox *m_conflictCombo = nullptr;
    QLineEdit *m_keyEdit = nullptr;
    QRadioButton *m_onceRadio = nullptr;
    QRadioButton *m_periodicRadio = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QPushButton *m_resumeButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_currentFileLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QTextEdit *m_log = nullptr;

    QThread *m_thread = nullptr;
    FileProcessor *m_processor = nullptr;
    bool m_closing = false;
};
