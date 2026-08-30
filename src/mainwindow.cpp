#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTextEdit>
#include <QThread>
#include <QFileInfo>
#include <QTime>
#include <algorithm>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("XOR File Processor"));
    resize(900, 700);
    buildUi();

    m_thread = new QThread(this);
    m_processor = new FileProcessor();
    m_processor->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_processor, &QObject::deleteLater);
    connect(m_processor, &FileProcessor::started, this, &MainWindow::onProcessorStarted);
    connect(m_processor, &FileProcessor::finished, this, &MainWindow::onProcessorFinished);
    connect(m_processor, &FileProcessor::pausedChanged, this, &MainWindow::onProcessorPausedChanged);
    connect(m_processor, &FileProcessor::status, this, &MainWindow::onStatus);
    connect(m_processor, &FileProcessor::error, this, &MainWindow::onError);
    connect(m_processor, &FileProcessor::fileStarted, this, &MainWindow::onFileStarted);
    connect(m_processor, &FileProcessor::fileProgress, this, &MainWindow::onFileProgress);
    connect(m_processor, &FileProcessor::fileFinished, this, &MainWindow::onFileFinished);
    connect(m_processor, &FileProcessor::scanStats, this, &MainWindow::onScanStats);

    m_thread->start();
}

MainWindow::~MainWindow()
{
    if (m_thread && m_thread->isRunning()) {
        m_processor->stop();
        m_thread->quit();
        m_thread->wait();
    }
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);

    auto *inputBox = new QGroupBox(QStringLiteral("Входные файлы"), central);
    auto *inputForm = new QFormLayout(inputBox);

    auto *inputRow = new QWidget(inputBox);
    auto *inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    m_inputDirEdit = new QLineEdit(inputRow);
    auto *inputBrowse = new QPushButton(QStringLiteral("Обзор…"), inputRow);
    inputLayout->addWidget(m_inputDirEdit, 1);
    inputLayout->addWidget(inputBrowse);
    inputForm->addRow(QStringLiteral("Каталог:"), inputRow);

    m_filtersEdit = new QLineEdit(QStringLiteral("*.txt;*.bin"), inputBox);
    m_filtersEdit->setToolTip(QStringLiteral("Маски через ';', например: *.txt;*.bin;*.dat"));
    inputForm->addRow(QStringLiteral("Маски файлов:"), m_filtersEdit);

    m_removeSourceCheck = new QCheckBox(QStringLiteral("Удалять входной файл после успешной обработки"), inputBox);
    inputForm->addRow(QString(), m_removeSourceCheck);

    auto *outputBox = new QGroupBox(QStringLiteral("Выход"), central);
    auto *outputForm = new QFormLayout(outputBox);

    auto *outputRow = new QWidget(outputBox);
    auto *outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    m_outputDirEdit = new QLineEdit(outputRow);
    auto *outputBrowse = new QPushButton(QStringLiteral("Обзор…"), outputRow);
    outputLayout->addWidget(m_outputDirEdit, 1);
    outputLayout->addWidget(outputBrowse);
    outputForm->addRow(QStringLiteral("Каталог:"), outputRow);

    m_conflictCombo = new QComboBox(outputBox);
    m_conflictCombo->addItem(QStringLiteral("Перезаписывать существующий файл"),
                             static_cast<int>(FileProcessor::ConflictPolicy::Overwrite));
    m_conflictCombo->addItem(QStringLiteral("Добавлять счетчик к имени"),
                             static_cast<int>(FileProcessor::ConflictPolicy::AddCounter));
    m_conflictCombo->setCurrentIndex(1);
    outputForm->addRow(QStringLiteral("Конфликт имен:"), m_conflictCombo);

    auto *operationBox = new QGroupBox(QStringLiteral("Операция XOR"), central);
    auto *operationForm = new QFormLayout(operationBox);

    m_keyEdit = new QLineEdit(operationBox);
    m_keyEdit->setPlaceholderText(QStringLiteral("1234567890ABCDEF"));
    m_keyEdit->setMaxLength(16);
    m_keyEdit->setInputMethodHints(Qt::ImhNoPredictiveText | Qt::ImhPreferUppercase);
    operationForm->addRow(QStringLiteral("8-байтный ключ (16 hex):"), m_keyEdit);

    auto *modeRow = new QWidget(operationBox);
    auto *modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    m_onceRadio = new QRadioButton(QStringLiteral("Однократный запуск"), modeRow);
    m_periodicRadio = new QRadioButton(QStringLiteral("Периодический опрос"), modeRow);
    m_onceRadio->setChecked(true);
    modeLayout->addWidget(m_onceRadio);
    modeLayout->addWidget(m_periodicRadio);
    modeLayout->addStretch();
    operationForm->addRow(QStringLiteral("Режим:"), modeRow);

    auto *intervalRow = new QWidget(operationBox);
    auto *intervalLayout = new QHBoxLayout(intervalRow);
    intervalLayout->setContentsMargins(0, 0, 0, 0);
    m_intervalSpin = new QSpinBox(intervalRow);
    m_intervalSpin->setRange(250, 24 * 60 * 60 * 1000);
    m_intervalSpin->setValue(5000);
    m_intervalSpin->setSingleStep(500);
    m_intervalSpin->setSuffix(QStringLiteral(" мс"));
    intervalLayout->addWidget(m_intervalSpin);
    intervalLayout->addStretch();
    operationForm->addRow(QStringLiteral("Период опроса:"), intervalRow);

    auto *controlRow = new QHBoxLayout();
    m_startButton = new QPushButton(QStringLiteral("Старт"), central);
    m_pauseButton = new QPushButton(QStringLiteral("Пауза"), central);
    m_resumeButton = new QPushButton(QStringLiteral("Продолжить"), central);
    m_stopButton = new QPushButton(QStringLiteral("Стоп"), central);
    m_pauseButton->setEnabled(false);
    m_resumeButton->setEnabled(false);
    m_stopButton->setEnabled(false);
    controlRow->addWidget(m_startButton);
    controlRow->addWidget(m_pauseButton);
    controlRow->addWidget(m_resumeButton);
    controlRow->addWidget(m_stopButton);
    controlRow->addStretch();

    m_currentFileLabel = new QLabel(QStringLiteral("Файл: —"), central);
    m_statsLabel = new QLabel(QStringLiteral("Обнаружено: 0 | Новых: 0"), central);
    m_progressBar = new QProgressBar(central);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    m_progressBar->setFormat(QStringLiteral("%p%"));

    m_log = new QTextEdit(central);
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(180);

    root->addWidget(inputBox);
    root->addWidget(outputBox);
    root->addWidget(operationBox);
    root->addLayout(controlRow);
    root->addWidget(m_currentFileLabel);
    root->addWidget(m_statsLabel);
    root->addWidget(m_progressBar);
    root->addWidget(m_log, 1);

    setCentralWidget(central);

    connect(inputBrowse, &QPushButton::clicked, this, &MainWindow::browseInputDirectory);
    connect(outputBrowse, &QPushButton::clicked, this, &MainWindow::browseOutputDirectory);
    connect(m_periodicRadio, &QRadioButton::toggled, this, &MainWindow::togglePeriodic);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startProcessing);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::pauseProcessing);
    connect(m_resumeButton, &QPushButton::clicked, this, &MainWindow::resumeProcessing);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopProcessing);
}

