#include "mainwindow.hpp"
#include "comms-debug.hpp"
#include "hx20-devices/crt/hx20-crt-dev.hpp"
#include "hx20-devices/disk/hx20-disk-dev.hpp"

#include <fstream>
#include <set>

#include <poll.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>

#include <QFileInfo>
#include <QSocketNotifier>
#include <QMessageBox>
#include <QMenuBar>
#include <QInputDialog>
#include <QActionGroup>

static void findTtysInDev(std::vector<dev_t> const &device_ids,
                          std::string const &base,
                          QStringList &res,
                          std::set<std::pair<dev_t,__ino_t> > &seen_inodes) {

    std::vector<std::string> found_dirs;
    if(base == "/dev/char/")
        return;
    DIR *d = opendir(base.c_str());
    if(!d)
        return;

    while(dirent *ent = readdir(d)) {
        //skip "." and ".."
        if(strcmp(ent->d_name,".") == 0 ||
                strcmp(ent->d_name,"..") == 0)
            continue;
        std::string name = base+ent->d_name;
        struct stat lsb, sb;
        if(lstat(name.c_str(), &lsb) != 0)
            continue;
        if(stat(name.c_str(), &sb) != 0)
            continue;
        if(seen_inodes.find(std::make_pair(lsb.st_dev, lsb.st_ino)) != seen_inodes.end())
            continue;
        seen_inodes.insert(std::make_pair(lsb.st_dev, lsb.st_ino));
        if((sb.st_mode & S_IFMT) == S_IFDIR) {
            found_dirs.push_back(name+"/");
        } else if((sb.st_mode & S_IFMT) == S_IFCHR) {
            bool found = false;
            for(auto &id : device_ids) {
                if(id == sb.st_rdev)
                    found = true;
            }
            if(found)
                res.append(QString::fromStdString(name));
        }
    }
    closedir(d);

    for(auto &fd : found_dirs) {
        findTtysInDev(device_ids, fd, res, seen_inodes);
    }
}

static QStringList findTtys() {
    std::vector<dev_t> device_ids;
    //iterate over /sys/class/tty, filter for devices with a driver
    //we do _not_ want to open any devices as that could be a networked
    //device and take a while to open.
    DIR *d = opendir("/sys/class/tty");

    while(dirent *ent = readdir(d)) {
        //skip "." and ".."
        if(strcmp(ent->d_name,".") == 0 ||
                strcmp(ent->d_name,"..") == 0)
            continue;
        //skip any that do not have a driver
        //we could probably also reduce that to just device?
        if(access((std::string("/sys/class/tty/")+ent->d_name+std::string("/device/driver")).c_str(),
                  F_OK) != 0)
            continue;
        //read the device_id
        std::ifstream f(std::string("/sys/class/tty/")+ent->d_name+std::string("/dev"));
        if(!f.good())
            continue;
        std::string s;
        std::getline(f,s);
        auto pos = s.find(':');
        if(pos == s.npos)
            continue;
        int maj,min;
        maj = atoi(s.substr(0,pos).c_str());
        min = atoi(s.substr(pos+1).c_str());
        device_ids.push_back(makedev(maj,min));
    }

    closedir(d);

    QStringList res;

    std::set<std::pair<dev_t,__ino_t> > seen_inodes;
    findTtysInDev(device_ids, "/dev/", res, seen_inodes);

    res.sort();
    res.removeDuplicates();
    return res;
}

static void cloneSettings(Settings::Group *dst, Settings::Group const *src) {
    for(auto &g : src->childGroups()) {
        cloneSettings(dst->group(g), src->group(g));
    }
    for(auto &k : src->childKeys()) {
        dst->setValue(k, src->value(k));
    }
}

