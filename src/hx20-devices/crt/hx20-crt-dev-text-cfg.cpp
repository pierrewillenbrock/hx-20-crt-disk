
#include "hx20-crt-dev-text-cfg.hpp"

#include <QPainter>
#include <QColorDialog>
#include <QFont>
#include <QIconEngine>

#include <iostream>

#include "ui_hx20-crt-dev-text-cfg.h"
#include "../../settings.hpp"

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

static QPixmap genCharIconPixmap(QString const &v, QFont const &f, QSize const &size) {
    //maybe a good idea to use an QIconEngine for this
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setFont(f);
    QTextOption to;
    to.setAlignment(Qt::AlignmentFlag::AlignCenter);
    p.drawText(QRect(QPoint(0,0), size), v, to);
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

static QIcon genCharIcon(QString const &v, std::unordered_map<int, QFont> const &fontmap) {
    auto it = chariconcache.find(v);
    if(it != chariconcache.end())
        return it->second;

    QIcon i;
    for(auto &s : iconSizes) {
        i.addPixmap(genCharIconPixmap(v, fontmap.at(s), QSize(s,s)));
    }
    chariconcache[v] = i;
    return i;
}

static QString nameForColor(QColor const &color) {
    for(auto &n : QColor::colorNames()) {
      if(color == QColor(n)) {
        return n;
      }
    }
    return color.name();
}

static void setupColorCombobox(QComboBox *cbo, QColor const &color) {
    cbo->clear();
    cbo->addItem(genColoredIcon(color),
                 QString("Current: %1").arg(nameForColor(color)), QVariant(color));
    cbo->addItem(QIcon::fromTheme("color-profile"), "Pick color...");
    //TODO additional colors, but from what list?
    cbo->addItem(genColoredIcon(Qt::green),
                 QString("green"), QVariant(QColor(Qt::green)));
    cbo->addItem(genColoredIcon(QColor("orange")),
                 QString("orange"), QVariant(QColor("orange")));
    cbo->addItem(genColoredIcon(Qt::black),
                 QString("black"), QVariant(QColor(Qt::black)));
    //TODO maybe we should use a model? but then we cannot just do "Current:..."
    cbo->connect(cbo, qOverload<int>(&QComboBox::currentIndexChanged),
    [cbo](int index) {
        if(cbo->itemData(index).isValid())
            return;
        QColor c = QColorDialog::getColor(Qt::white, cbo, "Select color");
        if(!c.isValid())
            return;
        for(int i = 0; i < cbo->count(); i++) {
            if(c == cbo->itemData(i).value<QColor>()) {
                cbo->setCurrentIndex(i);
                return;
            }
        }
        cbo->addItem(genColoredIcon(c),
                     nameForColor(c), QVariant(c));
        cbo->setCurrentIndex(cbo->count()-1);
    });
}

CharsetModel::CharsetModel() {}

void CharsetModel::setCharMap(QStringList const &char_map) {
    this->char_map = char_map;
    emit dataChanged(createIndex(0,0),
                     createIndex(255,0),
    {Qt::ItemDataRole::EditRole, Qt::ItemDataRole::DecorationRole});
}

void CharsetModel::setFontMap(std::unordered_map<int, QFont> const &fontmap) {
    this->fontmap = fontmap;
    emit dataChanged(createIndex(0,0),
                     createIndex(255,0),
    {Qt::ItemDataRole::DecorationRole});
}

QVariant CharsetModel::data(const QModelIndex &index, int role) const {
    if(!index.isValid() || index.row() < 0 || index.row() >= 256)
        return QVariant();
    int ch = (index.row()+32) % 256;
    if(ch >= char_map.size())
        return QVariant();
    switch(role) {
    case Qt::ItemDataRole::DisplayRole:
        return QString("%1").arg(ch);
    case Qt::ItemDataRole::EditRole:
        return char_map[ch];
    case Qt::ItemDataRole::DecorationRole:
        return genCharIcon(char_map[ch], fontmap);
    }
    return QVariant();
}

bool CharsetModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if(!index.isValid() || index.row() < 0 || index.row() >= 256)
        return false;
    int ch = (index.row()+32) % 256;
    if(ch < 0 || ch >= char_map.size())
        return false;
    switch(role) {
    case Qt::ItemDataRole::EditRole:
        //this may switch out the current charset, but will not change our map.
        emit charMapItemChanged(ch, value.toString());
        assert(ch >= 0 && ch < char_map.size());
        char_map[ch] = value.toString();
        emit dataChanged(index, index,
        {Qt::ItemDataRole::EditRole, Qt::ItemDataRole::DecorationRole});
        return true;
    }
    return false;
}

