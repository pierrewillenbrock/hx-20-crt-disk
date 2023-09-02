
#include "settings.hpp"

using namespace Settings;

Container::Container(QSettings &base) : base(base) {}

Container::~Container() = default;

SettingsKey::SettingsKey(Container &container, QString const &key)
    : container(container), key(key) {}

SettingsKey::SettingsKey(Container &container) : container(container) {}
SettingsKey::~SettingsKey() = default;

void SettingsKey::emitChanged(QVariant const &value) {
    emit changed(value);
}

void SettingsKey::emitRemoved() {
    emit removed();
}

QVariant SettingsKey::value(const QVariant &defaultValue) const {
    return container.base.value(key, defaultValue);
}

QVariant SettingsKey::value(const QVariant &defaultValue, bool setDefault) {
    if(!setDefault || container.base.contains(key))
        return value(defaultValue);
    setValue(defaultValue);
    return defaultValue;
}

void SettingsKey::setValue(const QVariant &value) {
    if(container.base.contains(key) &&
            container.base.value(key) == value)
        return;
    container.base.setValue(key, value);
    emit changed(value);
    QString group = this->key;
    QString key;
    bool firstgroup = true;
    while(!group.isEmpty()) {
        int pos = group.lastIndexOf('/');
        if(!key.isEmpty())
            key = group.mid(pos+1)+"/"+key;
        else
            key = group.mid(pos+1);
        group = group.left(pos);
        auto it = (pos != -1)?container.groups.find(group+"/"):container.groups.find("");
        if(it != container.groups.end()) {
            it->second->emitChangedKey(key, value, firstgroup);
        }
        firstgroup = false;
    }
}

void SettingsKey::remove() {
    if(!container.base.contains(key))
        return;
    container.base.remove(key);
    emit removed();
    QString group = this->key;
    QString key;
    bool firstgroup = true;
    while(!group.isEmpty()) {
        int pos = group.lastIndexOf('/');
        if(!key.isEmpty())
            key = group.mid(pos+1)+"/"+key;
        else
            key = group.mid(pos+1);
        group = group.left(pos);
        auto it = (pos != -1)?container.groups.find(group+"/"):container.groups.find("");
        if(it != container.groups.end()) {
            it->second->emitRemovedKey(key, firstgroup);
        }
        firstgroup = false;
    }
}

Group::Group(Container &container, QString const &prefix)
    : container(container), prefix(prefix) {}

Group::Group(Container &container)
    : container(container), prefix("") {}

Group::~Group() = default;

void Group::emitChangedKey(const QString &key, QVariant const &value, bool directChild) {
    if(directChild)
        emit changedKey(key, value);
    emit changedChildKey(key, value);
}

void Group::emitRemovedKey(const QString &key, bool directChild) {
    if(directChild)
        emit removedKey(key);
    emit removedChildKey(key);
}

Group *Group::group(QString const &prefix) {
    QString groupprefix = this->prefix+prefix+"/";
    auto it = container.groups.find(groupprefix);
    if(it != container.groups.end())
        return it->second.get();
    Group *grp = new Group(container, groupprefix);
    container.groups.insert(std::make_pair(groupprefix,
                                           std::unique_ptr<Group>(grp)));
    return grp;
}

Group const *Group::group(QString const &prefix) const {
    QString groupprefix = this->prefix+prefix+"/";
    auto it = container.groups.find(groupprefix);
    if(it != container.groups.end())
        return it->second.get();
    Group *grp = new Group(container, groupprefix);
    container.groups.insert(std::make_pair(groupprefix,
                                           std::unique_ptr<Group>(grp)));
    return grp;
}

int Group::arraySize(QString const &prefix) {
    return value(prefix+"/size", 0).toInt();
}

Group *Group::array(QString const &prefix, int index) {
    if(value(prefix+"/size", 0).toInt() <= index)
        setValue(prefix+"/size", index+1);

    QString groupprefix = this->prefix+prefix+QString("/%1/").arg(index);
    auto it = container.groups.find(groupprefix);
    if(it != container.groups.end())
        return it->second.get();
    Group *grp = new Group(container, groupprefix);
    container.groups.insert(std::make_pair(groupprefix,
                                           std::unique_ptr<Group>(grp)));
    return grp;
}

