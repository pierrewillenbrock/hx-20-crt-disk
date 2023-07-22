
#include "hx20-crt-dev.hpp"

bool
HX20CrtDevice::expose_event(GdkEventExpose *event) {
    int x,y;
    uint8_t *p = image_data;

    Glib::RefPtr<Gdk::Colormap> map =
    drawable.get_window()->get_colormap();

    Glib::RefPtr<Gdk::Pixbuf> pb = Gdk::Pixbuf::create(
                                   Gdk::COLORSPACE_RGB,
                                   false,
                                   8,
                                   graph_width,
                                   graph_height);

    guint8 *pix = pb->get_pixels();
    int rs = pb->get_rowstride();
    int ch = pb->get_n_channels();

    for(y = 0; y < graph_height; y++) {
        for(x = 0; x < graph_width; x++) {
            guint32 c = colors[(*p<8)?*p:0];
            pix[rs*y+ch*x] = c >> 16;
            pix[rs*y+ch*x+1] = c >> 8;
            pix[rs*y+ch*x+2] = c >> 0;
            p++;
        }
    }

    drawable.get_window()->
    draw_pixbuf(Glib::RefPtr<const Gdk::GC>(NULL),
                pb,
                0,0,
                0,0,
                graph_width, graph_height,
                Gdk::RGB_DITHER_NONE,
                0, 0);

    return true;
}

bool
HX20CrtDevice::configure_event(GdkEventConfigure *event) {
    drawable.queue_draw_area(0, 0,
                             drawable.get_allocation().get_width(),
                             drawable.get_allocation().get_height());
    return TRUE;
}

int HX20CrtDevice::getDeviceID() const {
    return 0x30;
}

//some bresenham, with clipping
static void
draw_line(uint8_t *image, int width, int height,
          int x1, int y1, int x2, int y2, uint8_t color) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    int px = x1;
    int py = y1;
    if(dx > dy) {
        if(dx < 0) {
            px += dx;
            dx = -dx;
            py += dy;
            dy = -dy;
        }
        int end = px + dx + 1;
        if(end > width)
            end = width;
        int frac = dx / 2;
        while((px < 0 || py >= height || py < 0) &&
                px != end) {
            frac += dy;
            if(frac > dx) {
                frac -= dx;
                py++;
            }
            if(frac < 0) {
                frac += dx;
                py--;
            }
            px++;
        }
        while(px != end && py >= 0 && py < height) {
            image[px+py*width] = color;
            frac += dy;
            if(frac > dx) {
                frac -= dx;
                py++;
            }
            if(frac < 0) {
                frac += dx;
                py--;
            }
            px++;
        }
    } else {
        if(dy < 0) {
            px += dx;
            dx = -dx;
            py += dy;
            dy = -dy;
        }
        int end = py + dy + 1;
        if(end > height)
            end = height;
        int frac = dy / 2;
        while((py < 0 || px >= width || px < 0) &&
                py != end) {
            frac += dx;
            if(frac > dy) {
                frac -= dy;
                px++;
            }
            if(frac < 0) {
                frac += dy;
                px--;
            }
            py++;
        }
        while(py != end && px >= 0 && px < width) {
            image[px+py*width] = color;
            frac += dx;
            if(frac > dy) {
                frac -= dy;
                px++;
            }
            if(frac < 0) {
                frac += dy;
                px--;
            }
            py++;
        }
    }
}

void HX20CrtDevice::redrawText() {
    //redraws all Text + cursor if needed
    int x,y;
    for(y = win_y; y < win_y+win_height; y++) {
        int len;
        for(len = virt_width-1;
                len >= 0; len--) {
            if(char_data[y*virt_width+len] != 0x20)
                break;
        }
        for(x = win_x; x < win_x + win_width && x <= len; x++) {
            int cx, cy;
            int px = x - win_x;
            int py = y - win_y;
            uint8_t ch = char_data[y*virt_width+x];
            for(cx = 0; cx < 6; cx++) {
                for(cy = 0; cy < 8; cy++) {
                    if(chars[ch].cols[cx] & (1 << cy))
                        image_data[px*6+cx+
                                   (py*8+cy)*graph_width] = 1;//color ?
                    else
                        image_data[px*6+cx+
                                   (py*8+cy)*graph_width] = background_color;
                }
            }

            drawable.queue_draw_area(px*6,
                                     py*8,
                                     px*6+6,
                                     py*8+8);
        }
    }
    drawCursor();
}

