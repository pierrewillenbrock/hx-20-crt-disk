
#include "hx20-crt-dev-gfx-cfg.hpp"
#include "ui_hx20-crt-dev-gfx-cfg.h"
#include "settings.hpp"

#include <QTreeWidgetItem>
#include <QPainter>
#include <QColorDialog>
#include <cmath>

static int iconSizes[] = {
    128, 96, 64, 48, 32, 16,
};

static std::unordered_map<QRgb, QIcon> coloriconcache;
static std::unordered_map<QString, QIcon> chariconcache;

static QPixmap genColoredIconPixmap(QColor const &color, QSize const &size) {
    QPixmap pixmap(size);
    QPainter p(&pixmap);
    p.fillRect(QRect(QPoint(0,0), size), color);
    return pixmap;
}

static QPixmap genColorsetIconPixmap(std::vector<QColor> const &colors, QSize const &size) {
    QPixmap pixmap(size);
    QPainter p(&pixmap);
    if(colors.size() == 0) {
        p.fillRect(QRect(QPoint(0,0), size), Qt::black);
        return pixmap;
    }
    p.fillRect(QRect(QPoint(0,0), size), colors[0]);
    int sq = ceil(sqrt(colors.size()));
    int sz = size.width()/sq;
    if(sz == 0) sz = 1;
    for(int i = 0; i < (int)colors.size(); i++) {
        int r = i / sq;
        int c = i % sq;
        p.fillRect(QRect(QPoint(c*sz,r*sz), QSize(sz,sz)), colors[i]);
    }
    return pixmap;
}

static QIcon genColoredIcon(QColor const &color) {
    auto it = coloriconcache.find(color.rgb());
    if(it != coloriconcache.end())
        return it->second;
    QIcon i;
    for(auto &s : iconSizes) {
        i.addPixmap(genColoredIconPixmap(color, QSize(s,s)));
    }
    coloriconcache[color.rgb()] = i;
    return i;
}

static QIcon genColorsetIcon(std::vector<QColor> const &colors) {
    QIcon i;
    for(auto &s : iconSizes) {
        i.addPixmap(genColorsetIconPixmap(colors, QSize(s,s)));
    }
    return i;
}

ColorsetModel::ColorsetModel() {}

void ColorsetModel::setColorMap(std::vector<QColor> const &color_map) {
    size_t old_size = this->color_map.size();
    if(old_size < color_map.size()) {
        beginInsertRows(QModelIndex(), old_size, color_map.size()-1);
    }
    if(old_size > color_map.size()) {
        beginRemoveRows(QModelIndex(), color_map.size(), old_size-1);
    }
    this->color_map = color_map;
    if(old_size < color_map.size()) {
        endInsertRows();
    }
    if(old_size > color_map.size()) {
        endRemoveRows();
    }
    emit dataChanged(createIndex(0,0),
                     createIndex(qMin(old_size, color_map.size())-1, 0),
    {Qt::ItemDataRole::EditRole, Qt::ItemDataRole::DecorationRole});
}

QVariant ColorsetModel::data(const QModelIndex &index, int role) const {
    if(!index.isValid() || index.row() < 0 || (unsigned)index.row() >= color_map.size())
        return QVariant();
    int ch = index.row();
    if((unsigned)ch >= color_map.size())
        return QVariant();
    switch(role) {
    case Qt::ItemDataRole::DisplayRole:
        return QString("%1").arg(ch);
    case Qt::ItemDataRole::EditRole:
        return color_map[ch];
    case Qt::ItemDataRole::DecorationRole:
        return genColoredIcon(color_map[ch]);
    }
    return QVariant();
}

bool ColorsetModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if(!index.isValid() || index.row() < 0 || (unsigned)index.row() >= color_map.size())
        return false;
    int ch = index.row();
    if(ch < 0 || (unsigned)ch >= color_map.size())
        return false;
    switch(role) {
    case Qt::ItemDataRole::EditRole:
        //this may switch out the current charset, but will not change our map.
        emit colorMapItemChanged(ch, value.toString());
        assert(ch >= 0 && (unsigned)ch < color_map.size());
        color_map[ch] = value.value<QColor>();
        emit dataChanged(index, index,
        {Qt::ItemDataRole::EditRole, Qt::ItemDataRole::DecorationRole});
        return true;
    }
    return false;
}

