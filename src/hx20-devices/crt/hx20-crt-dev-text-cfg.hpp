#pragma once

#include <QDialog>
#include <QAbstractItemModel>
#include <memory>
#include <deque>

namespace Ui {
class HX20CrtDeviceTextCfgUi;
}

namespace Settings {
class Group;
}

class CharsetModel : public QAbstractItemModel {
    Q_OBJECT;
private:
    QStringList char_map;
    std::unordered_map<int, QFont> fontmap;
protected:
    virtual QVariant data(const QModelIndex &index, int role) const override;
    virtual bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    virtual QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    virtual QModelIndex parent(const QModelIndex &child) const override;
    virtual int rowCount(const QModelIndex &parent) const override;
    virtual int columnCount(const QModelIndex &parent) const override;
    virtual Qt::ItemFlags flags(const QModelIndex &index) const override;
public:
    CharsetModel();
    void setCharMap(QStringList const &char_map);
    void setFontMap(std::unordered_map<int, QFont> const &fontmap);
signals:
    void charMapItemChanged(int idx, QString const &value);
};

class HX20CrtDeviceTextCfg : public QDialog {
    Q_OBJECT;
private:
    std::unique_ptr<Ui::HX20CrtDeviceTextCfgUi> ui;
    Settings::Group *settingsConfig;
    Settings::Group *settingsPresets;
    std::unique_ptr<CharsetModel> charsetmodel;
    struct Charset {
        QString name;
        QString builtin;
        QStringList chars;
    };
    std::deque<Charset> charsets;

    void setCharset(int charsetnum);
    void duplicateAndActivateCharSet(int charsetnum);
public:
    HX20CrtDeviceTextCfg(Settings::Group *settingsConfig,
                         Settings::Group *settingsPresets,
                         QWidget *parent = nullptr,
                         Qt::WindowFlags f = Qt::WindowFlags());
    virtual ~HX20CrtDeviceTextCfg() override;
private slots:
    void saveConfig();
};
