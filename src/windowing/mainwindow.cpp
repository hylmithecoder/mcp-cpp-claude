#include "windowing/mainwindow.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

namespace Windowing {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), serverProcess(nullptr), ngrokProcess(nullptr) {

  // Window settings
  setWindowTitle("MCP Server Configurator");
  resize(950, 520);

  // Resolve configurations
  resolveEnvPath();
  loadSettings();

  // Build and style UI
  setupUi();
  applyStyleSheet();

  // Populate form fields
  clientIdEdit->setText(QString::fromStdString(envMap["MCP_CLIENT_ID"]));
  clientSecretEdit->setText(
      QString::fromStdString(envMap["MCP_CLIENT_SECRET"]));
  ftpHostEdit->setText(QString::fromStdString(envMap["FTPHOSTNAME"]));
  ftpUserEdit->setText(QString::fromStdString(envMap["FTPUSERNAME"]));
  ftpPassEdit->setText(QString::fromStdString(envMap["FTPPASSWORD"]));
  ngrokAuthEdit->setText(QString::fromStdString(envMap["NGROK_AUTHTOKEN"]));

  // Setup monitoring timer
  processTimer = new QTimer(this);
  connect(processTimer, &QTimer::timeout, this,
          &MainWindow::checkProcessStatus);
  processTimer->start(1000);

  // Show initial status
  checkProcessStatus();

  // Start terminal welcome
  logConsole->appendPlainText(
      "--- MCP Configuration Utility Ready ---\nClick 'Start MCP Server' to "
      "launch the background daemon.\n");
}

MainWindow::~MainWindow() {
  if (serverProcess) {
    serverProcess->terminate();
    serverProcess->waitForFinished(2000);
  }
  if (ngrokProcess) {
    ngrokProcess->terminate();
    ngrokProcess->waitForFinished(2000);
  }
}

void MainWindow::resolveEnvPath() {
  // Check if a local .env exists in current working directory
  if (QFile::exists(".env")) {
    envPath = ".env";
  } else {
    // Fallback to home directory .env
    QString home = QDir::homePath();
    envPath = QDir::cleanPath(home + "/.env").toStdString();
  }
}

void MainWindow::loadSettings() {
  envMap.clear();
  // Default configs
  envMap["MCP_CLIENT_ID"] = "REDACTED";
  envMap["MCP_CLIENT_SECRET"] = "REDACTED";
  envMap["FTPHOSTNAME"] = "REDACTED";
  envMap["FTPUSERNAME"] = "REDACTED";
  envMap["FTPPASSWORD"] = "REDACTED";
  envMap["NGROK_AUTHTOKEN"] = "";

  QFile file(".env");
  if (!file.exists()) {
    file.setFileName(QString::fromStdString(envPath));
  }

  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&file);
    while (!in.atEnd()) {
      QString line = in.readLine().trimmed();
      if (line.isEmpty() || line.startsWith('#'))
        continue;
      int eq = line.indexOf('=');
      if (eq != -1) {
        QString key = line.left(eq).trimmed();
        QString val = line.mid(eq + 1).trimmed();

        if (val.startsWith('"') && val.endsWith('"')) {
          val = val.mid(1, val.length() - 2);
        } else if (val.startsWith('\'') && val.endsWith('\'')) {
          val = val.mid(1, val.length() - 2);
        }
        envMap[key.toStdString()] = val.toStdString();
      }
    }
    file.close();
  }
}