void HX20CrtDevice::eraseCursor() {
    if(cur_x >= win_x && cur_x < win_x + win_width &&
            cur_y >= win_y && cur_y < win_y + win_height) {
        int i;
        for(i = 0; i < 6; i++)
            image_data[(cur_x-win_x)*6+i +
                                         ((cur_y-win_y)*8+7)*graph_width] =
                       background_color;
        drawable.queue_draw_area((cur_x-win_x)*6,
                                 (cur_y-win_y)*8+7,
                                 (cur_x-win_x)*6+6,
                                 (cur_y-win_y)*8+8);
    }
}

void HX20CrtDevice::drawCursor() {
    if(cur_x >= win_x && cur_x < win_x + win_width &&
            cur_y >= win_y && cur_y < win_y + win_height) {
        int i;
        for(i = 0; i < 6; i++)
            image_data[(cur_x-win_x)*6+i +
                                         ((cur_y-win_y)*8+7)*graph_width] =
                       1;//todo: color?
        drawable.queue_draw_area((cur_x-win_x)*6,
                                 (cur_y-win_y)*8+7,
                                 (cur_x-win_x)*6+6,
                                 (cur_y-win_y)*8+8);
    }
}

void HX20CrtDevice::processCharacter(uint8_t ch) {
    printf("processing %c(%02x)\n",(ch >= 0x20 && ch < 0x7f)?ch:'?',ch);
    switch(ch) {
    case 0x00:
    case 0x02:
    case 0x03:
    case 0x07:
    case 0x0e:
    case 0x0f:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1b:
        printf("unhandled ctl code %02x\n",ch);
        break;
    case 0x01:
        if(win_x != 0) {
            win_x = 0;
            cur_x = 0x10;//??
            redrawText();
        } else if(cur_x != 0x10) {
            eraseCursor();//if visible
            cur_x = 0x10;
            drawCursor();//if visible
        }
        break;
    case 0x04:
        if(win_x + win_width < virt_width && !list_flag) {
            win_x += 1;//horiz scroll steps
            if(win_x + win_width < virt_width)
                win_x = virt_width - win_width;
            redrawText();
        }
        break;
    case 0x05: {
        int x,y;
        x = cur_x;
        y = cur_y;
        while(y < virt_height) {
            char_data[x+y*virt_height] = 0x20;
            x++;
            if(x >= virt_width) {
                y++;
                if(y >= virt_height || !line_cont[y])
                    break;
                x = 0;
            }
        }
        redrawText();
        break;
    }
    case 0x06:
        if(win_x + win_width != virt_width && !list_flag) {
            win_x = virt_width - win_width;
            cur_x = win_x + 0x10;//??
            redrawText();
        } else if(cur_x != win_x + 0x10 && !list_flag) {
            eraseCursor();//if visible
            cur_x = win_x + 0x10;
            drawCursor();//if visible
        }
        break;
    case 0x08: {
        if(cur_x == 0 && (!line_cont[cur_y] || cur_y == 0))
            cur_x++;

        int y;
        y = cur_y;
        while(y < virt_height && line_cont[y+1]) {
            y++;
        }
        memmove(&char_data[cur_x-1+cur_y*virt_width],
                &char_data[cur_x+cur_y*virt_width],
                virt_width-cur_x+
                (y-cur_y)*virt_width);
        char_data[virt_width*(y+1)-1] = 0x20;

        if(cur_x == 0) {
            cur_x = virt_width -1;
            cur_y--;
        } else
            cur_x--;

        if(cur_x < win_x && !list_flag)
            win_x = cur_x;
        if(cur_y < win_y)
            win_y = cur_y;
        if(cur_x >= win_x + win_width && !list_flag)
            win_x = cur_x - win_width-1;
        if(cur_y >= win_y + win_height)
            win_y = cur_y - win_height-1;
        redrawText();
        break;
    }
    case 0x09: {
        uint8_t new_x;
        new_x = (cur_x & ~7) + 8;
        if(new_x > virt_width)
            new_x = 0;
        if(new_x < win_x && !list_flag) {
            win_x = cur_x = new_x;
            redrawText();
        } else if(new_x >= win_x + win_width && !list_flag) {
            cur_x = new_x;
            win_x = cur_x - win_width-1;
            redrawText();
        } else {
            eraseCursor();//if visible
            cur_x = new_x;
            drawCursor();//if visible
        }
        break;
    }
    case 0x0a:
        if(cur_y+1 >= virt_height) {
            cur_y = virt_height - 1;
            memmove(&char_data[0],&char_data[virt_width],
                    virt_width*(virt_height-1));
            redrawText();
        } else if(cur_y+1 >= win_y + win_height) {
            cur_y++;
            win_y = cur_y - win_height + 1;
            redrawText();
        } else {
            eraseCursor();//if visible
            cur_y++;
            drawCursor();//if visible
        }
        break;
    case 0x0b:
        if(win_x != 0 || win_y != 0) {
            cur_x = 0;
            cur_y = 0;
            win_x = 0;
            win_y = 0;
            redrawText();
        } else if(cur_x != 0 || cur_y != 0) {
            eraseCursor();//if visible
            cur_x = 0;
            cur_y = 0;
            drawCursor();//if visible
        }
        break;
    case 0x0c:
        memset(char_data, 0x20, virt_width*virt_height);
        cur_x = 0;
        cur_y = 0;
        win_x = 0;
        win_y = 0;
        redrawText();
        break;
    case 0x0d:
        if(win_x != 0) {
            cur_x = 0;
            win_x = 0;
            redrawText();
        } else {
            eraseCursor();//if visible
            cur_x = 0;
            drawCursor();//if visible
        }
        if(cur_y < virt_height)
            line_cont[cur_y+1] = 0;
        break;
    case 0x10:
        if(win_y >= 1) {
            win_y -= 1;
            cur_x = 0x10;
            redrawText();
        } else if(win_y > 0) {
            win_y = 0;
            cur_x = 0x10;
            redrawText();
        } else if(cur_x != 0x10) {
            eraseCursor();//if visible
            cur_x = 0x10;
            drawCursor();//if visible
        }
        break;
    case 0x11:
        if(win_y + win_height + 1 < virt_height) {
            win_y += 1;
            cur_x = 0x10;
            redrawText();
        } else if(win_y + win_height > virt_height) {
            win_y = virt_height - win_height;
            cur_x = 0x10;
            redrawText();
        } else if(cur_x != 0x10) {
            eraseCursor();//if visible
            cur_x = 0x10;
            drawCursor();//if visible
        }
        break;
    case 0x12: {
        int ly = cur_y;
        while(ly+1 < virt_height && line_cont[ly+1])
            ly++;
        if(char_data[virt_width*(ly+1)-1] != 0x20) {
            if(ly < virt_height-1) {
                memmove(&char_data[virt_width*(ly+2)],
                        &char_data[virt_width*(ly+1)],
                        virt_width*(virt_height-ly-2));
                memset(&char_data[virt_width*(ly+1)],0x20,
                       virt_width);
                line_cont[ly+1] = 0xff;
            } else {
                memmove(&char_data[0],
                        &char_data[virt_width],
                        virt_width*(virt_height-1));
                memset(&char_data[virt_width*(virt_height-1)],
                       0x20, virt_width);
                cur_y--;
                if(cur_y < win_y)
                    win_y = cur_y;
            }
            memmove(&char_data[virt_width*cur_y+cur_x+1],
                    &char_data[virt_width*cur_y+cur_x],
                    virt_width*(ly-cur_y+1)-cur_x);
        } else {
            memmove(&char_data[virt_width*cur_y+cur_x+1],
                    &char_data[virt_width*cur_y+cur_x],
                    virt_width*(ly-cur_y+1)-cur_x-1);
        }
        redrawText();
        break;
    }
    case 0x13:
        if(win_x >= 1) {
            win_x -= 1;
            redrawText();
        } else if(win_x > 0) {
            win_x = 0;
            redrawText();
        }
        break;
    case 0x1a:
        memset(&line_cont[cur_y+1], 0x00, virt_height-cur_y-1);
        memset(&char_data[virt_width*cur_y+cur_x], 0x20,
               virt_width*(virt_height-cur_y)-cur_x);
        redrawText();
        break;
    case 0x1c:
        eraseCursor();//if visible
        cur_x++;
        if(cur_x >= virt_width) {
            cur_x = 0;
            cur_y++;
            if(cur_y >= virt_height)
                cur_y = virt_height-1;
        }
        if(cur_x < win_x || win_x + win_width < cur_x - 1 ||
                win_y + win_height < cur_y - 1) {
            if(cur_x < win_x)
                win_x = cur_x;
            if(win_x + win_width < cur_x - 1)
                win_x = cur_x - win_width + 1;
            if(win_y + win_height < cur_y - 1)
                win_y = cur_y - win_height + 1;
            redrawText();
        } else {
            drawCursor();//if visible
        }
        break;
    case 0x1d:
        eraseCursor();//if visible
        if(cur_x < 1) {
            cur_x = virt_width - 1;
            if(cur_y < 1) {
                cur_y = 0;
            } else
                cur_y--;
        } else
            cur_x--;
        if(cur_x < win_x || win_x + win_width < cur_x - 1 ||
                cur_y < win_y) {
            if(cur_x < win_x)
                win_x = cur_x;
            if(win_x + win_width < cur_x - 1)
                win_x = cur_x - win_width + 1;
            if(cur_y < win_y)
                win_y = cur_y;
            redrawText();
        } else {
            drawCursor();//if visible
        }
        break;
    case 0x1e:
        if(cur_y > 0) {
            eraseCursor();//if visible
            cur_y--;
            if(cur_y < win_y) {
                win_y = cur_y;
                redrawText();
            } else
                drawCursor();//if visible
        }
        break;
    case 0x1f:
        if(cur_y < virt_height - 1) {
            eraseCursor();//if visible
            cur_y++;
            if(win_y + win_height < cur_y - 1) {
                win_y = cur_y - win_height + 1;
                redrawText();
            } else
                drawCursor();//if visible
        }
        break;
    default:
        char_data[cur_x+cur_y*virt_width] = ch;
        cur_x++;
        if(cur_x >= virt_width) {
            cur_y++;
            cur_x = 0;
            if(cur_y >= virt_height) {
                cur_y = virt_height - 1;
                memmove(&char_data[0],
                        &char_data[virt_width],
                        virt_width*(virt_height - 1));
                memset(&char_data[virt_width*(virt_height-1)],
                       0x20,
                       virt_width);
            }
            line_cont[cur_y] = 0xff;
        }
        if(cur_x < win_x || win_x + win_width < cur_x - 1 ||
                win_y + win_height < cur_y - 1) {
            if(cur_x < win_x)
                win_x = cur_x;
            if(win_x + win_width < cur_x - 1)
                win_x = cur_x - win_width + 1;
            if(win_y + win_height < cur_y - 1)
                win_y = cur_y - win_height + 1;
        }
        redrawText();
        break;
    }

    if(win_x + win_width > virt_width)
        printf("win_x out of bounds\n");
    if(win_y + win_height > virt_height)
        printf("win_y out of bounds\n");
    if(cur_x >= virt_width)
        printf("cur_x out of bounds\n");
    if(cur_y >= virt_height)
        printf("cur_y out of bounds\n");
}