Qt::ItemFlags ColorsetModel::flags(const QModelIndex &index) const {
    return /*Qt::ItemFlag::ItemIsEditable |*/ // only when we have a custom editor. otherwise, we will just catch the double click/item activate/whatever it is called.
           Qt::ItemFlag::ItemIsEnabled |
           Qt::ItemFlag::ItemIsSelectable |
           Qt::ItemFlag::ItemNeverHasChildren;
}

QModelIndex ColorsetModel::index(int row, int column, const QModelIndex &parent) const {
    if(parent.isValid())
        return QModelIndex();
    return createIndex(row, column);
}

QModelIndex ColorsetModel::parent(const QModelIndex &child) const {
    return QModelIndex();
}

int ColorsetModel::rowCount(const QModelIndex &parent) const {
    if(parent.isValid())
        return 0;
    return color_map.size();
}

int ColorsetModel::columnCount(const QModelIndex &parent) const {
    return 1;
}

void HX20CrtDeviceGfxCfg::setColorset(int colorsetnum) {
    std::vector<QColor> map;
    if(colorsetnum < (int)colorsets.size()) {
        map = colorsets[colorsetnum].colors;
        ui->trwColorPresets->setCurrentItem(ui->trwColorPresets->topLevelItem(colorsetnum));
        ui->trwColorPresets->scrollToItem(ui->trwColorPresets->topLevelItem(colorsetnum));
    }
    ui->tlbColorSetMoveUp->setEnabled(colorsetnum > 0);
    ui->tlbColorSetMoveDown->setEnabled(colorsetnum < (int)colorsets.size()-1);
    ui->tlbColorSetDelete->setDisabled(!colorsets[colorsetnum].builtin.isEmpty());
    colorsetmodel->setColorMap(map);
}

void HX20CrtDeviceGfxCfg::duplicateAndActivateColorSet(int colorsetnum) {
    if(colorsetnum < (int)colorsets.size()) {
        auto const &srcset = colorsets[colorsetnum];
        Colorset dstset = srcset;
        QString name = srcset.name;
        name.replace("(builtin)","");
        name.replace("; builtin)",")");
        dstset.name = name+"(copy)";
        dstset.builtin.clear();

        int dstnum = colorsets.size();
        colorsets.push_back(dstset);

        auto item = new QTreeWidgetItem(QStringList({dstset.name}));
        item->setData(0, Qt::ItemDataRole::UserRole, QVariant(dstnum));
        item->setFlags(item->flags() | Qt::ItemFlag::ItemIsEditable);
        ui->trwColorPresets->addTopLevelItem(item);
        ui->trwColorPresets->setCurrentItem(item);
        ui->cobColorSet1->addItem(dstset.name, QVariant(dstnum));
        ui->cobColorSet2->addItem(dstset.name, QVariant(dstnum));
        ui->cobBorderColorSet->addItem(dstset.name, QVariant(dstnum));
    }
}

static void swapItems(QComboBox *cob, int item1, int item2) {
    int cidx = cob->currentIndex();
    QString name = cob->itemText(item1);
    cob->removeItem(item1);
    cob->insertItem(item2, name, QVariant(item2));
    cob->setItemData(item1, QVariant(item1));
    if(cidx == item1)
        cob->setCurrentIndex(item2);
    else if(cidx == item2)
        cob->setCurrentIndex(item1);
}

void HX20CrtDeviceGfxCfg::renumberItems(int first) {
    for(int i = first; i < ui->trwColorPresets->topLevelItemCount(); i++) {
        ui->trwColorPresets->topLevelItem(i)->setData(0, Qt::ItemDataRole::UserRole, QVariant(i));
        ui->cobColorSet1->setItemData(i, QVariant(i));
        ui->cobColorSet2->setItemData(i, QVariant(i));
        ui->cobBorderColorSet->setItemData(i, QVariant(i));
        QIcon icon = genColorsetIcon(colorsets[i].colors);
        ui->cobColorSet1->setItemIcon(i, icon);
        ui->cobColorSet2->setItemIcon(i, icon);
        ui->cobBorderColorSet->setItemIcon(i, icon);
    }
}