void MainWindow::saveSettings() {
  envMap["MCP_CLIENT_ID"] = clientIdEdit->text().toStdString();
  envMap["MCP_CLIENT_SECRET"] = clientSecretEdit->text().toStdString();
  envMap["FTPHOSTNAME"] = ftpHostEdit->text().toStdString();
  envMap["FTPUSERNAME"] = ftpUserEdit->text().toStdString();
  envMap["FTPPASSWORD"] = ftpPassEdit->text().toStdString();
  envMap["NGROK_AUTHTOKEN"] = ngrokAuthEdit->text().toStdString();

  QString path = QString::fromStdString(envPath);
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&file);
    for (const auto &[key, val] : envMap) {
      out << QString::fromStdString(key) << "=" << QString::fromStdString(val)
          << "\n";
    }
    file.close();

    // Ensure local .env is also synced if we are running locally
    if (path != ".env") {
      QFile localFile(".env");
      if (localFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream localOut(&localFile);
        for (const auto &[key, val] : envMap) {
          localOut << QString::fromStdString(key) << "="
                   << QString::fromStdString(val) << "\n";
        }
        localFile.close();
      }
    }

    QMessageBox::information(this, "Success",
                             "Configuration saved successfully!");
    logConsole->appendPlainText("[INFO] Configuration file updated.");
  } else {
    QMessageBox::critical(this, "Error",
                          "Failed to write settings file to Home directory.");
    logConsole->appendPlainText(
        "[ERROR] Failed to save configuration settings.");
  }
}

void MainWindow::toggleServer() {
  if (serverProcess && serverProcess->state() != QProcess::NotRunning) {
    logConsole->appendPlainText("[INFO] Stopping MCP Server...");
    serverProcess->terminate();
    if (!serverProcess->waitForFinished(3000)) {
      serverProcess->kill();
    }
    return;
  }

  // Check for mcp.exe
  QString mcpPath = "mcp.exe";
  if (!QFile::exists(mcpPath)) {
    QString appDirPath = QCoreApplication::applicationDirPath();
    mcpPath = QDir::cleanPath(appDirPath + "/mcp.exe");
    if (!QFile::exists(mcpPath)) {
      // Check current directory fallback
      mcpPath = "./mcp.exe";
      if (!QFile::exists(mcpPath)) {
        QMessageBox::critical(this, "Error",
                              "mcp.exe not found.\n\nMake sure mcp.exe is in "
                              "the same directory as this configurator.");
        logConsole->appendPlainText("[ERROR] mcp.exe executable missing!");
        return;
      }
    }
  }

  logConsole->appendPlainText("[INFO] Launching mcp.exe background daemon...");

  if (!serverProcess) {
    serverProcess = new QProcess(this);
    connect(serverProcess, &QProcess::readyReadStandardOutput, this,
            &MainWindow::readServerOutput);
    connect(serverProcess, &QProcess::readyReadStandardError, this,
            &MainWindow::readServerError);
    // Connect state change
    connect(serverProcess, &QProcess::stateChanged, this,
            &MainWindow::checkProcessStatus);
  }

  // Suppress console window on Windows
  serverProcess->start(mcpPath);
  if (!serverProcess->waitForStarted(2000)) {
    QMessageBox::critical(this, "Error", "Failed to start mcp.exe process.");
    logConsole->appendPlainText("[ERROR] Failed to start background daemon.");
  } else {
    logConsole->appendPlainText(
        "[INFO] Background daemon started successfully.");
  }
}

void MainWindow::checkProcessStatus() {
  if (serverProcess && serverProcess->state() != QProcess::NotRunning) {
    statusLabel->setText("Server Status: Running");
    statusLabel->setStyleSheet(
        "color: #a6e3a1; font-weight: bold; font-size: 14px;");
    launchButton->setText("Stop MCP Server");
    launchButton->setStyleSheet("QPushButton {"
                                "  background-color: #f38ba8;"
                                "  color: #11111b;"
                                "  border: none;"
                                "  border-radius: 6px;"
                                "  font-weight: bold;"
                                "  font-size: 13px;"
                                "  padding: 10px 16px;"
                                "}"
                                "QPushButton:hover {"
                                "  background-color: #e07a97;"
                                "}"
                                "QPushButton:pressed {"
                                "  background-color: #c95f7c;"
                                "}");
  } else {
    statusLabel->setText("Server Status: Stopped");
    statusLabel->setStyleSheet(
        "color: #f38ba8; font-weight: bold; font-size: 14px;");
    launchButton->setText("Start MCP Server");
    launchButton->setStyleSheet("QPushButton {"
                                "  background-color: #89b4fa;"
                                "  color: #11111b;"
                                "  border: none;"
                                "  border-radius: 6px;"
                                "  font-weight: bold;"
                                "  font-size: 13px;"
                                "  padding: 10px 16px;"
                                "}"
                                "QPushButton:hover {"
                                "  background-color: #7ab0e6;"
                                "}"
                                "QPushButton:pressed {"
                                "  background-color: #5f97d9;"
                                "}");
  }
}

