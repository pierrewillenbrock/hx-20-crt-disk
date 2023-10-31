
#pragma once

#include <map>
#include <stdint.h>
#include <QObject>
#include <QIcon>
#include <memory>

#include "../../hx20-ser-proto.hpp"
#include "tf20-adapters.hpp"
#include "../../settings.hpp"

QT_BEGIN_NAMESPACE

class QMainWindow;
class QLabel;
class QLineEdit;
class QTimer;
class QDockWidget;
class QMenu;

QT_END_NAMESPACE

class HX20DiskDevice : public QObject, public HX20SerialDevice {
    Q_OBJECT;
private:
    struct FCBInfo {
        void *fcb;
        uint8_t drive_code;
        std::string filename;
        TF20DriveInterface *drive;
    };
    struct DirSearchInfo {
        uint8_t drive_code;
        TF20DriveInterface *drive;
    };
    struct DriveInfo {
        uint8_t drive_code;
        std::unique_ptr<TF20DriveInterface> drive;
        QLabel *status_icon;
        QLineEdit *last_file;
        std::unique_ptr<QTimer> status_timer;
        QDockWidget *dock;
        QString title;
        DriveInfo(uint8_t drive_code, QLabel *status_icon, QLineEdit *last_file, std::unique_ptr<QTimer> &&status_timer);
        ~DriveInfo();
    };
    std::vector<uint8_t> load_buffer;
    DirSearchInfo dirSearch;
    std::map<uint16_t,FCBInfo> fcbs;
    int ddno;
    QIcon activeIcon;
    QIcon inactiveIcon;
    DriveInfo drive_1;
    DriveInfo drive_2;
    Settings::Group *settingsConfig;
    Settings::Group *settingsPresets;

    DriveInfo &drive(uint8_t drive_code);
    DriveInfo const &drive(uint8_t drive_code) const;

    void triggerActivityStatus(int drive_code);
    void setCurrentFilename(int drive_code, std::string const &filename);
    void installNewDrive(int drive_code,
                         std::unique_ptr<TF20DriveInterface> &&new_drive,
                         QString const &title);
protected:
    virtual int getDeviceID() const override;
    virtual int gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                          uint16_t size, uint8_t *buf,
                          HX20SerialConnection *conn) override;
public:
    HX20DiskDevice(int ddno = 0);
    ~HX20DiskDevice();
    void setDiskDirectory(int drive_code, std::string const &dir);
    void setDiskFile(int drive_code, std::string const &file);
    void ejectDisk(int drive_code);
    void addDocksToMainWindow(QMainWindow *window, QMenu *devices_menu);
    void setSettings(Settings::Group *settingsConfig,
                     Settings::Group *settingsPresets);
private slots:
    void updateFromConfig();
};