MainWindow::MainWindow(QWidget *parent, Qt::WindowFlags flags) :
    QMainWindow(parent, flags),
    settings(QSettings::Scope::UserScope),
    settingsContainer(settings),
    settingsRoot(new Settings::Group(settingsContainer)),
    settingsConfigurationRoot(nullptr),
    currentConfiguration(-1),
    crt_dev(std::make_unique<HX20CrtDevice>()),
    disk_devs({
        std::make_unique<HX20DiskDevice>(0),
        std::make_unique<HX20DiskDevice>(1)}),
    commsdbg(new CommsDebugWindow()),
    config_action_group(new QActionGroup(this)) {
    commsdbg->setObjectName("commsdebugdock");
    currentConfiguration = settingsRoot->value("CurrentConfiguration",0).toInt();
    if(currentConfiguration >= settingsRoot->arraySize("Configuration"))
        currentConfiguration = 0;
    settingsPresetRoot = settingsRoot->group("Presets");

    auto devices_menu = new QMenu(tr("&Devices"));

    crt_dev->addDocksToMainWindow(this, devices_menu);
    for(auto &dd : disk_devs)
        if(dd)
            dd->addDocksToMainWindow(this, devices_menu);

    auto comms_menu = menuBar()->addMenu(tr("&Communication"));
    auto connect_comms = comms_menu->addAction(tr("&Connect..."));
    auto debug_comms_action = comms_menu->addAction(tr("&Debug"));

    QObject::connect(connect_comms, &QAction::triggered,
    [this]() {
        QInputDialog dlg;
        dlg.setComboBoxEditable(true);
        QStringList devs = findTtys();
        bool ok;
        QString item = dlg.getItem(this, tr("Select Device"),
                                   tr("TTY Device for connection"),
                                   devs, 0, true, &ok);
        if(ok && !item.isEmpty()) {
            try {
                connectCommunication(item);
                settingsConfigurationRoot->setValue("comms_device", item);
            } catch(std::exception &e) {
                QMessageBox::warning(this, "Could not open device", e.what());
            }
        }
    });
    addDockWidget(Qt::BottomDockWidgetArea, commsdbg);
    QObject::connect(debug_comms_action, &QAction::triggered,
    [this]() {
        commsdbg->show();
    });

    auto config_menu = menuBar()->addMenu(tr("&Configurations"));
    auto new_config = config_menu->addAction(tr("&New Configuration"));
    auto delete_config = config_menu->addAction(tr("&Delete Configuration"));
    auto rename_config = config_menu->addAction(tr("&Rename current Configuration..."));
    QObject::connect(new_config, &QAction::triggered,
    [this, config_menu]() {
        saveConfiguration();
        //clone the config groups
        currentConfiguration = settingsRoot->arraySize("Configuration");
        Settings::Group *new_group = settingsRoot->array("Configuration", currentConfiguration);
        cloneSettings(new_group, settingsConfigurationRoot);
        settingsConfigurationRoot = new_group;
        settingsConfigurationRoot->setValue
        ("name",
         settingsConfigurationRoot->value("name").toString()+"(Copy)");
        int i = currentConfiguration;
        auto a = config_menu->addAction(
                 settingsRoot->array("Configuration", i)->value("name").toString());
        config_action_group->addAction(a);
        a->setCheckable(true);
        a->setChecked(true);
        QObject::connect(a, &QAction::triggered,
        [this,i]() {
            saveConfiguration();
            settingsRoot->setValue("CurrentConfiguration",i);
            loadConfiguration(i);
        });
        //must be done after creating the QAction
        loadConfiguration(currentConfiguration);
    });
    QObject::connect(delete_config, &QAction::triggered,
    [this, config_menu]() {
        //order in config_action_group should match up, so we can get our
        //action from there. yay!
        QAction *work_action = config_action_group->actions()[currentConfiguration];
        if(settingsRoot->arraySize("Configuration") <= 1)
            return;
        //so, first go around and clone configs down.
        for(int i = currentConfiguration+1; i < settingsRoot->arraySize("Configuration"); i++) {
            cloneSettings(settingsRoot->array("Configuration", i-1),
                          settingsRoot->array("Configuration", i));
        }
        settingsRoot->setArraySize("Configuration",
                                   settingsRoot->arraySize("Configuration")-1);
        //remove the action from the menu
        config_menu->removeAction(work_action);
        config_action_group->removeAction(work_action);
        if(currentConfiguration >= settingsRoot->arraySize("Configuration"))
            currentConfiguration = settingsRoot->arraySize("Configuration")-1;
        settingsConfigurationRoot = settingsRoot->array("Configuration", currentConfiguration);
        config_action_group->actions()[currentConfiguration]->setChecked(true);
        loadConfiguration(currentConfiguration);
        settingsRoot->setValue("CurrentConfiguration",currentConfiguration);
    });
    QObject::connect(rename_config, &QAction::triggered,
    [this]() {
        //order in config_action_group should match up, so we can get our
        //action from there. yay!
        QAction *work_action = config_action_group->actions()[currentConfiguration];
        bool ok;
        QString newName = QInputDialog::getText(this, "Enter new name",
                                                "New name of the configuration",
                                                QLineEdit::Normal,
                                                settingsConfigurationRoot->value("name").toString(),
                                                &ok);
        if(ok) {
            settingsConfigurationRoot->setValue("name", newName);
            work_action->setText(newName);
        }
    });
    config_menu->addSeparator();
    for(int i = 0; i < settingsRoot->arraySize("Configuration"); i++) {
        auto a = config_menu->addAction(
                 settingsRoot->array("Configuration", i)->value("name").toString());
        config_action_group->addAction(a);
        a->setCheckable(true);
        a->setChecked(i == currentConfiguration);
        QObject::connect(a, &QAction::triggered,
        [this,i]() {
            settingsRoot->setValue("CurrentConfiguration",i);
            saveConfiguration();
            loadConfiguration(i);
        });
    }

    menuBar()->addMenu(devices_menu);
}

MainWindow::~MainWindow() {
    commsdbg->setConnection(nullptr);
}

bool MainWindow::setConfigFromCommandline(QString const &config) {
    bool ok;
    int v = config.toInt(&ok);
    if(ok) {
        currentConfiguration = v;
        return true;
    } else {
        for(int i = 0; i < settingsRoot->arraySize("Configuration"); i++) {
            if(settingsRoot->array("Configuration", i)->value("name", QString()).toString() ==
                    config) {
                currentConfiguration = i;
                return true;
            }
        }
    }
    return false;
}