void MainWindow::readServerOutput() {
  if (serverProcess) {
    QByteArray data = serverProcess->readAllStandardOutput();
    QString text = QString::fromUtf8(data);
    logConsole->appendPlainText(text.trimmed());
  }
}

void MainWindow::readServerError() {
  if (serverProcess) {
    QByteArray data = serverProcess->readAllStandardError();
    QString text = QString::fromUtf8(data);
    logConsole->appendPlainText("[STDERR] " + text.trimmed());
  }
}

void MainWindow::clearLog() {
  logConsole->clear();
  logConsole->appendPlainText("--- Console Log Cleared ---");
}

void MainWindow::setupUi() {
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(20);

  // LEFT PANEL - Config Form
  QWidget *leftPanel = new QWidget(this);
  leftPanel->setObjectName("leftPanel");
  QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(10, 10, 10, 10);
  leftLayout->setSpacing(15);

  QLabel *configTitle = new QLabel("Configuration Settings", this);
  configTitle->setStyleSheet("color: #cba6f7; font-size: 20px; font-weight: "
                             "bold; margin-bottom: 10px;");
  leftLayout->addWidget(configTitle);

  QFormLayout *formLayout = new QFormLayout();
  formLayout->setSpacing(12);
  formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

  clientIdEdit = new QLineEdit(this);
  clientSecretEdit = new QLineEdit(this);
  clientSecretEdit->setEchoMode(QLineEdit::Password);

  ftpHostEdit = new QLineEdit(this);
  ftpUserEdit = new QLineEdit(this);
  ftpPassEdit = new QLineEdit(this);
  ftpPassEdit->setEchoMode(QLineEdit::Password);

  ngrokAuthEdit = new QLineEdit(this);
  ngrokAuthEdit->setEchoMode(QLineEdit::Password);

  auto addFormRow = [&](const QString &labelText, QWidget *field) {
    QLabel *label = new QLabel(labelText, this);
    label->setStyleSheet("color: #a6adc8; font-size: 13px; font-weight: bold; "
                         "min-width: 110px;");
    formLayout->addRow(label, field);
  };

  addFormRow("Client ID:", clientIdEdit);
  addFormRow("Client Secret:", clientSecretEdit);
  addFormRow("FTP Hostname:", ftpHostEdit);
  addFormRow("FTP Username:", ftpUserEdit);
  addFormRow("FTP Password:", ftpPassEdit);
  addFormRow("Ngrok Authtoken:", ngrokAuthEdit);

  leftLayout->addLayout(formLayout);

  QFrame *divider = new QFrame(this);
  divider->setFrameShape(QFrame::HLine);
  divider->setFrameShadow(QFrame::Sunken);
  divider->setStyleSheet(
      "background-color: #313244; max-height: 1px; margin: 10px 0;");
  leftLayout->addWidget(divider);

  statusLabel = new QLabel("Server Status: Unknown", this);
  statusLabel->setAlignment(Qt::AlignCenter);
  leftLayout->addWidget(statusLabel);

  ngrokStatusLabel = new QLabel("Ngrok Status: Stopped", this);
  ngrokStatusLabel->setAlignment(Qt::AlignCenter);
  ngrokStatusLabel->setStyleSheet("color: #f38ba8; font-weight: bold;");
  leftLayout->addWidget(ngrokStatusLabel);

  QHBoxLayout *btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(12);

  saveButton = new QPushButton("Save Settings", this);
  saveButton->setStyleSheet("QPushButton {"
                            "  background-color: #a6e3a1;"
                            "  color: #11111b;"
                            "  border: none;"
                            "  border-radius: 6px;"
                            "  font-weight: bold;"
                            "  font-size: 13px;"
                            "  padding: 10px 16px;"
                            "}"
                            "QPushButton:hover {"
                            "  background-color: #b5e8b0;"
                            "}"
                            "QPushButton:pressed {"
                            "  background-color: #93d28e;"
                            "}");

  launchButton = new QPushButton("Start MCP Server", this);

  ngrokButton = new QPushButton("Start Ngrok Tunnel", this);
  ngrokButton->setStyleSheet("QPushButton {"
                             "  background-color: #fab387;"
                             "  color: #11111b;"
                             "  border: none;"
                             "  border-radius: 6px;"
                             "  font-weight: bold;"
                             "  font-size: 13px;"
                             "  padding: 10px 16px;"
                             "}"
                             "QPushButton:hover {"
                             "  background-color: #e8a278;"
                             "}"
                             "QPushButton:pressed {"
                             "  background-color: #d18f66;"
                             "}");

  btnLayout->addWidget(saveButton);
  btnLayout->addWidget(launchButton);
  btnLayout->addWidget(ngrokButton);
  leftLayout->addLayout(btnLayout);
  leftLayout->addStretch();

  // RIGHT PANEL - Terminal Console
  QWidget *rightPanel = new QWidget(this);
  QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(10, 10, 10, 10);
  rightLayout->setSpacing(10);

  QHBoxLayout *consoleHeader = new QHBoxLayout();
  QLabel *consoleTitle = new QLabel("MCP Server Console Log", this);
  consoleTitle->setStyleSheet(
      "color: #89b4fa; font-size: 18px; font-weight: bold;");
  consoleHeader->addWidget(consoleTitle);

  clearLogButton = new QPushButton("Clear Log", this);
  clearLogButton->setStyleSheet("QPushButton {"
                                "  background-color: #313244;"
                                "  color: #cdd6f4;"
                                "  border: 1px solid #45475a;"
                                "  border-radius: 5px;"
                                "  font-size: 11px;"
                                "  padding: 4px 10px;"
                                "}"
                                "QPushButton:hover {"
                                "  background-color: #45475a;"
                                "}"
                                "QPushButton:pressed {"
                                "  background-color: #585b70;"
                                "}");
  consoleHeader->addWidget(clearLogButton);
  consoleHeader->addStretch();

  rightLayout->addLayout(consoleHeader);

  logConsole = new QPlainTextEdit(this);
  logConsole->setReadOnly(true);
  rightLayout->addWidget(logConsole);

  // Connect slots
  connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveSettings);
  connect(launchButton, &QPushButton::clicked, this, &MainWindow::toggleServer);
  connect(ngrokButton, &QPushButton::clicked, this, &MainWindow::toggleNgrok);
  connect(clearLogButton, &QPushButton::clicked, this, &MainWindow::clearLog);

  // Assemble main layout (Left: 40%, Right: 60%)
  mainLayout->addWidget(leftPanel, 4);
  mainLayout->addWidget(rightPanel, 6);
}