HX20CrtDeviceGfxCfg::HX20CrtDeviceGfxCfg(Settings::Group *settingsConfig,
        Settings::Group *settingsPresets,
        QWidget *parent, Qt::WindowFlags f)
    : QDialog(parent, f), ui(std::make_unique<Ui::HX20CrtDeviceGfxCfgUi>()),
      settingsConfig(settingsConfig), settingsPresets(settingsPresets)
{
    ui->setupUi(this);

    ui->spbSizeX->setValue(settingsConfig->value("sizeX").toInt());
    ui->spbSizeY->setValue(settingsConfig->value("sizeY").toInt());

    int colorsetssize = settingsPresets->arraySize("colorsets");
    for(int i = 0; i < colorsetssize; i++) {
        Settings::Group *srcset = settingsPresets->array("colorsets", i);
        Colorset dst;
        dst.name = srcset->value("name").toString();
        if(srcset->contains("builtin"))
            dst.builtin = srcset->value("builtin").toString();
        else
            dst.builtin.clear();

        auto item = new QTreeWidgetItem(QStringList({dst.name}));
        item->setData(0, Qt::ItemDataRole::UserRole, QVariant(i));
        if(dst.builtin.isEmpty())
            item->setFlags(item->flags() | Qt::ItemFlag::ItemIsEditable);
        ui->trwColorPresets->addTopLevelItem(item);

        for(int j = 0; j < srcset->value("size",0).toInt(); j++) {
            QString idtext = QString("%1").arg(j);
            dst.colors.push_back(srcset->value(idtext).value<QColor>());
        }
        QIcon icon = genColorsetIcon(dst.colors);
        ui->cobColorSet1->addItem(icon, dst.name, QVariant(i));
        ui->cobColorSet2->addItem(icon, dst.name, QVariant(i));
        ui->cobBorderColorSet->addItem(icon, dst.name, QVariant(i));
        colorsets.push_back(dst);
    }

    colorsetmodel = std::make_unique<ColorsetModel>();
    ui->lsvColors->setModel(colorsetmodel.get());

    connect(ui->trwColorPresets, &QTreeWidget::currentItemChanged,
    this, [this](QTreeWidgetItem *item, QTreeWidgetItem *prev) {
        int colorsetnum = item->data(0, Qt::ItemDataRole::UserRole).toInt();
        setColorset(colorsetnum);
        if(colorsetnum >= 0 && (unsigned)colorsetnum < colorsets.size()) {
            ui->tlbColorSetDelete->setDisabled(!colorsets[colorsetnum].builtin.isEmpty());
        }
    });
    connect(ui->trwColorPresets, &QTreeWidget::itemChanged,
    this, [this](QTreeWidgetItem *item, int column) {
        int colorsetnum = item->data(0, Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 || (unsigned)colorsetnum >= colorsets.size())
            return;
        if(column != 0)
            return;
        colorsets[colorsetnum].name = item->text(0);
        ui->cobColorSet1->setItemText(colorsetnum, item->text(0));
        ui->cobColorSet2->setItemText(colorsetnum, item->text(0));
        ui->cobBorderColorSet->setItemText(colorsetnum, item->text(0));
    });

    if(settingsConfig->value("bordercolorset").toInt() < (int)colorsets.size()) {
        auto const &set = colorsets[settingsConfig->value("bordercolorset").toInt()];
        for(int j = 0; j < (int)set.colors.size(); j++) {
            ui->cobBorderColor->addItem(genColoredIcon(set.colors[j]),
                                        QString("%1").arg(j),
                                        j);
        }
    }

    setColorset(settingsConfig->value("colorset1").toInt());
    ui->cobColorSet1->setCurrentIndex(settingsConfig->value("colorset1").toInt());
    ui->cobColorSet2->setCurrentIndex(settingsConfig->value("colorset2").toInt());
    ui->cobBorderColorSet->setCurrentIndex(settingsConfig->value("bordercolorset").toInt());
    ui->cobBorderColor->setCurrentIndex(settingsConfig->value("bordercolor").toInt());

    connect(ui->cobBorderColorSet, qOverload<int>(&QComboBox::currentIndexChanged),
    this, [this](int index) {
        if(index < 0 || (unsigned)index >= colorsets.size())
            return;
        auto const &set = colorsets[index];
        for(int j = 0;
                j < ui->cobBorderColor->count() && j < (int)set.colors.size(); j++) {
            ui->cobBorderColor->setItemIcon(j, genColoredIcon(set.colors[j]));
        }
        for(int j = ui->cobBorderColor->count();
                j < (int)set.colors.size(); j++) {
            ui->cobBorderColor->addItem(genColoredIcon(set.colors[j]),
                                        QString("%1").arg(j),
                                        j);
        }
        while(ui->cobBorderColor->count() > (int)set.colors.size()) {
            ui->cobBorderColor->removeItem(set.colors.size());
        }
    });
    connect(colorsetmodel.get(), &ColorsetModel::colorMapItemChanged,
    this, [this](int idx, QString const &value) {
        //if the current char set map is builtin, duplicate it.
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum)
            return;
        if(colorsets[colorsetnum].builtin.isEmpty())
            return;
        duplicateAndActivateColorSet(colorsetnum);
    });

    connect(ui->tlbColorSetNew, &QToolButton::clicked,
    this, [this]() {
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum)
            return;
        duplicateAndActivateColorSet(colorsetnum);
    });
    connect(ui->tlbColorSetDelete, &QToolButton::clicked,
    this, [this]() {
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum)
            return;
        if(!colorsets[colorsetnum].builtin.isEmpty())
            return;
        colorsets.erase(colorsets.begin()+colorsetnum);
        delete ui->trwColorPresets->takeTopLevelItem(colorsetnum);
        ui->cobColorSet1->removeItem(colorsetnum);
        ui->cobColorSet2->removeItem(colorsetnum);
        ui->cobBorderColorSet->removeItem(colorsetnum);
        renumberItems(colorsetnum);
        if((unsigned)colorsetnum >= colorsets.size())
            colorsetnum = colorsets.size() -1;
        setColorset(colorsetnum);
    });
    connect(ui->tlbColorSetMoveUp, &QToolButton::clicked,
    this, [this]() {
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt() - 1;
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum+1)
            return;
        auto set = colorsets[colorsetnum];
        colorsets[colorsetnum] = colorsets[colorsetnum+1];
        colorsets[colorsetnum+1] = set;
        ui->trwColorPresets->insertTopLevelItem(colorsetnum+1,
                                            ui->trwColorPresets->takeTopLevelItem(colorsetnum));
        swapItems(ui->cobColorSet1, colorsetnum, colorsetnum+1);
        swapItems(ui->cobColorSet2, colorsetnum, colorsetnum+1);
        int cidxb = ui->cobBorderColor->currentIndex();
        swapItems(ui->cobBorderColorSet, colorsetnum, colorsetnum+1);
        ui->cobBorderColor->setCurrentIndex(cidxb);
        renumberItems(colorsetnum);
        setColorset(colorsetnum);
    });
    connect(ui->tlbColorSetMoveDown, &QToolButton::clicked,
    this, [this]() {
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum+1)
            return;
        auto set = colorsets[colorsetnum];
        colorsets[colorsetnum] = colorsets[colorsetnum+1];
        colorsets[colorsetnum+1] = set;
        ui->trwColorPresets->insertTopLevelItem(colorsetnum+1,
                                            ui->trwColorPresets->takeTopLevelItem(colorsetnum));
        swapItems(ui->cobColorSet1, colorsetnum, colorsetnum+1);
        swapItems(ui->cobColorSet2, colorsetnum, colorsetnum+1);
        int cidxb = ui->cobBorderColor->currentIndex();
        swapItems(ui->cobBorderColorSet, colorsetnum, colorsetnum+1);
        ui->cobBorderColor->setCurrentIndex(cidxb);
        renumberItems(colorsetnum);
        setColorset(colorsetnum+1);
    });
    connect(ui->lsvColors, &QListView::doubleClicked,
    this, [this](QModelIndex const &index) {
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum)
            return;
        auto &set = colorsets[colorsetnum];
        QColor new_color = QColorDialog::getColor(
                           set.colors[index.row()], this,
                           QString("Select color for index %1").arg(index.row()));
        if(!new_color.isValid())
            return;
        set.colors[index.row()] = new_color;
        setColorset(colorsetnum);
        if(ui->cobBorderColorSet->currentIndex() == colorsetnum) {
            ui->cobBorderColor->setItemIcon(index.row(), genColoredIcon(new_color));
        }
        QIcon icon = genColorsetIcon(colorsets[colorsetnum].colors);
        ui->cobColorSet1->setItemIcon(colorsetnum, icon);
        ui->cobColorSet2->setItemIcon(colorsetnum, icon);
        ui->cobBorderColorSet->setItemIcon(colorsetnum, icon);
    });
    connect(ui->lsvColors->selectionModel(), &QItemSelectionModel::currentChanged,
    this, [this](const QModelIndex& current, const QModelIndex& previous) {
        ui->tlbColorDelete->setEnabled(current.isValid());
        ui->tlbColorMoveUp->setEnabled(current.row() > 0);
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum)
            return;

        auto &set = colorsets[colorsetnum];
        ui->tlbColorMoveDown->setEnabled(current.row()+1 < (int)set.colors.size());
    });
    connect(ui->tlbColorNew, &QToolButton::clicked,
    this, [this]() {
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum)
            return;
        auto &set = colorsets[colorsetnum];
        int colornum = ui->lsvColors->currentIndex().row();
        if(colornum >= 0 && (unsigned)colornum < set.colors.size())
            set.colors.push_back(set.colors[colornum]);
        else
            set.colors.push_back(Qt::black);
        if(ui->cobBorderColorSet->currentIndex() == colorsetnum) {
            int colornum = set.colors.size()-1;
            ui->cobBorderColor->addItem(genColoredIcon(set.colors[colornum]),
                                        QString("%1").arg(colornum),
                                        colornum);
        }
        QIcon icon = genColorsetIcon(set.colors);
        ui->cobColorSet1->setItemIcon(colorsetnum, icon);
        ui->cobColorSet2->setItemIcon(colorsetnum, icon);
        ui->cobBorderColorSet->setItemIcon(colorsetnum, icon);
        setColorset(colorsetnum);
    });
    connect(ui->tlbColorDelete, &QToolButton::clicked,
    this, [this]() {
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum)
            return;

        auto &set = colorsets[colorsetnum];
        int colornum = ui->lsvColors->currentIndex().row();

        set.colors.erase(set.colors.begin() + colornum);

        if(ui->cobBorderColorSet->currentIndex() == colorsetnum) {
            ui->cobBorderColor->removeItem(colornum);
        }
        QIcon icon = genColorsetIcon(set.colors);
        ui->cobColorSet1->setItemIcon(colorsetnum, icon);
        ui->cobColorSet2->setItemIcon(colorsetnum, icon);
        ui->cobBorderColorSet->setItemIcon(colorsetnum, icon);
        setColorset(colorsetnum);
    });
    connect(ui->tlbColorMoveUp, &QToolButton::clicked,
    this, [this]() {
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum)
            return;

        auto &set = colorsets[colorsetnum];
        int colornum = ui->lsvColors->currentIndex().row() -1;
        if(colornum < 0 || colornum+1 >= (int)set.colors.size())
            return;

        QColor t = set.colors[colornum];
        set.colors[colornum] = set.colors[colornum+1];
        set.colors[colornum+1] = t;

        if(ui->cobBorderColorSet->currentIndex() == colorsetnum) {
            int cidx = ui->cobBorderColor->currentIndex();
            ui->cobBorderColor->removeItem(colornum);
            ui->cobBorderColor->removeItem(colornum);
            ui->cobBorderColor->insertItem(colornum,
                                           genColoredIcon(set.colors[colornum]),
                                           QString("%1").arg(colornum),
                                        colornum);
            ui->cobBorderColor->insertItem(colornum+1,
                                           genColoredIcon(set.colors[colornum+1]),
                                           QString("%1").arg(colornum+1),
                                        colornum+1);
            if(cidx == colornum)
                cidx = colornum+1;
            else if (cidx == colornum+1)
                cidx = colornum;
            ui->cobBorderColor->setCurrentIndex(cidx);
        }
        QIcon icon = genColorsetIcon(set.colors);
        ui->cobColorSet1->setItemIcon(colorsetnum, icon);
        ui->cobColorSet2->setItemIcon(colorsetnum, icon);
        ui->cobBorderColorSet->setItemIcon(colorsetnum, icon);
        setColorset(colorsetnum);
        ui->lsvColors->setCurrentIndex(colorsetmodel->index(colornum, 0, QModelIndex()));
    });
    connect(ui->tlbColorMoveDown, &QToolButton::clicked,
    this, [this]() {
        int colorsetnum = ui->trwColorPresets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(colorsetnum < 0 ||
                colorsets.size() <= (unsigned)colorsetnum)
            return;

        auto &set = colorsets[colorsetnum];
        int colornum = ui->lsvColors->currentIndex().row();
        if(colornum < 0 || colornum+1 >= (int)set.colors.size())
            return;

        QColor t = set.colors[colornum];
        set.colors[colornum] = set.colors[colornum+1];
        set.colors[colornum+1] = t;

        if(ui->cobBorderColorSet->currentIndex() == colorsetnum) {
            int cidx = ui->cobBorderColor->currentIndex();
            ui->cobBorderColor->removeItem(colornum);
            ui->cobBorderColor->removeItem(colornum);
            ui->cobBorderColor->insertItem(colornum,
                                           genColoredIcon(set.colors[colornum]),
                                           QString("%1").arg(colornum),
                                        colornum);
            ui->cobBorderColor->insertItem(colornum+1,
                                           genColoredIcon(set.colors[colornum+1]),
                                           QString("%1").arg(colornum+1),
                                        colornum+1);
            if(cidx == colornum)
                cidx = colornum+1;
            else if (cidx == colornum+1)
                cidx = colornum;
            ui->cobBorderColor->setCurrentIndex(cidx);
        }
        QIcon icon = genColorsetIcon(set.colors);
        ui->cobColorSet1->setItemIcon(colorsetnum, icon);
        ui->cobColorSet2->setItemIcon(colorsetnum, icon);
        ui->cobBorderColorSet->setItemIcon(colorsetnum, icon);
        setColorset(colorsetnum);
        ui->lsvColors->setCurrentIndex(colorsetmodel->index(colornum+1, 0, QModelIndex()));
    });
    connect(this, &HX20CrtDeviceGfxCfg::accepted,
            this, &HX20CrtDeviceGfxCfg::saveConfig);
}