Qt::ItemFlags CharsetModel::flags(const QModelIndex &index) const {
    return Qt::ItemFlag::ItemIsEditable |
           Qt::ItemFlag::ItemIsEnabled |
           Qt::ItemFlag::ItemIsSelectable |
           Qt::ItemFlag::ItemNeverHasChildren;
}

QModelIndex CharsetModel::index(int row, int column, const QModelIndex &parent) const {
    if(parent.isValid())
        return QModelIndex();
    return createIndex(row, column);
}

QModelIndex CharsetModel::parent(const QModelIndex &child) const {
    return QModelIndex();
}

int CharsetModel::rowCount(const QModelIndex &parent) const {
    if(parent.isValid())
        return 0;
    return 256;
}

int CharsetModel::columnCount(const QModelIndex &parent) const {
    return 1;
}

void HX20CrtDeviceTextCfg::setCharset(int charsetnum) {
    QStringList map;
    if(charsetnum < (int)charsets.size()) {
        map = charsets[charsetnum].chars;
        ui->trwCharsets->setCurrentItem(ui->trwCharsets->topLevelItem(charsetnum));
        ui->trwCharsets->scrollToItem(ui->trwCharsets->topLevelItem(charsetnum));
    }
    ui->tlbCharSetMoveUp->setEnabled(charsetnum > 0);
    ui->tlbCharSetMoveDown->setEnabled(charsetnum < (int)charsets.size()-1);
    ui->tlbCharSetDelete->setDisabled(!charsets[charsetnum].builtin.isEmpty());
    charsetmodel->setCharMap(map);
}

void HX20CrtDeviceTextCfg::duplicateAndActivateCharSet(int charsetnum) {
    if(charsetnum < (int)charsets.size()) {
        auto const &srcset = charsets[charsetnum];
        Charset dstset = srcset;
        QString name = srcset.name;
        name.replace("(builtin)","");
        name.replace("; builtin)",")");
        dstset.name = name+"(copy)";
        dstset.builtin.clear();

        int dstnum = charsets.size();
        charsets.push_back(dstset);

        auto item = new QTreeWidgetItem(QStringList({dstset.name}));
        item->setData(0, Qt::ItemDataRole::UserRole, QVariant(dstnum));
        item->setFlags(item->flags() | Qt::ItemFlag::ItemIsEditable);
        ui->trwCharsets->addTopLevelItem(item);
        ui->trwCharsets->setCurrentItem(item);
        ui->cobCharSet->addItem(dstset.name, QVariant(dstnum));
    }
}