void MainWindow::setupDrive(std::unique_ptr< HX20DiskDevice > const &dev,
                            int drive_code, QString const &disk) {
    QFileInfo fi(disk);
    if(fi.isDir()) {
        try {
            dev->setDiskDirectory(drive_code, disk.toLocal8Bit().data());
        } catch(std::exception &e) {
            printf("Failed to open %s as a disk directory:\n%s\n",
                   disk.toLocal8Bit().data(),
                   e.what());
        }
    } else if(fi.isFile()) {
        try {
            dev->setDiskFile(drive_code, disk.toLocal8Bit().data());
        } catch(std::exception &e) {
            printf("Failed to open %s as a disk file:\n%s\n",
                   disk.toLocal8Bit().data(),
                   e.what());
        }
    } else {
        printf("%s is neither directory nor file\n",
               disk.toLocal8Bit().data());
    }
}


void MainWindow::setDiskFromCommandline(int device, int drive_code, QString const &config) {
    setupDrive(disk_devs[device], drive_code, config);
}

void MainWindow::loadConfiguration(int configuration, bool noConnect) {
    if(configuration >= settingsRoot->arraySize("Configuration"))
        return;
    currentConfiguration = configuration;
    settingsConfigurationRoot = settingsRoot->array("Configuration", currentConfiguration);
    settingsConfigurationRoot->value("name", "Default", true);

    crt_dev->setSettings(settingsConfigurationRoot->group("crt_dev"),
                         settingsPresetRoot->group("crt_dev"));

    disk_devs[0]->setSettings(settingsConfigurationRoot->group("disk_dev1"),
                              settingsPresetRoot->group("disk_dev"));
    disk_devs[1]->setSettings(settingsConfigurationRoot->group("disk_dev2"),
                              settingsPresetRoot->group("disk_dev"));

    if(!noConnect) {
        try {
            connectCommunication(settingsConfigurationRoot->value("comms_device").toString());
        } catch(std::exception &e) {
            QMessageBox::warning(this, "Could not open device", e.what());
        }
    }

    config_action_group->actions()[currentConfiguration]->setChecked(true);
    restoreGeometry(settingsConfigurationRoot->value("window_geometry").toByteArray());
    restoreState(settingsConfigurationRoot->value("window_state").toByteArray());
}

void MainWindow::saveConfiguration() {
    if(!settingsConfigurationRoot)
        return;
    settingsConfigurationRoot->setValue("window_geometry", saveGeometry());
    settingsConfigurationRoot->setValue("window_state", saveState());
}

void MainWindow::connectCommunication(QString const &device) {
    conn = std::make_unique<HX20SerialConnection>(device.toLocal8Bit().data());

    std::vector<struct pollfd> pfds;
    pfds.resize(conn->getNfds());
    conn->fillPollFd(pfds.data());

    for(auto &pfd : pfds) {
        if(pfd.events & POLLIN) {
            in_notifier.reset(
            new QSocketNotifier(pfd.fd, QSocketNotifier::Type::Read));
            QObject::connect(in_notifier.get(), &QSocketNotifier::activated,
            [this, pfd](QSocketDescriptor, QSocketNotifier::Type) {
                auto p = pfd;
                p.revents = POLLIN;
                if(conn->handleEvents(&p, 1) < 0) {
                    QMessageBox::critical(this, "IO error", "IO on filedescriptor failed");
                    in_notifier.release()->deleteLater();
                }
            });
            in_notifier->setEnabled(true);
        }
        if(pfd.events & POLLOUT) {
            out_notifier.reset(
            new QSocketNotifier(pfd.fd, QSocketNotifier::Type::Write));
            QObject::connect(out_notifier.get(), &QSocketNotifier::activated,
            [this, pfd](QSocketDescriptor, QSocketNotifier::Type) {
                auto p = pfd;
                p.revents = POLLOUT;
                if(conn->handleEvents(&p, 1) < 0) {
                    QMessageBox::critical(this, "IO error", "IO on filedescriptor failed");
                    out_notifier.release()->deleteLater();
                }
            });
            out_notifier->setEnabled(true);
        }
        if(pfd.events & (POLLPRI | POLLERR | POLLHUP | POLLNVAL)) {
            err_notifier.reset(
            new QSocketNotifier(pfd.fd, QSocketNotifier::Type::Exception));
            QObject::connect(err_notifier.get(), &QSocketNotifier::activated,
            [this, pfd](QSocketDescriptor, QSocketNotifier::Type) {
                auto p = pfd;
                p.revents = p.events;
                if(conn->handleEvents(&p, 1) < 0) {
                    QMessageBox::critical(this, "IO error", "IO on filedescriptor failed");
                    err_notifier.release()->deleteLater();
                }
            });
            err_notifier->setEnabled(true);
        }
    }

    conn->registerDevice(crt_dev.get());
    if(disk_devs[0])
        conn->registerDevice(disk_devs[0].get());
    if(disk_devs[1])
        conn->registerDevice(disk_devs[1].get());

    commsdbg->setConnection(conn.get());
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveConfiguration();
}

