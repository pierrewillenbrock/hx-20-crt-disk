#pragma once

#include <QMainWindow>
#include <QSettings>

#include "settings.hpp"

QT_BEGIN_NAMESPACE

class QActionGroup;
class QSocketNotifier;

QT_END_NAMESPACE

class CommsDebugWindow;
class HX20CrtDevice;
class HX20DiskDevice;
class HX20SerialConnection;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    QSettings settings;
    Settings::Container settingsContainer;
    std::unique_ptr<Settings::Group> settingsRoot;
    Settings::Group *settingsConfigurationRoot;
    Settings::Group *settingsPresetRoot;
    int currentConfiguration;
    std::unique_ptr<HX20CrtDevice> crt_dev;
    std::array<std::unique_ptr<HX20DiskDevice>, 2 > disk_devs;
    std::unique_ptr<HX20SerialConnection> conn;
    CommsDebugWindow *commsdbg;
    QActionGroup *config_action_group;
    std::unique_ptr<QSocketNotifier> in_notifier;
    std::unique_ptr<QSocketNotifier> out_notifier;
    std::unique_ptr<QSocketNotifier> err_notifier;

    MainWindow(QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());
    virtual ~MainWindow() override;
    bool setConfigFromCommandline(QString const &config);
    void setDiskFromCommandline(int device, int drive_code, QString const &config);
    void loadConfiguration(int configuration, bool noConnect = false);
    void saveConfiguration();
    void connectCommunication(QString const &device);

    static void setupDrive(std::unique_ptr< HX20DiskDevice > const &dev,
                           int drive_code, QString const &disk);
protected:
    virtual void closeEvent(QCloseEvent *event) override;
};