void MainWindow::browseInputDirectory()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Входной каталог"), m_inputDirEdit->text());
    if (!path.isEmpty()) {
        m_inputDirEdit->setText(QDir::toNativeSeparators(path));
    }
}

void MainWindow::browseOutputDirectory()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Каталог назначения"), m_outputDirEdit->text());
    if (!path.isEmpty()) {
        m_outputDirEdit->setText(QDir::toNativeSeparators(path));
    }
}

void MainWindow::togglePeriodic(bool checked)
{
    m_intervalSpin->setEnabled(checked);
}

void MainWindow::startProcessing()
{
    QString configError;
    if (!readConfiguration(&configError)) {
        QMessageBox::warning(this, QStringLiteral("Проверка настроек"), configError);
        return;
    }

    const QByteArray key = parseXorKey(nullptr);
    const auto policy = static_cast<FileProcessor::ConflictPolicy>(m_conflictCombo->currentData().toInt());

    m_log->clear();
    m_progressBar->setValue(0);
    m_currentFileLabel->setText(QStringLiteral("Файл: подготовка…"));
    m_statsLabel->setText(QStringLiteral("Обнаружено: 0 | Новых: 0"));

    m_processor->configure(m_inputDirEdit->text(),
                           parseFilters(),
                           m_outputDirEdit->text(),
                           m_removeSourceCheck->isChecked(),
                           policy,
                           key,
                           m_periodicRadio->isChecked(),
                           m_intervalSpin->value());

    setRunningUi(true);
    QMetaObject::invokeMethod(m_processor, "start", Qt::QueuedConnection);
}

void MainWindow::pauseProcessing()
{
    m_processor->pause();
}

void MainWindow::resumeProcessing()
{
    m_processor->resume();
}

void MainWindow::stopProcessing()
{
    m_processor->stop();
}

void MainWindow::onProcessorStarted()
{
    appendLog(QStringLiteral("Обработка запущена."));
}

void MainWindow::onProcessorFinished()
{
    if (!m_closing) {
        setRunningUi(false);
        appendLog(QStringLiteral("Обработка завершена."));
    }
}

void MainWindow::onProcessorPausedChanged(bool paused)
{
    m_pauseButton->setEnabled(!paused && m_stopButton->isEnabled());
    m_resumeButton->setEnabled(paused && m_stopButton->isEnabled());
}

void MainWindow::onStatus(const QString &text)
{
    appendLog(text);
}

void MainWindow::onError(const QString &text)
{
    appendLog(QStringLiteral("ОШИБКА: %1").arg(text), true);
}