HX20CrtDeviceGfxCfg::~HX20CrtDeviceGfxCfg() {
}

void HX20CrtDeviceGfxCfg::saveConfig() {
    //save config
    settingsConfig->setValue("sizeX", ui->spbSizeX->value());
    settingsConfig->setValue("sizeY", ui->spbSizeY->value());

    settingsConfig->setValue("colorset1", ui->cobColorSet1->currentIndex());
    settingsConfig->setValue("colorset2", ui->cobColorSet2->currentIndex());
    settingsConfig->setValue("bordercolorset", ui->cobBorderColorSet->currentIndex());
    settingsConfig->setValue("bordercolor", ui->cobBorderColor->currentIndex());

    for(int i = 0; i < (int)colorsets.size(); i++) {
        Settings::Group *dstset = settingsPresets->array("colorsets", i);
        auto const &src = colorsets[i];
        dstset->setValue("name", src.name);
        if(src.builtin.isEmpty())
            dstset->remove("builtin");
        else
            dstset->setValue("builtin", src.builtin);
        for(int j = 0; j < (int)src.colors.size(); j++) {
            QString idtext = QString("%1").arg(j);
            dstset->setValue(idtext, src.colors[j]);
        }
        dstset->setValue("size", (int)src.colors.size());
    }
    settingsPresets->setArraySize("colorsets", colorsets.size());
}