void MainWindow::applyStyleSheet() {
  setStyleSheet(
      "QMainWindow {"
      "  background-color: #1e1e2e;"
      "}"
      "QWidget#leftPanel {"
      "  background-color: #1e1e2e;"
      "}"
      "QLineEdit {"
      "  background-color: #181825;"
      "  color: #cdd6f4;"
      "  border: 1px solid #313244;"
      "  border-radius: 6px;"
      "  padding: 8px 12px;"
      "  font-size: 13px;"
      "}"
      "QLineEdit:focus {"
      "  border: 1px solid #cba6f7;"
      "}"
      "QPlainTextEdit {"
      "  background-color: #11111b;"
      "  color: #a6e3a1;"
      "  border: 1px solid #313244;"
      "  border-radius: 8px;"
      "  padding: 12px;"
      "  font-family: 'Consolas', 'Courier New', 'Courier', monospace;"
      "  font-size: 12px;"
      "  line-height: 140%;"
      "}");
}

QString MainWindow::getNgrokPath() {
  QString binDir = QDir::homePath() + "/.mcp/bin";
#ifdef _WIN32
  return QDir::cleanPath(binDir + "/ngrok.exe");
#else
  return QDir::cleanPath(binDir + "/ngrok");
#endif
}

void MainWindow::downloadNgrok() {
  logConsole->appendPlainText("[INFO] Preparing to download ngrok...");
  QDir().mkpath(QDir::homePath() + "/.mcp/bin");

#ifdef _WIN32
  QString ngrokUrl =
      "https://bin.equinox.io/c/bNyj1mQVY4c/ngrok-v3-stable-windows-amd64.zip";
  QString zipPath = QDir::homePath() + "/.mcp/bin/ngrok.zip";
#else
  QString ngrokUrl =
      "https://bin.equinox.io/c/bNyj1mQVY4c/ngrok-v3-stable-linux-amd64.tgz";
  QString zipPath = QDir::homePath() + "/.mcp/bin/ngrok.tgz";
#endif

  ngrokStatusLabel->setText("Ngrok Status: Downloading...");
  ngrokStatusLabel->setStyleSheet("color: #f9e2af; font-weight: bold;");

  QProcess *dlProcess = new QProcess(this);
  connect(
      dlProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      this, [this, dlProcess, zipPath]() {
        dlProcess->deleteLater();
        if (dlProcess->exitCode() == 0) {
          logConsole->appendPlainText(
              "[INFO] Download completed. Extracting...");
          extractNgrok(zipPath);
        } else {
          logConsole->appendPlainText(
              "[ERROR] Failed to download ngrok via curl.");
          ngrokStatusLabel->setText("Ngrok Status: Download Failed");
          ngrokStatusLabel->setStyleSheet("color: #f38ba8; font-weight: bold;");
        }
      });

  dlProcess->start("curl", QStringList() << "-Lo" << zipPath << ngrokUrl);
}

