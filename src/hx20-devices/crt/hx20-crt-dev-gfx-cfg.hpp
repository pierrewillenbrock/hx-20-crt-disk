#pragma once

#include <QDialog>
#include <QAbstractItemModel>
#include <memory>
#include <deque>

namespace Ui {
class HX20CrtDeviceGfxCfgUi;
}

namespace Settings {
class Group;
};

class ColorsetModel : public QAbstractItemModel {
    Q_OBJECT;
private:
    std::vector<QColor> color_map;
public:
    virtual QVariant data(const QModelIndex &index, int role) const override;
    virtual bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    virtual QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    virtual QModelIndex parent(const QModelIndex &child) const override;
    virtual int rowCount(const QModelIndex &parent) const override;
    virtual int columnCount(const QModelIndex &parent) const override;
    virtual Qt::ItemFlags flags(const QModelIndex &index) const override;
public:
    ColorsetModel();
    void setColorMap(std::vector<QColor> const &color_map);
signals:
    void colorMapItemChanged(int idx, QString const &value);
};

class HX20CrtDeviceGfxCfg : public QDialog {
    Q_OBJECT;
private:
    std::unique_ptr<Ui::HX20CrtDeviceGfxCfgUi> ui;
    Settings::Group *settingsConfig;
    Settings::Group *settingsPresets;
    std::unique_ptr<ColorsetModel> colorsetmodel;
    struct Colorset {
        QString name;
        QString builtin;
        std::vector<QColor> colors;
    };
    std::deque<Colorset> colorsets;

    void setColorset(int colorsetnum);
    void duplicateAndActivateColorSet(int colorsetnum);
    void renumberItems(int first);
public:
    HX20CrtDeviceGfxCfg(Settings::Group *settingsConfig,
                        Settings::Group *settingsPresets,
                        QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    virtual ~HX20CrtDeviceGfxCfg() override;
private slots:
    void saveConfig();
};