HX20CrtDeviceTextCfg::HX20CrtDeviceTextCfg(Settings::Group *settingsConfig,
        Settings::Group *settingsPresets,
        QWidget *parent, Qt::WindowFlags f)
    : QDialog(parent, f), ui(std::make_unique<Ui::HX20CrtDeviceTextCfgUi>()),
      settingsConfig(settingsConfig), settingsPresets(settingsPresets)
{
    ui->setupUi(this);

    ui->spbVirtualSizeX->setValue(settingsConfig->value("virtualSizeX").toInt());
    ui->spbVirtualSizeY->setValue(settingsConfig->value("virtualSizeY").toInt());
    ui->spbWindowSizeX->setValue(settingsConfig->value("windowSizeX").toInt());
    ui->spbWindowSizeY->setValue(settingsConfig->value("windowSizeY").toInt());
    setupColorCombobox(ui->cobColor1, settingsConfig->value("color1").value<QColor>());
    setupColorCombobox(ui->cobColor2, settingsConfig->value("color2").value<QColor>());
    setupColorCombobox(ui->cobBackgroundColor, settingsConfig->value("background").value<QColor>());
    setupColorCombobox(ui->cobBorderColor, settingsConfig->value("border").value<QColor>());

    int charsetssize = settingsPresets->arraySize("charsets");
    for(int i = 0; i < charsetssize; i++) {
        Settings::Group *srcset = settingsPresets->array("charsets", i);
        Charset dst;
        dst.name = srcset->value("name").toString();
        if(srcset->contains("builtin"))
            dst.builtin = srcset->value("builtin").toString();
        else
            dst.builtin.clear();

        auto item = new QTreeWidgetItem(QStringList({dst.name}));
        item->setData(0, Qt::ItemDataRole::UserRole, QVariant(i));
        if(dst.builtin.isEmpty())
            item->setFlags(item->flags() | Qt::ItemFlag::ItemIsEditable);
        ui->trwCharsets->addTopLevelItem(item);
        ui->cobCharSet->addItem(dst.name, QVariant(i));

        for(int j = 0; j < 256; j++) {
            QString idtext = QString("%1").arg(j);
            dst.chars.append(srcset->value(idtext).toString());
        }
        charsets.push_back(dst);
    }

    charsetmodel = std::make_unique<CharsetModel>();
    ui->lsvChars->setModel(charsetmodel.get());

    QFont font;
    font.setFamilies({"Courier", "Mono"});
    font.setFixedPitch(true);
    std::unordered_map< int, QFont > fontmap;
    for(auto &s : iconSizes) {
        QPixmap pixmap(QSize(s,s));
        QFontMetrics fm(font, &pixmap);

        float zx = s * 1.0f / fm.boundingRect("W").width();
        float zy = s * 1.0f / fm.boundingRect("W").height();
        float z = qMin(zx,zy);

        QFont f2(font);
        f2.setPointSizeF(font.pointSizeF()*z);
        fontmap[s] = f2;
    }

    charsetmodel->setFontMap(fontmap);

    connect(ui->trwCharsets, &QTreeWidget::currentItemChanged,
    this, [this](QTreeWidgetItem *item, QTreeWidgetItem *prev) {
        int charsetnum = item->data(0, Qt::ItemDataRole::UserRole).toInt();
        setCharset(charsetnum);
        if(charsetnum >= 0 && (unsigned)charsetnum < charsets.size()) {
            ui->tlbCharSetDelete->setDisabled(!charsets[charsetnum].builtin.isEmpty());
        }
    });
    connect(ui->trwCharsets, &QTreeWidget::itemChanged,
    this, [this](QTreeWidgetItem *item, int column) {
        int charsetnum = item->data(0, Qt::ItemDataRole::UserRole).toInt();
        if(charsetnum < 0 || (unsigned)charsetnum >= charsets.size())
            return;
        if(column != 0)
            return;
        charsets[charsetnum].name = item->text(0);
        ui->cobCharSet->setItemText(charsetnum, item->text(0));
    });

    setCharset(settingsConfig->value("charset").toInt());
    ui->cobCharSet->setCurrentIndex(settingsConfig->value("charset").toInt());

    connect(charsetmodel.get(), &CharsetModel::charMapItemChanged,
    this, [this](int idx, QString const &value) {
        //if the current char set map is builtin, duplicate it.
        int charsetnum = ui->trwCharsets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(charsetnum < 0 ||
                charsets.size() <= (unsigned)charsetnum)
            return;
        if(charsets[charsetnum].builtin.isEmpty())
            return;
        duplicateAndActivateCharSet(charsetnum);
    });

    connect(ui->tlbCharSetNew, &QToolButton::clicked,
    this, [this]() {
        int charsetnum = ui->trwCharsets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(charsetnum < 0 ||
                charsets.size() <= (unsigned)charsetnum)
            return;
        duplicateAndActivateCharSet(charsetnum);
    });
    connect(ui->tlbCharSetDelete, &QToolButton::clicked,
    this, [this]() {
        int charsetnum = ui->trwCharsets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(charsetnum < 0 ||
                charsets.size() <= (unsigned)charsetnum)
            return;
        if(!charsets[charsetnum].builtin.isEmpty())
            return;
        charsets.erase(charsets.begin()+charsetnum);
        delete ui->trwCharsets->takeTopLevelItem(charsetnum);
        ui->cobCharSet->removeItem(charsetnum);
        for(int i = charsetnum; i < ui->trwCharsets->topLevelItemCount(); i++) {
            ui->trwCharsets->topLevelItem(i)->setData(0, Qt::ItemDataRole::UserRole, QVariant(i));
            ui->cobCharSet->setItemData(i, QVariant(i));
        }
        if((unsigned)charsetnum >= charsets.size())
            charsetnum = charsets.size() -1;
        setCharset(charsetnum);
    });
    connect(ui->tlbCharSetMoveUp, &QToolButton::clicked,
    this, [this]() {
        int charsetnum = ui->trwCharsets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt() - 1;
        if(charsetnum < 0 ||
                charsets.size() <= (unsigned)charsetnum+1)
            return;
        auto set = charsets[charsetnum];
        charsets[charsetnum] = charsets[charsetnum+1];
        charsets[charsetnum+1] = set;
        ui->trwCharsets->insertTopLevelItem(charsetnum+1,
                                            ui->trwCharsets->takeTopLevelItem(charsetnum));
        int cidx = ui->cobCharSet->currentIndex();
        ui->cobCharSet->removeItem(charsetnum);
        ui->cobCharSet->insertItem(charsetnum+1, set.name, QVariant(charsetnum+1));
        if(cidx == charsetnum)
            ui->cobCharSet->setCurrentIndex(charsetnum+1);
        else if(cidx == charsetnum+1)
            ui->cobCharSet->setCurrentIndex(charsetnum);
        for(int i = charsetnum; i < ui->trwCharsets->topLevelItemCount(); i++) {
            ui->trwCharsets->topLevelItem(i)->setData(0, Qt::ItemDataRole::UserRole, QVariant(i));
            ui->cobCharSet->setItemData(i, QVariant(i));
        }
        setCharset(charsetnum);
    });
    connect(ui->tlbCharSetMoveDown, &QToolButton::clicked,
    this, [this]() {
        int charsetnum = ui->trwCharsets->currentItem()->data(0,
                         Qt::ItemDataRole::UserRole).toInt();
        if(charsetnum < 0 ||
                charsets.size() <= (unsigned)charsetnum+1)
            return;
        auto set = charsets[charsetnum];
        charsets[charsetnum] = charsets[charsetnum+1];
        charsets[charsetnum+1] = set;
        ui->trwCharsets->insertTopLevelItem(charsetnum+1,
                                            ui->trwCharsets->takeTopLevelItem(charsetnum));
        int cidx = ui->cobCharSet->currentIndex();
        ui->cobCharSet->removeItem(charsetnum);
        ui->cobCharSet->insertItem(charsetnum+1, set.name, QVariant(charsetnum+1));
        if(cidx == charsetnum)
            ui->cobCharSet->setCurrentIndex(charsetnum+1);
        else if(cidx == charsetnum+1)
            ui->cobCharSet->setCurrentIndex(charsetnum);
        for(int i = charsetnum; i < ui->trwCharsets->topLevelItemCount(); i++) {
            ui->trwCharsets->topLevelItem(i)->setData(0, Qt::ItemDataRole::UserRole, QVariant(i));
            ui->cobCharSet->setItemData(i, QVariant(i));
        }
        setCharset(charsetnum+1);
    });
    connect(this, &HX20CrtDeviceTextCfg::accepted,
            this, &HX20CrtDeviceTextCfg::saveConfig);
}

