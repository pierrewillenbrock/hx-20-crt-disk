
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

#include "mainwindow.hpp"

int main(int argc, char **argv) {
//    Q_INIT_RESOURCE(application);
#ifdef Q_OS_ANDROID
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Pirsoft.de");
    QCoreApplication::setOrganizationDomain("pirsoft.de");
    QCoreApplication::setApplicationName("EPSON HX-20 Serial Options Emulator");
    QCoreApplication::setApplicationVersion(QT_VERSION_STR);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::applicationName());
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("device", "Use <device> for communication.", "device"));
    parser.addOption(QCommandLineOption("disk1", "Use <directory> for the first disk drive.", "directory"));
    parser.addOption(QCommandLineOption("disk2", "Use <directory> for the second disk drive.", "directory"));
    parser.addOption(QCommandLineOption("disk3", "Use <directory> for the third disk drive.", "directory"));
    parser.addOption(QCommandLineOption("disk4", "Use <directory> for the fourth disk drive.", "directory"));
    parser.addOption(QCommandLineOption("config", "Use <config> As configuration set. The other command line options override any option from the configuration set.", "config"));
    parser.process(app);

    setlocale(LC_NUMERIC, "C");

    MainWindow mainWin;

    if(parser.isSet("config")) {
        mainWin.setConfigFromCommandline(parser.value("config"));
    }

    mainWin.loadConfiguration(mainWin.currentConfiguration, true);

    if(parser.isSet("disk1")) {
        mainWin.setDiskFromCommandline(0,1,parser.value("disk1"));
    }
    if(parser.isSet("disk2")) {
        mainWin.setDiskFromCommandline(0,2,parser.value("disk2"));
    }
    if(parser.isSet("disk3")) {
        mainWin.setDiskFromCommandline(1,1,parser.value("disk3"));
    }
    if(parser.isSet("disk4")) {
        mainWin.setDiskFromCommandline(1,2,parser.value("disk4"));
    }

    if(parser.isSet("device")) {
        mainWin.connectCommunication(parser.value("device"));
    } else if (mainWin.settingsConfigurationRoot->contains("comms_device")) {
        try {
            mainWin.connectCommunication(mainWin.settingsConfigurationRoot->value("comms_device").toString());
        } catch(std::exception &e) {
        }
    }

    mainWin.show();
    return app.exec();
}
