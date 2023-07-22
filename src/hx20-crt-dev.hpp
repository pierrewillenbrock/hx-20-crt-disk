
#pragma once

#include <stdint.h>
#include <gtkmm.h>

#include "hx20-ser-proto.hpp"

class HX20CrtDevice : public HX20SerialDevice {
public:
    struct Character {
        uint8_t cols[6];
    };
private:
    Gtk::DrawingArea drawable;
    uint8_t virt_width;
    uint8_t virt_height;
    uint8_t win_width;
    uint8_t win_height;
    uint8_t cur_x;
    uint8_t cur_y;
    uint8_t win_x;
    uint8_t win_y;
    int graph_width;
    int graph_height;

    uint8_t background_color;

    uint8_t *image_data;
    uint8_t *char_data;
    uint8_t *line_cont;
    bool list_flag;

    struct Character chars[256];

    guint32 colors[8];

    void redrawText();
    void eraseCursor();
    void drawCursor();
    void processCharacter(uint8_t ch);

    bool expose_event(GdkEventExpose *event);
    bool configure_event(GdkEventConfigure *event);
protected:
    virtual int getDeviceID() const override;
    virtual int gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                           uint16_t size, uint8_t *buf,
                           HX20SerialConnection *conn) override;
public:
    HX20CrtDevice();
    Gtk::Widget &getWidget();
};