void MainWindow::extractNgrok(QString zipPath) {
  QString binDir = QDir::homePath() + "/.mcp/bin";
  QProcess *extProcess = new QProcess(this);
  connect(
      extProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      this, [this, extProcess, zipPath]() {
        extProcess->deleteLater();
        QFile::remove(zipPath); // delete zip

        if (extProcess->exitCode() == 0) {
          logConsole->appendPlainText("[INFO] ngrok extracted successfully!");
          ngrokStatusLabel->setText("Ngrok Status: Ready");
          ngrokStatusLabel->setStyleSheet("color: #a6e3a1; font-weight: bold;");
          // Now start it!
          startNgrokTunnel();
        } else {
          logConsole->appendPlainText("[ERROR] Extraction failed via tar.");
          ngrokStatusLabel->setText("Ngrok Status: Extraction Failed");
          ngrokStatusLabel->setStyleSheet("color: #f38ba8; font-weight: bold;");
        }
      });

  extProcess->start("tar", QStringList() << "-xf" << zipPath << "-C" << binDir);
}

void MainWindow::startNgrokTunnel() {
  QString authtoken = QString::fromStdString(envMap["NGROK_AUTHTOKEN"]);
  if (authtoken.trimmed().isEmpty()) {
    QMessageBox::warning(this, "Ngrok Authtoken Required",
                         "Please enter your ngrok Authtoken in the form first "
                         "and save settings.");
    return;
  }

  QString path = QStandardPaths::findExecutable("ngrok");
  if (path.isEmpty()) {
    path = getNgrokPath();
  }

  // Run authtoken configuration
  QProcess *authProcess = new QProcess(this);
  connect(authProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, authProcess, path, authtoken]() {
            authProcess->deleteLater();
            logConsole->appendPlainText("[INFO] ngrok authtoken configured.");
            actualStartNgrok(path);
          });

  authProcess->start(path, QStringList()
                               << "config" << "add-authtoken" << authtoken);
}

void MainWindow::actualStartNgrok(QString path) {
  logConsole->appendPlainText("[INFO] Starting ngrok http 5500...");

  if (!ngrokProcess) {
    ngrokProcess = new QProcess(this);
    connect(ngrokProcess, &QProcess::readyReadStandardOutput, this, [this]() {
      QByteArray data = ngrokProcess->readAllStandardOutput();
      logConsole->appendPlainText("[ngrok] " +
                                  QString::fromUtf8(data).trimmed());
    });
    connect(ngrokProcess, &QProcess::readyReadStandardError, this, [this]() {
      QByteArray data = ngrokProcess->readAllStandardError();
      logConsole->appendPlainText("[ngrok STDERR] " +
                                  QString::fromUtf8(data).trimmed());
    });
    connect(ngrokProcess, &QProcess::stateChanged, this,
            &MainWindow::updateNgrokUI);
  }

  ngrokProcess->start(path, QStringList() << "http" << "5500");
  if (!ngrokProcess->waitForStarted(2000)) {
    logConsole->appendPlainText(
        "[ERROR] Failed to start ngrok tunnel process.");
  } else {
    logConsole->appendPlainText("[INFO] ngrok tunnel process started.");
    // Start querying the API for the public URL
    QTimer::singleShot(2000, this, &MainWindow::fetchNgrokUrl);
  }
}