void MainWindow::onFileStarted(const QString &inputPath, const QString &outputPath, qint64 totalBytes)
{
    m_currentFileLabel->setText(QStringLiteral("Файл: %1 → %2 (%3 байт)")
                                    .arg(QFileInfo(inputPath).fileName(), QFileInfo(outputPath).fileName())
                                    .arg(totalBytes));
    m_progressBar->setValue(0);
}

void MainWindow::onFileProgress(qint64 processedBytes, qint64 totalBytes)
{
    if (totalBytes <= 0) {
        m_progressBar->setRange(0, 1);
        m_progressBar->setValue(1);
        return;
    }
    m_progressBar->setRange(0, 1000);
    const int value = static_cast<int>(std::min<qint64>(1000, (processedBytes * 1000) / totalBytes));
    m_progressBar->setValue(value);
}

void MainWindow::onFileFinished(const QString &inputPath, const QString &outputPath, qint64 bytesProcessed)
{
    appendLog(QStringLiteral("Обработан: %1 → %2, %3 байт")
                  .arg(QFileInfo(inputPath).fileName(), QFileInfo(outputPath).fileName())
                  .arg(bytesProcessed));
}

void MainWindow::onScanStats(int discovered, int eligible)
{
    m_statsLabel->setText(QStringLiteral("Обнаружено: %1 | Новых к обработке: %2").arg(discovered).arg(eligible));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_stopButton->isEnabled()) {
        event->accept();
        return;
    }

    m_closing = true;
    appendLog(QStringLiteral("Остановка перед закрытием приложения…"));
    m_processor->stop();
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
    event->accept();
}

void MainWindow::setRunningUi(bool running)
{
    setControlsEnabled(!running);
    m_startButton->setEnabled(!running);
    m_pauseButton->setEnabled(running);
    m_resumeButton->setEnabled(false);
    m_stopButton->setEnabled(running);
}

void MainWindow::setControlsEnabled(bool enabled)
{
    m_inputDirEdit->setEnabled(enabled);
    m_filtersEdit->setEnabled(enabled);
    m_removeSourceCheck->setEnabled(enabled);
    m_outputDirEdit->setEnabled(enabled);
    m_conflictCombo->setEnabled(enabled);
    m_keyEdit->setEnabled(enabled);
    m_onceRadio->setEnabled(enabled);
    m_periodicRadio->setEnabled(enabled);
    m_intervalSpin->setEnabled(enabled && m_periodicRadio->isChecked());
}

void MainWindow::appendLog(const QString &text, bool isError)
{
    Q_UNUSED(isError);
    m_log->append(QStringLiteral("[%1] %2")
                      .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
}

bool MainWindow::readConfiguration(QString *errorText)
{
    const QString input = QDir::cleanPath(QDir::fromNativeSeparators(m_inputDirEdit->text().trimmed()));
    const QString output = QDir::cleanPath(QDir::fromNativeSeparators(m_outputDirEdit->text().trimmed()));
    const QStringList filters = parseFilters();

    if (input.isEmpty() || !QDir(input).exists()) {
        if (errorText) *errorText = QStringLiteral("Укажите существующий входной каталог.");
        return false;
    }
    if (output.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Укажите каталог назначения.");
        return false;
    }
    if (filters.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Укажите хотя бы одну маску файлов.");
        return false;
    }

    bool keyOk = false;
    parseXorKey(&keyOk);
    if (!keyOk) {
        if (errorText) *errorText = QStringLiteral("Ключ должен содержать ровно 16 шестнадцатеричных символов, например 1234567890ABCDEF.");
        return false;
    }

    if (m_removeSourceCheck->isChecked() &&
        QDir(input).absolutePath().compare(QDir(output).absolutePath(),
#ifdef Q_OS_WIN
                                            Qt::CaseInsensitive
#else
                                            Qt::CaseSensitive
#endif
                                            ) == 0 &&
        m_conflictCombo->currentData().toInt() == static_cast<int>(FileProcessor::ConflictPolicy::Overwrite)) {
        // A same-directory setup is not inherently invalid; the processor additionally protects same-file processing.
        // Do not reject it merely because directories are equal.
    }
    return true;
}

QByteArray MainWindow::parseXorKey(bool *ok) const
{
    const QString hex = m_keyEdit->text().trimmed();
    const bool valid = hex.size() == 16 && QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{16}$")).match(hex).hasMatch();
    if (ok) {
        *ok = valid;
    }
    if (!valid) {
        return {};
    }

    QByteArray key;
    key.reserve(8);
    for (int i = 0; i < 16; i += 2) {
        bool byteOk = false;
        const int value = hex.mid(i, 2).toInt(&byteOk, 16);
        if (!byteOk) {
            if (ok) *ok = false;
            return {};
        }
        key.append(static_cast<char>(value));
    }
    return key;
}

QStringList MainWindow::parseFilters() const
{
    QStringList raw = m_filtersEdit->text().split(QRegularExpression(QStringLiteral("[;\\s]+")), Qt::SkipEmptyParts);
    QStringList filters;
    for (QString filter : raw) {
        filter = filter.trimmed();
        if (!filter.isEmpty()) {
            filters << filter;
        }
    }
    return filters;
}