HX20CrtDeviceTextCfg::~HX20CrtDeviceTextCfg() {
}

void HX20CrtDeviceTextCfg::saveConfig() {
    //save config
    settingsConfig->setValue("virtualSizeX", ui->spbVirtualSizeX->value());
    settingsConfig->setValue("virtualSizeY", ui->spbVirtualSizeY->value());
    settingsConfig->setValue("windowSizeX", ui->spbWindowSizeX->value());
    settingsConfig->setValue("windowSizeY", ui->spbWindowSizeY->value());
    settingsConfig->setValue("color1", ui->cobColor1->currentData());
    settingsConfig->setValue("color2", ui->cobColor2->currentData());
    settingsConfig->setValue("background", ui->cobBackgroundColor->currentData());
    settingsConfig->setValue("border", ui->cobBorderColor->currentData());
    settingsConfig->setValue("charset", ui->cobCharSet->currentIndex());

    for(int i = 0; i < (int)charsets.size(); i++) {
        Settings::Group *dstset = settingsPresets->array("charsets", i);
        auto const &src = charsets[i];
        dstset->setValue("name", src.name);
        if(src.builtin.isEmpty())
            dstset->remove("builtin");
        else
            dstset->setValue("builtin", src.builtin);
        for(int j = 0; j < src.chars.size(); j++) {
            QString idtext = QString("%1").arg(j);
            dstset->setValue(idtext, src.chars[j]);
        }
    }
    settingsPresets->setArraySize("charsets", charsets.size());
}
