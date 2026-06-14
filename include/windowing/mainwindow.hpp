#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <map>
#include <string>

namespace Windowing {

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

private slots:
  void saveSettings();
  void toggleServer();
  void checkProcessStatus();
  void readServerOutput();
  void readServerError();
  void clearLog();
  void toggleNgrok();
  void updateNgrokUI();
  void fetchNgrokUrl();

private:
  void resolveEnvPath();
  void loadSettings();
  void setupUi();
  void applyStyleSheet();
  void downloadNgrok();
  void extractNgrok(QString zipPath);
  void startNgrokTunnel();
  void actualStartNgrok(QString path);
  QString getNgrokPath();

  // UI elements
  QLineEdit *clientIdEdit;
  QLineEdit *clientSecretEdit;
  QLineEdit *ftpHostEdit;
  QLineEdit *ftpUserEdit;
  QLineEdit *ftpPassEdit;
  QLineEdit *ngrokAuthEdit;

  QLabel *statusLabel;
  QLabel *ngrokStatusLabel;
  QPushButton *saveButton;
  QPushButton *launchButton;
  QPushButton *ngrokButton;

  QPlainTextEdit *logConsole;
  QPushButton *clearLogButton;

  // Process & Settings
  QProcess *serverProcess;
  QProcess *ngrokProcess;
  QTimer *processTimer;

  std::string envPath;
  std::map<std::string, std::string> envMap;
};

} // namespace Windowing

#endif // MAINWINDOW_HPP