Group const *Group::array(QString const &prefix, int index) const {
    if(value(prefix+"/size", 0).toInt() <= index)
        return nullptr;

    QString groupprefix = this->prefix+prefix+QString("/%1/").arg(index);
    auto it = container.groups.find(groupprefix);
    if(it != container.groups.end())
        return it->second.get();
    Group *grp = new Group(container, groupprefix);
    container.groups.insert(std::make_pair(groupprefix,
                                           std::unique_ptr<Group>(grp)));
    return grp;
}

void Group::setArraySize(QString const &prefix, int size) {
    int oldSize = value(prefix+"/size", 0).toInt();
    if(oldSize <= size) {
        setValue(prefix+"/size", size);
        return;
    }
    while(oldSize > size) {
        oldSize--;
        Group *grp = array(prefix, oldSize);
        grp->remove();
    }
    setValue(prefix+"/size", size);
}

void Group::remove() {
    for(auto &gn : childGroups()) {
        Group *grp = group(gn);
        grp->remove();
    }
    for(auto &kn : childKeys()) {
        remove(kn);
    }
}

SettingsKey *Group::key(QString const &key) {
    QString keyname = this->prefix+key;
    auto it = container.keys.find(keyname);
    if(it != container.keys.end())
        return it->second.get();
    SettingsKey *ky = new SettingsKey(container, keyname);
    container.keys.insert(std::make_pair(keyname,
                                         std::unique_ptr<SettingsKey>(ky)));
    return ky;
}

QStringList Group::childGroups() const {
    QStringList r;
    container.base.beginGroup(prefix.chopped(1));
    r = container.base.childGroups();
    container.base.endGroup();
    return r;
}

QStringList Group::childKeys() const {
    QStringList r;
    container.base.beginGroup(prefix.chopped(1));
    r = container.base.childKeys();
    container.base.endGroup();
    return r;
}

QVariant Group::value(const QString &key, const QVariant &defaultValue) const {
    return container.base.value(prefix+key, defaultValue);
}

QVariant Group::value(const QString &key, const QVariant &defaultValue, bool setDefault) {
    if(!setDefault || container.base.contains(prefix+key))
        return container.base.value(prefix+key, defaultValue);
    setValue(key, defaultValue);
    return defaultValue;
}

void Group::setValue(const QString &key, const QVariant &value) {
    container.base.setValue(prefix+key, value);
    auto it = container.keys.find(prefix+key);
    if(it != container.keys.end())
        it->second->emitChanged(value);
    emit changedKey(key, value);
    QString group = prefix+key;
    QString key2;
    while(!group.isEmpty()) {
        int pos = group.lastIndexOf('/');
        if(!key2.isEmpty())
            key2 = group.mid(pos+1)+"/"+key2;
        else
            key2 = group.mid(pos+1);
        if(pos > -1)
            group = group.left(pos);
        else
            group = "";
        auto it = (pos != -1)?container.groups.find(group+"/"):container.groups.find("");
        if(it != container.groups.end()) {
            it->second->emitChangedKey(key2, value, false);
        }
    }
}

bool Group::contains(const QString &key) const {
    return container.base.contains(prefix+key);
}

void Group::remove(const QString &key) {
    container.base.remove(prefix+key);
    auto it = container.keys.find(prefix+key);
    if(it != container.keys.end())
        it->second->emitRemoved();
    emit removedKey(key);
    QString group = prefix+key;
    QString key2;
    while(!group.isEmpty()) {
        int pos = group.lastIndexOf('/');
        if(!key2.isEmpty())
            key2 = group.mid(pos+1)+"/"+key2;
        else
            key2 = group.mid(pos+1);
        if(pos > -1)
            group = group.left(pos);
        else
            group = "";
        auto it = (pos != -1)?container.groups.find(group+"/"):container.groups.find("");
        if(it != container.groups.end()) {
            it->second->emitRemovedKey(key2, false);
        }
    }
}
