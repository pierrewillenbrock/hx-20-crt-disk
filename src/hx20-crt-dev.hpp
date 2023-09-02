
#pragma once

#include <stdint.h>
#include <QWidget>
#include <array>

#include "hx20-ser-proto.hpp"

QT_BEGIN_NAMESPACE

class QMainWindow;
class QTextEdit;
class QMenu;

QT_END_NAMESPACE

namespace Settings {
class Group;
};

class HX20CrtGraphicsView : public QWidget {
    Q_OBJECT;
public:
    int width;
    int height;
    float zoom;
    std::vector<uint8_t> image_data;
    std::array<QRgb, 256> color_map;
    std::unique_ptr<QImage> image;
    QRgb border_color;
    HX20CrtGraphicsView(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    ~HX20CrtGraphicsView();
    virtual QSize sizeHint() const override;
public slots:
    void updateImage();
protected:
    virtual void paintEvent(QPaintEvent *event) override;
    virtual void resizeEvent(QResizeEvent *event) override;
};

class HX20CrtDevice : public QObject, public HX20SerialDevice {
    Q_OBJECT;
public:
    struct Character {
        uint8_t cols[6];
    };
private:
    uint8_t virt_width;
    uint8_t virt_height;
    uint8_t win_width;
    uint8_t win_height;
    uint8_t cur_x;
    uint8_t cur_y;
    uint8_t win_x;
    uint8_t win_y;
    uint8_t access_x;
    uint8_t access_y;
    uint8_t color_set;
    int graph_width;
    int graph_height;
    QColor text_color_1;
    QColor text_color_2;
    QColor text_background;
    QColor text_border;
    std::array<QString, 256> text_char_map;

    uint8_t background_color;
    uint8_t cursor_margin;
    uint8_t horizontal_scroll_step;
    uint8_t vertical_scroll_step;

    std::vector<uint8_t> char_data;
    std::vector<uint8_t> line_cont;
    bool list_flag;

    HX20CrtGraphicsView *graphicsview;
    QTextEdit *textview;
    Settings::Group *settingsConfig;
    Settings::Group *settingsPresets;

    void redrawText();
    void processCharacter(uint8_t ch);
    bool windowFollowCursor();
    void updateGraphicsColors();
protected:
    virtual int getDeviceID() const override;
    virtual int gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                           uint16_t size, uint8_t *buf,
                           HX20SerialConnection *conn) override;
public:
    HX20CrtDevice();
    ~HX20CrtDevice();
    void addDocksToMainWindow(QMainWindow *window, QMenu *devices_menu);
    void setSettings(Settings::Group *settingsConfig,
                     Settings::Group *settingsPresets);
private slots:
    void updateFromConfig();
};