void MainWindow::fetchNgrokUrl() {
  if (!ngrokProcess || ngrokProcess->state() == QProcess::NotRunning) {
    return;
  }

  QProcess *curlProc = new QProcess(this);
  connect(curlProc,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, curlProc]() {
            curlProc->deleteLater();
            if (curlProc->exitCode() == 0) {
              QString response =
                  QString::fromUtf8(curlProc->readAllStandardOutput());
              int pos = response.indexOf("\"public_url\":\"");
              if (pos != -1) {
                pos += 14;
                int endPos = response.indexOf("\"", pos);
                if (endPos != -1) {
                  QString publicUrl = response.mid(pos, endPos - pos);
                  ngrokStatusLabel->setText("Ngrok Status: Running\nURL: " +
                                            publicUrl);
                  ngrokStatusLabel->setStyleSheet(
                      "color: #a6e3a1; font-weight: bold;");
                  logConsole->appendPlainText("[INFO] ngrok tunnel active: " +
                                              publicUrl);
                  return;
                }
              }
            }

            // Retry if still running
            if (ngrokProcess && ngrokProcess->state() != QProcess::NotRunning) {
              QTimer::singleShot(2000, this, &MainWindow::fetchNgrokUrl);
            }
          });

  curlProc->start("curl", QStringList() << "-s"
                                        << "http://127.0.0.1:4040/api/tunnels");
}

void MainWindow::updateNgrokUI() {
  if (ngrokProcess && ngrokProcess->state() != QProcess::NotRunning) {
    ngrokButton->setText("Stop Ngrok Tunnel");
    ngrokButton->setStyleSheet("QPushButton {"
                               "  background-color: #f38ba8;"
                               "  color: #11111b;"
                               "  border: none;"
                               "  border-radius: 6px;"
                               "  font-weight: bold;"
                               "  font-size: 13px;"
                               "  padding: 10px 16px;"
                               "}"
                               "QPushButton:hover {"
                               "  background-color: #e07a97;"
                               "}"
                               "QPushButton:pressed {"
                               "  background-color: #c95f7c;"
                               "}");
  } else {
    ngrokStatusLabel->setText("Ngrok Status: Stopped");
    ngrokStatusLabel->setStyleSheet("color: #f38ba8; font-weight: bold;");
    ngrokButton->setText("Start Ngrok Tunnel");
    ngrokButton->setStyleSheet("QPushButton {"
                               "  background-color: #fab387;"
                               "  color: #11111b;"
                               "  border: none;"
                               "  border-radius: 6px;"
                               "  font-weight: bold;"
                               "  font-size: 13px;"
                               "  padding: 10px 16px;"
                               "}"
                               "QPushButton:hover {"
                               "  background-color: #e8a278;"
                               "}"
                               "QPushButton:pressed {"
                               "  background-color: #d18f66;"
                               "}");
  }
}

void MainWindow::toggleNgrok() {
  if (ngrokProcess && ngrokProcess->state() != QProcess::NotRunning) {
    logConsole->appendPlainText("[INFO] Stopping ngrok tunnel...");
    ngrokProcess->terminate();
    if (!ngrokProcess->waitForFinished(3000)) {
      ngrokProcess->kill();
    }
    return;
  }

  // Check if ngrok exists
  QString path = QStandardPaths::findExecutable("ngrok");
  if (path.isEmpty()) {
    path = getNgrokPath();
    if (!QFile::exists(path)) {
      downloadNgrok();
      return;
    }
  }

  startNgrokTunnel();
}

} // namespace Windowing
