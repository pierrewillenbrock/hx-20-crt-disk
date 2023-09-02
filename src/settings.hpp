
#pragma once

#include <QSettings>
#include <memory>
#include <vector>

namespace Settings {

class Configuration {
    //this is the container for a configuration, that is the
    //settings of all the virtual devices.
    //this does not contain the presets themselves, but the name
    //and the settings
    //this also stores the dock layout
    QString name;
    //this is being passed to the devices for them to store their settings in
};

class Settings {
    std::map<QString, Configuration> configurations;
    //this is being passed to the devices for them to store their presets in
};

//The idea of the QSettings is just to be an adapter around some kind of ini
//file, and for it to be mostly "read/written" in a linear fashion. i want a
//settings systems where i can pull out/store stuff as needed.
//
//when we ignore begin/endGroup/Array, this is no issue, but we need to
//pass the group/array path around.
//
// groups are a prefix to the keys: group0/group1/group2/key
// arrays work like this: group0/array1/<index>/key,
// group0/array1/<index>/group2/key, with group0/array1/size

class SettingsKey;
class Group;

class Container : public QObject {
    Q_OBJECT
private:
    QSettings &base;
    std::unordered_map<QString, std::unique_ptr<SettingsKey> > keys;
    std::unordered_map<QString, std::unique_ptr<Group> > groups;

    friend class SettingsKey;
    friend class Group;
public:
    Container(QSettings &base);
    ~Container();
};

class SettingsKey : public QObject {
    Q_OBJECT
private:
    Container &container;
    QString const key;
    SettingsKey(Container &container, QString const &key);
    void emitChanged(QVariant const &value);
    void emitRemoved();
    friend class Group;
public:
    SettingsKey(Container &container);
    ~SettingsKey();
    QVariant value(const QVariant &defaultValue = QVariant()) const;
    QVariant value(const QVariant &defaultValue, bool setDefault);
    void setValue(const QVariant &value);
    void remove();
signals:
    void changed(QVariant const &value);
    void removed();
};

class Group : public QObject {
    Q_OBJECT
private:
    Container &container;
    QString const prefix;
    Group(Container &container, QString const &prefix);
    void emitChangedKey(const QString &key, QVariant const &value, bool directChild);
    void emitRemovedKey(const QString &key, bool directChild);
    friend class SettingsKey;
public:
    Group(Container &container);
    ~Group();
    Group *group(QString const &prefix);
    Group const *group(QString const &prefix) const;
    int arraySize(QString const &prefix);
    Group *array(QString const &prefix, int index);
    Group const *array(QString const &prefix, int index) const;
    void setArraySize(QString const &prefix, int size);
    SettingsKey *key(QString const &key);
    QStringList childGroups() const;
    QStringList childKeys() const;
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    QVariant value(const QString &key, const QVariant &defaultValue, bool setDefault);
    void setValue(const QString &key, const QVariant &value);
    bool contains(const QString &key) const;
    void remove(const QString &key);
    void remove();
signals:
    void changedKey(const QString &key, QVariant const &value);
    void removedKey(const QString &key);
    void changedChildKey(const QString &key, QVariant const &value);
    void removedChildKey(const QString &key);
};

}