int HX20CrtDevice::gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                             uint16_t size, uint8_t *buf,
                             HX20SerialConnection *conn) {

    uint8_t b;
    switch(fnc) {
    case 0x88: {
        uint8_t buf[2];
        buf[0] = virt_width-1;
        buf[1] = virt_height-1;
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x89: {
        uint8_t buf[2];
        buf[0] = win_width-1;
        buf[1] = win_height-1;
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x8c: {
        uint8_t buf[2];
        buf[0] = cur_x;
        buf[1] = cur_y;
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x8f: {
        uint8_t b;
        uint16_t x = (buf[0] << 8) | buf[1];
        uint16_t y = (buf[2] << 8) | buf[3];
        if(x < graph_width &&
                y < graph_height)
            b = image_data[x+y*graph_width];
        else
            b = 0;
        return conn->sendPacket(did, sid, fnc, 1, &b);
    }
    case 0x91: {
        uint8_t buf[4];
        buf[1] = cur_y;
        buf[3] = cur_y;
        while(line_cont[buf[1]] && buf[1] > 0)
            buf[1]--;
        while(buf[3] < virt_height-1 && line_cont[buf[3]+1])
            buf[3]++;
        buf[0] = 0;
        buf[2] = virt_width-1;
        printf("0x91: %02x %02x %02x %02x\n",
               buf[0], buf[1], buf[2], buf[3]);
        return conn->sendPacket(did, sid, fnc, 4, buf);
    }
    case 0x92: {
        processCharacter(buf[0]);
        uint8_t buf[2];
        buf[0] = cur_x;
        buf[1] = cur_y;
        printf("0x92: %02x %02x\n",
               buf[0], buf[1]);
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x93: {
        //we ignore the rest, doing all modes at the same time
        background_color = buf[2];
        b = 0;
        return conn->sendPacket(did, sid, fnc, 1, &b);
    }
    case 0x97: {
        printf("sending %d char(s) from %d,%d\n",
               buf[2], buf[0], buf[1]);
        uint8_t *b = &char_data[buf[0]+buf[1]*virt_width];
        return conn->sendPacket(did, sid, fnc, buf[2], b);
    }
    case 0x98: {
        processCharacter(buf[0]);
        uint8_t buf[4];
        buf[0] = cur_x;
        buf[1] = cur_y;
        buf[2] = cur_y;
        buf[3] = cur_y;
        while(line_cont[buf[2]] && buf[2] > 0)
            buf[2]--;
        while(buf[3] < virt_height-1 && line_cont[buf[3]+1])
            buf[3]++;
        printf("0x98: %02x %02x %02x %02x\n",
               buf[0], buf[1], buf[2], buf[3]);
        return conn->sendPacket(did, sid, fnc, 4, buf);
    }
    //hx20 does not want answer if bit 6 is set
    case 0xc2:
        if(cur_x != buf[0] ||
                cur_y != buf[1]) {
            eraseCursor();
            cur_x = buf[0];
            cur_y = buf[1];
            if(cur_x >= virt_width)
                cur_x = virt_width - 1;
            if(cur_y >= virt_height)
                cur_y = virt_height - 1;
            if(((cur_x < win_x || cur_x >= win_x + win_width) &&
                    !list_flag) ||
                    cur_y < win_y || cur_y >= win_y + win_height) {
                win_x = list_flag?0:cur_x;
                win_y = cur_y;
                if(win_x +  win_width > virt_width)
                    win_x = virt_width - win_width;
                if(win_x +  win_width > virt_width)
                    win_x = virt_width - win_width;
                redrawText();
            } else {
                drawCursor();
            }
        }
        return 0;
    case 0xc5:
        list_flag = true;//keep virt_x == 0;
        if(win_x != 0) {
            win_x = 0;
            redrawText();
        }
        return 0;
    case 0xc6:
        list_flag = false;
        return 0;
    case 0xc7: {
        uint16_t x = (buf[0] << 8) | buf[1];
        uint16_t y = (buf[2] << 8) | buf[3];
        if(x < graph_width &&
                y < graph_height) {
            image_data[x+y*graph_width] = buf[4];
            drawable.queue_draw_area(x,y,x+1,y+1);
        }
        return 0;
    }
    case 0xc8: {
        uint16_t x1 = (buf[0] << 8) | buf[1];
        uint16_t y1 = (buf[2] << 8) | buf[3];
        uint16_t x2 = (buf[4] << 8) | buf[5];
        uint16_t y2 = (buf[6] << 8) | buf[7];
        draw_line(image_data, graph_width, graph_height,
                  x1, y1, x2, y2, buf[8]);
        drawable.queue_draw_area(MIN(x1,x2),
                                 MIN(y1,y2),
                                 MAX(x1,x2)+1,
                                 MAX(y1,y2)+1);
        return 0;
    }
    case 0xc9:
        if(buf[0] < virt_height)
            line_cont[buf[0]] = 0;
        return 0;
    case 0xcf:
        printf("select color set %d\n",buf[0]);
        return 0;
    case 0xd4://screen new?
        return 0;
    default:
        printf("unknown function %02x\n", fnc);
        exit(1);
    }
    return 0;
}

static void make_color(guint32 &color, int red, int green, int blue) {
    color = ((red << 8) & 0xff0000) |
            ((green) & 0x00ff00) |
            ((blue >> 8) & 0x0000ff);
}

#include "hx20-default-chars.hpp"

HX20CrtDevice::HX20CrtDevice() :
    virt_width(80),
    virt_height(25),
    win_width(80),
    win_height(25),
    cur_x(0),
    cur_y(0),
    win_x(0),
    win_y(0),
    graph_width(win_width*6),
    graph_height(win_height*8),
    background_color(0),
    list_flag(false) {
    image_data = (uint8_t *)malloc(graph_width*graph_height);
    char_data = (uint8_t *)malloc(virt_width*virt_height);
    line_cont = (uint8_t *)malloc(virt_height);

    memset(image_data, 0, graph_width*graph_height);
    memset(char_data, 0, virt_width*virt_height);
    memset(line_cont, 0, virt_height);

    memcpy(chars,defaultCharacters,sizeof(chars));

    drawable.signal_expose_event().connect(
    sigc::mem_fun(*this, &HX20CrtDevice::expose_event));
    drawable.signal_configure_event().connect(
    sigc::mem_fun(*this, &HX20CrtDevice::configure_event));

    drawable.set_events(Gdk::EXPOSURE_MASK);

    drawable.set_size_request(graph_width, graph_height);

    make_color(colors[0], 0x0000, 0xffff, 0x0000);
    make_color(colors[1], 0xffff, 0xffff, 0x0000);
    make_color(colors[2], 0x0000, 0x0000, 0xffff);
    make_color(colors[3], 0xffff, 0x0000, 0x0000);
    make_color(colors[4], 0xffff, 0xffff, 0xffff);
    make_color(colors[5], 0x0000, 0xffff, 0xffff);
    make_color(colors[6], 0xffff, 0x0000, 0xffff);
    make_color(colors[7], 0xffff, 0x7fff, 0x0000);
}

Gtk::Widget &HX20CrtDevice::getWidget() {
    return drawable;
}
