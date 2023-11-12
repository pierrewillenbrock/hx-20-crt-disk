
#include "hx20-crt-dev.hpp"

#include <QTextEdit>
#include <QMainWindow>
#include <QDockWidget>
#include <QPainter>
#include <QMenu>
#include "../../dockwidgettitlebar.hpp"
#include "hx20-crt-dev-gfx-cfg.hpp"
#include "hx20-crt-dev-text-cfg.hpp"
#include "../../settings.hpp"

HX20CrtGraphicsView::HX20CrtGraphicsView(QWidget *parent, Qt::WindowFlags f)
    : QWidget(parent, f), width(128), height(96), zoom(1.0) {
    image = std::make_unique<QImage>(width, height, QImage::Format::Format_RGB32);
    image_data.resize(width * height);
    border_color = qRgb(0,0,0);
    //this is a color map for background colors;
    //depending on the color set, this would use either the first or the
    //second half for pixels.
    color_map[0] = qRgb(0x00, 0xff, 0x00);
    color_map[1] = qRgb(0xff, 0xff, 0x00);
    color_map[2] = qRgb(0x00, 0x00, 0xff);
    color_map[3] = qRgb(0xff, 0x00, 0x00);
    color_map[4] = qRgb(0xff, 0xff, 0xff);
    color_map[5] = qRgb(0x00, 0xff, 0xff);
    color_map[6] = qRgb(0xff, 0x00, 0xff);
    color_map[7] = qRgb(0xff, 0x7f, 0x00);
}

HX20CrtGraphicsView::~HX20CrtGraphicsView() =default;

void HX20CrtGraphicsView::updateImage() {
    if(width != image->width() || height != image->height()) {
        image = std::make_unique<QImage>(width, height, QImage::Format::Format_RGB32);
        if(image_data.size() < (unsigned)(width * height))
            image_data.resize(width * height);
    }
    for(int y = 0; y < height; y++) {
        QRgb *line_ptr = reinterpret_cast<QRgb *>(image->scanLine(y));
        for(int x = 0; x < width; x++) {
            *line_ptr = color_map[image_data[x+y*width]];
            line_ptr++;
        }
    }
    update();
}

void HX20CrtGraphicsView::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.fillRect(rect(), border_color);
    QRect dst = image->rect();
    dst.setWidth(dst.width()*zoom);
    dst.setHeight(dst.height()*zoom);
    dst.moveCenter(rect().center());
    p.drawImage(dst, *image.get());
}

void HX20CrtGraphicsView::resizeEvent(QResizeEvent *event) {
    update();
}

QSize HX20CrtGraphicsView::sizeHint() const {
    return image->size();
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

static char const *char_map[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal
    //note the non breaking space at position 0x20
    " ", "!", "\"", "#",   "$", "%", "&", "´",
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "@", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "[",    "\\", "]", "^", "_",
    "`", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "{",    "|", "}", "~", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

static char const *char_map_fr[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal with the occasional translated characters
    //note the non breaking space at position 0x20
    " ", "!", "\"", "#",   "$", "%", "&", "´",
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "à", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "°",    "ç", "§", "^", "_",
    "`", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "é",    "ù", "è", "¨", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

static char const *char_map_de[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal with the occasional translated characters
    //note the non breaking space at position 0x20
    " ", "!", "\"", "#",   "$", "%", "&", "´",
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "§", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "Ä",    "Ö", "Ü", "^", "_",
    "`", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "ä",    "ö", "ü", "ß", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

static char const *char_map_en[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal with the occasional translated characters
    //note the non breaking space at position 0x20
    " ", "!", "\"", "£",   "$", "%", "&", "´",
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "@", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "[",    "\\", "]", "^", "_",
    "`", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "{",    "³", "}", "~", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

static char const *char_map_dk_intl[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal with the occasional translated characters
    //note the non breaking space at position 0x20
    " ", "!", "\"", "#",   "$", "%", "&", "´",
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "@", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "Æ",    "Ø", "Å", "^", "_",
    "`", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "æ",    "ø", "å", "~", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

static char const *char_map_dk_eurp[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal with the occasional translated characters
    //note the non breaking space at position 0x20
    " ", "!", "\"", "#",   "$", "%", "&", "´",
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "É", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "Æ",    "Ø", "Å", "Ü", "_",
    "é", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "æ",    "ø", "å", "ü", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

static char const *char_map_sv[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal with the occasional translated characters
    //note the non breaking space at position 0x20
    " ", "!", "\"", "#",   "¤", "%", "&", "´",
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "É", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "Ä",    "Ö", "Å", "Ü", "_",
    "é", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "ä",    "ö", "å", "ü", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

static char const *char_map_it[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal with the occasional translated characters
    //note the non breaking space at position 0x20
    " ", "!", "\"", "#",   "$", "%", "&", "´",
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "@", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "°",    "\\", "é", "^", "_",
    "ù", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "à",    "ò", "è", "ì", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

static char const *char_map_es[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal with the occasional translated characters
    //note the non breaking space at position 0x20
    " ", "!", "\"", "#",   "$", "%", "&", "´", //# should be Pt ligature, i think?
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "@", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "í",    "Ñ", "¿", "^", "_",
    "`", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "¨",    "ñ", "}", "~", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

static char const *char_map_no[256] = {
    //these 32 should not appear at all.
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    //these 95 are normal with the occasional translated characters
    //note the non breaking space at position 0x20
    " ", "!", "\"", "#",   "¤", "%", "&", "´",
    "(", ")", "*", "+",    ",", "-", ".", "/",
    "0", "1", "2", "3",    "4", "5", "6", "7",
    "8", "9", ":", ";",    "<", "=", ">", "?",
    "É", "A", "B", "C",    "D", "E", "F", "G",
    "H", "I", "J", "K",    "L", "M", "N", "O",
    "P", "Q", "R", "S",    "T", "U", "V", "W",
    "X", "Y", "Z", "Æ",    "Ø", "Å", "Ü", "_",
    "é", "a", "b", "c",    "d", "e", "f", "g",
    "h", "i", "j", "k",    "l", "m", "n", "o",
    "p", "q", "r", "s",    "t", "u", "v", "w",
    "x", "y", "z", "æ",    "ø", "å", "ü", " ",
    "┼", "┴", "┬", "┤",    "├", "─", "│", "┌",
    "┐", "└", "┘", "▒",    "█", "▄", "▌", "🌑",
    "🌕", "♠", "♥", "♦",    "♣", "♪", "☎", "✈",
    "⛟", "🏆", "⛹", "↑",   "↓", "×", "÷", "±",
    " ", " ", " ",  " ", " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
    " ", " ", " ", " ",  " ", " ", " ", " ",
};

void HX20CrtDevice::redrawText() {
    QString new_text;
    QColor text_color = ((color_set==0)?text_color_1:text_color_2);
    new_text += QString("<span style=\"color: %1; background-color: %2;\">").
                arg(text_color.name()).
                arg(text_background.name());
    for(int y = win_y; y < win_y + win_height; y++) {
        for(int x = win_x; x < win_x + win_width; x++) {
            if(y == cur_y && x == cur_x) {
                new_text += "<u>";
            }
            uint8_t ch = char_data[y*virt_width+x];
            new_text += text_char_map[ch].toHtmlEscaped();
            if(y == cur_y && x == cur_x) {
                new_text += "</u>";
            }
        }
        new_text += "<br>\n";
    }
    new_text += "</span>";
    textview->setText(new_text);
}

bool HX20CrtDevice::windowFollowCursor() {
    /* As far as i can tell, the cursor is allowed to leave the window
     * in list mode. At least the rules change in list mode.
     */
    if(list_flag)
        return false;

    bool res = false;
    if(cur_x < win_x + cursor_margin - 1) {
        if(cur_x > cursor_margin - 1) {
            win_x = cur_x - cursor_margin + 1;
            res = true;
        } else if(win_x != 0) {
            win_x = 0;
            res = true;
        }
    }
    if(win_x - cursor_margin + 1 + win_width < cur_x - 1) {
        if(virt_width > cur_x + cursor_margin - 1 - 1) {
            win_x = cur_x + cursor_margin - 1 - win_width - 1;
            res = true;
        } else if(win_x != virt_width - win_width) {
            win_x = virt_width - win_width;
            res = true;
        }
    }
    if(cur_y < win_y) {
        if(cur_y >= 0) {
            win_y = cur_y;
            res = true;
        }
    }
    if(win_y + win_height - 1 < cur_y) {
        if(cur_y - win_height + 1 <= virt_height) {
            win_y = cur_y - win_height + 1;
            res = true;
        } else if(win_y + win_height != virt_height) {
            win_y = virt_height -  win_height;
            res = true;
        }
    }
    return res;
}

void HX20CrtDevice::processCharacter(uint8_t ch) {
    //see tms15-17.pdf: Technical Manual, Section 2: Software,
    // Chapter 15: Virtual screen, 15.4: Virtual Screen Control
    printf("processing %c(%02x)\n",(char)((ch >= 0x20 && ch < 0x7f)?ch:'?'),(int)ch);
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
        printf("unhandled ctl code %02x\n",(int)ch);
        break;
    case 0x01:
        if(win_x != 0) {
            win_x = 0;
            cur_x = 9;
            redrawText();
        } else if(cur_x != 9) {
            cur_x = 9;
            redrawText();
        }
        break;
    case 0x04:
        if(win_x + horizontal_scroll_step + win_width < virt_width && !list_flag) {
            win_x += horizontal_scroll_step;
            cur_x = win_x + 9;
            windowFollowCursor();
            redrawText();
        } else if(win_x + win_width < virt_width) {
            win_x = virt_width - win_width;
            cur_x = win_x + 9;
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
        windowFollowCursor();
        redrawText();
        break;
    }
    case 0x06:
        if(win_x + win_width != virt_width && !list_flag) {
            win_x = virt_width - win_width;
            cur_x = win_x + 9;
            redrawText();
        } else if(cur_x != win_x + 9 && !list_flag) {
            cur_x = win_x + 9;
            redrawText();
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

        windowFollowCursor();
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
        } else if(new_x >= win_x + win_width && !list_flag) {
            cur_x = new_x;
            win_x = cur_x - win_width-1;
        } else {
            cur_x = new_x;
        }
        windowFollowCursor();
        redrawText();
        break;
    }
    case 0x0a:
        if(cur_y+1 >= virt_height) {
            cur_y = virt_height - 1;
            memmove(&char_data[0],&char_data[virt_width],
                    virt_width*(virt_height-1));
            memmove(&char_data[0],
                    &char_data[virt_width],
                    virt_width*(virt_height - 1));
            memset(&char_data[virt_width*(virt_height-1)],
                   0x20,
                   virt_width);
            memmove(&line_cont[0], &line_cont[1], virt_height-1);
            line_cont[virt_height-1] = 0;
        } else if(cur_y+1 >= win_y + win_height) {
            cur_y++;
            win_y = cur_y - win_height + 1;
        } else {
            cur_y++;
        }
        windowFollowCursor();
        redrawText();
        break;
    case 0x0b:
        if(win_x != 0 || win_y != 0) {
            cur_x = 0;
            cur_y = 0;
            win_x = 0;
            win_y = 0;
            redrawText();
        } else if(cur_x != 0 || cur_y != 0) {
            cur_x = 0;
            cur_y = 0;
            windowFollowCursor();
            redrawText();
        }
        break;
    case 0x0c:
        memset(char_data.data(), 0x20, virt_width*virt_height);
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
        } else {
            cur_x = 0;
        }
        if(cur_y < virt_height)
            line_cont[cur_y+1] = 0;
        redrawText();
        break;
    case 0x10:
        if(win_y >= vertical_scroll_step) {
            win_y -= vertical_scroll_step;
            //cur_x = 9;
            cur_y -= vertical_scroll_step;
            windowFollowCursor();
            redrawText();
        } else if(win_y != 0) {
            cur_y -= win_y;
            win_y = 0;
            //cur_x = 9;
            windowFollowCursor();
            redrawText();
        } /*else if(cur_x != 9) {
            cur_x = 9;
            windowFollowCursor();
            redrawText();
        }*/
        break;
    case 0x11:
        if(win_y + win_height + vertical_scroll_step < virt_height) {
            win_y += vertical_scroll_step;
            cur_y += vertical_scroll_step;
            //cur_x = 9;
            windowFollowCursor();
            redrawText();
        } else if(win_y + win_height != virt_height) {
            cur_y += virt_height - win_height - win_y;
            win_y = virt_height - win_height;
            //cur_x = 9;
            windowFollowCursor();
            redrawText();
        }/* else if(cur_x != 9) {
            cur_x = 9;
            windowFollowCursor();
            redrawText();
        }*/
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
        windowFollowCursor();
        redrawText();
        break;
    }
    case 0x13:
        if(win_x >= horizontal_scroll_step) {
            win_x -= horizontal_scroll_step;
            cur_x = win_x + 9;
            windowFollowCursor();
            redrawText();
        } else if(win_x > 0) {
            win_x = 0;
            cur_x = win_x + 9;
            windowFollowCursor();
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
        cur_x++;
        if(cur_x >= virt_width) {
            cur_x = 0;
            cur_y++;
            if(cur_y >= virt_height)
                cur_y = virt_height-1;
        }
        windowFollowCursor();
        redrawText();
        break;
    case 0x1d:
        if(cur_x < 1) {
            cur_x = virt_width - 1;
            if(cur_y < 1) {
                cur_y = 0;
            } else
                cur_y--;
        } else
            cur_x--;
        windowFollowCursor();
        redrawText();
        break;
    case 0x1e:
        if(cur_y > 0) {
            cur_y--;
            if(cur_y < win_y) {
                win_y = cur_y;
            }
            windowFollowCursor();
            redrawText();
        }
        break;
    case 0x1f:
        if(cur_y < virt_height - 1) {
            cur_y++;
            if(win_y + win_height < cur_y - 1) {
                win_y = cur_y - win_height + 1;
            }
            windowFollowCursor();
            redrawText();
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
                memmove(&line_cont[0], &line_cont[1], virt_height-1);
                line_cont[virt_height-1] = 0;
            }
            line_cont[cur_y] = 0xff;
        }
        windowFollowCursor();
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
                             uint16_t size, uint8_t *inbuf,
                             HX20SerialConnection *conn) {
    //see tms15-17.pdf: Technical Manual, Section 2: Software,
    // Chapter 15: Virtual screen, 15.5: Virtual Screen Function Table
    uint8_t b;
    switch(fnc) {
    case 0x84: {
        //Screen device select
        //Not seen.
        b = 0;
        return conn->sendPacket(did, sid, fnc, 1, &b);
    }
    case 0x85: {
        //Initialization of the Display controller
        //takes and ignores one byte.
        //Not seen.
        b = 0;
        return conn->sendPacket(did, sid, fnc, 1, &b);
    }
    case 0x87: {
        //set character display virtual screen size
        //inbuf[0]: virtual screen width(m-1 for m columns)
        //inbuf[1]: virtual screen length(n-1 for n lines)
        //inbuf[2]: msb of buffer base address(not used with display controller)
        //inbuf[3]: lsb of buffer base address(not used with display controller)
        //Not seen.
        virt_width = inbuf[0];
        virt_height = inbuf[1];
        char_data.resize(virt_width * virt_height);
        line_cont.resize(virt_height);
        if(win_x + win_width > virt_width)
            win_x = virt_width - win_width;
        if(win_y + win_height > virt_height)
            win_y = virt_height - win_height;
        if(cur_x > virt_width)
            cur_x = virt_width-1;
        if(cur_y > virt_height)
            cur_y = virt_height-1;
        b = 0;
        return conn->sendPacket(did, sid, fnc, 1, &b);
    }
    case 0x88: {
        //get character display virtual screen size
        //outbuf[0]: virtual screen width(m-1 for m columns)
        //outbuf[1]: virtual screen length(n-1 for n lines)
        uint8_t buf[2];
        buf[0] = virt_width-1;
        buf[1] = virt_height-1;
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x89: {
        //get character display window size
        //outbuf[0]: window width(m-1 for m columns)
        //outbuf[1]: window length(n-1 for n lines)
        uint8_t buf[2];
        buf[0] = win_width-1;
        buf[1] = win_height-1;
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x8a: {
        //get character display window position
        //outbuf[0]: x
        //outbuf[1]: y
        //Not seen.
        uint8_t buf[2];
        buf[0] = win_x;
        buf[1] = win_y;
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x8c: {
        //get character display cursor position
        //outbuf[0]: x
        //outbuf[1]: y
        uint8_t buf[2];
        buf[0] = cur_x;
        buf[1] = cur_y;
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x8d: {
        //get character display cursor margin value
        //outbuf[0]: margin value (number of characters the cursor should be away from the window left/right border, 1 is at the border)
        //Not seen.
        uint8_t buf[1];
        buf[0] = cursor_margin;
        return conn->sendPacket(did, sid, fnc, 1, buf);
    }
    case 0x8e: {
        //get character display scroll steps
        //outbuf[0]: number of horizontal scroll steps
        //outbuf[1]: number of vertical scroll steps
        //Not seen.
        uint8_t buf[2];
        buf[0] = horizontal_scroll_step;
        buf[1] = vertical_scroll_step;
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x8f: {
        //get graphics pixel data at x,y
        //inbuf[0]: msb of x
        //inbuf[1]: lsb of x
        //inbuf[2]: msb of y
        //inbuf[3]: lsb of y
        //outbuf[0]: display controller color code (depending on mode, 0-1(black-green/white), 0-3(green/white,yellow/cyan,blue/magenta,red/orange) (css-off/css-on). we can go beyond that.
        uint8_t b;
        uint16_t x = (inbuf[0] << 8) | inbuf[1];
        uint16_t y = (inbuf[2] << 8) | inbuf[3];
        if(x < graph_width &&
                y < graph_height)
            b = graphicsview->image_data[x+y*graph_width];
        else
            b = 0;
        return conn->sendPacket(did, sid, fnc, 1, &b);
    }
    case 0x91: {
        //get the range of the current character display logical line at the cursor
        //outbuf[0]: first column in the logical single line: 0
        //outbuf[1]: first line in the logical single line
        //outbuf[2]: physical screen width -1
        // probably: last column in the logical single line: window width-1
        //outbuf[3]: last line in the logical single line
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
        //display one one character on virtual screen
        //inbuf[0]: character code
        //outbuf[0]: new cursor x
        //outbuf[1]: new cursor y
        processCharacter(inbuf[0]);
        uint8_t buf[2];
        buf[0] = cur_x;
        buf[1] = cur_y;
        printf("0x92: %02x %02x\n",
               buf[0], buf[1]);
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x93: {
        //set display mode for the display controller
        //inbuf[0]: text mode: 0: graphics mode, 1: text mode (we ignore this)
        //inbuf[1]: graphic mode: 0: text mode, 1: color graphic mode, 2: monochromatic graphic mode
        //inbuf[2]: background color: 0: green, 1: yellow, 2: blue, 3: red, 4: white, 5: cyan, 6: magenta, 7: orange
        //we ignore the rest, doing all modes at the same time
        background_color = inbuf[2];
        b = 0;
        return conn->sendPacket(did, sid, fnc, 1, &b);
    }
    case 0x95: {
        //get one character on character display
        //reads the character at the "access pointer"
        //outbuf[0]: character code
        //outbuf[1]: color code(background color code)
        uint8_t buf[2];
        if(access_x+access_y*virt_width >= 0 &&
                (unsigned)(access_x+access_y*virt_width) < char_data.size())
            buf[0] = char_data[access_x+access_y*virt_width];
        else
            buf[0] = 0;
        buf[1] = background_color;
        return conn->sendPacket(did, sid, fnc, 2, buf);
    }
    case 0x97: {
        //get character display content
        //inbuf[0]: read start x coordinate
        //inbuf[1]: read start y coordinate
        //inbuf[2]: number of characters to read
        //outbuf[...]: character codes
        printf("sending %d char(s) from %d,%d\n",
               inbuf[2], inbuf[0], inbuf[1]);
        uint8_t *b = &char_data[inbuf[0]+inbuf[1]*virt_width];
        return conn->sendPacket(did, sid, fnc, inbuf[2], b);
    }
    case 0x98: {
        //display one character on virtual screen
        //inbuf[0]: character code
        //outbuf[0]: new cursor x
        //outbuf[1]: new cursor y
        //outbuf[2]: first line number in the logical single line containing the new cursor
        //outbuf[3]: last line number in the logical single line containing the new cursor
        processCharacter(inbuf[0]);
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
    case 0xc0: {
        //set the character display window position
        //inbuf[0]: coordinate x on the virtual screen
        //inbuf[1]: coordinate y on the virtual screen
        //Note: If the window position is outside the bounds of the virtual
        //      screen, the maximum values are set for both coordinates x and y.
        //Not seen
        win_x = inbuf[0];
        win_y = inbuf[1];
        if(win_x +  win_width > virt_width)
            win_x = virt_width - win_width;
        if(win_x +  win_width > virt_width)
            win_x = virt_width - win_width;
        windowFollowCursor();
        redrawText();
        return 0;
    }
    case 0xc2:
        //set cursor position
        //inbuf[0]: coordinate x on the virtual screen
        //inbuf[1]: coordinate y on the virtual screen
        //Note: The window position is controlled as follows:
        //  (1) The window does not move when the specified cursor is within
        //      the window area.
        //  (2) When the specified cursor position is not within the window
        //      area, the windows moves so that the new cursor is located at
        //      the home position of the window. The cursor position can not be
        //      located at the home position of the window, if the bottom edge
        //      of the window is in alignment with the bottom edge of the
        //      virtual screen. In such case, the cursor position is set within
        //      the window area according to the same rule as that of function
        //      code 0xc0.
        if(cur_x != inbuf[0] ||
                cur_y != inbuf[1]) {
            cur_x = inbuf[0];
            cur_y = inbuf[1];
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
            }
            redrawText();
        }
        return 0;
    case 0xc3: {
        //set the value of the character display cursor margin
        //inbuf[0]: cursor margin value (between 1 and half the window width; 1 is no margin)
        //this limits the cursor position inside the window to always be cursor
        //margin-1 characters away from the left/right edge, except if that
        //would move the window outside the virtual screen.
        //Not seen
        cursor_margin = inbuf[0];
        if(windowFollowCursor())
            redrawText();
        return 0;
    }
    case 0xc4: {
        //set the character display scrolling steps
        //inbuf[0]: number of horizontal scroll steps (0..255)
        //inbuf[1]: number of vertical scroll steps (0..255)
        horizontal_scroll_step = inbuf[0];
        vertical_scroll_step = inbuf[1];
        //Not seen
        return 0;
    }
    case 0xc5:
        //turn the list flag on
        //no inputs
        list_flag = true;//keep virt_x == 0;
        if(win_x != 0) {
            win_x = 0;
            redrawText();
        }
        return 0;
    case 0xc6:
        //turn the list flag off
        //no inputs
        list_flag = false;
        if(windowFollowCursor())
            redrawText();
        return 0;
    case 0xc7: {
        //set graphics display pixel at position
        //inbuf[0]: msb of coordinate x
        //inbuf[1]: lsb of coordinate x
        //inbuf[2]: msb of coordinate y
        //inbuf[3]: lsb of coordinate y
        //inbuf[4]: color code (depending on mode, 0-1(black-green/white), 0-3(green/white,yellow/cyan,blue/magenta,red/orange) (css-off/css-on). we can go beyond that.
        uint16_t x = (inbuf[0] << 8) | inbuf[1];
        uint16_t y = (inbuf[2] << 8) | inbuf[3];
        if(x < graph_width &&
                y < graph_height) {
            graphicsview->image_data[x+y*graph_width] = inbuf[4];
            graphicsview->updateImage();
        }
        return 0;
    }
    case 0xc8: {
        //draw graphics display line
        //inbuf[0]: msb of start coordinate x
        //inbuf[1]: lsb of start coordinate x
        //inbuf[2]: msb of start coordinate y
        //inbuf[3]: lsb of start coordinate y
        //inbuf[4]: msb of end coordinate x
        //inbuf[5]: lsb of end coordinate x
        //inbuf[6]: msb of end coordinate y
        //inbuf[7]: lsb of end coordinate y
        //inbuf[8]: color code (depending on mode, 0-1(black-green/white), 0-3(green/white,yellow/cyan,blue/magenta,red/orange) (css-off/css-on). we can go beyond that.
        uint16_t x1 = (inbuf[0] << 8) | inbuf[1];
        uint16_t y1 = (inbuf[2] << 8) | inbuf[3];
        uint16_t x2 = (inbuf[4] << 8) | inbuf[5];
        uint16_t y2 = (inbuf[6] << 8) | inbuf[7];
        draw_line(graphicsview->image_data.data(), graph_width, graph_height,
                  x1, y1, x2, y2, inbuf[8]);
        graphicsview->updateImage();
        return 0;
    }
    case 0xc9:
        //termination of the logical single line on character display
        //inbuf[0]: line number
        if(inbuf[0] < virt_height)
            line_cont[inbuf[0]] = 0;
        return 0;
    case 0xca: {
        //clear graphics display screen
        //inbuf[0]: background color: 0: green, 1: yellow, 2: blue, 3: red, 4: white, 5: cyan, 6: magenta, 7: orange
        //Not seen
        return 0;
    }
    case 0xcb: {
        //set scrolling speed
        //inbuf[0]: speed; 0-9, 9 is fastest.
        //Not seen; not used with the display controller
        return 0;
    }
    case 0xcd: {
        //output of one character at the position of the "access pointer"
        //inbuf[0]: character code
        //Not seen
        if(access_x+access_y*virt_width >= 0 &&
                (unsigned)(access_x+access_y*virt_width) < char_data.size())
            char_data[access_x+access_y*virt_width] = inbuf[0];
        redrawText();
        return 0;
    }
    case 0xce: {
        //set access pointer
        //inbuf[0]: coordinate x of access pointer
        //inbuf[1]: coordinate y of access pointer
        access_x = inbuf[0];
        access_y = inbuf[1];
        if(access_x >= virt_width)
            access_x = virt_width-1;
        if(access_y >= virt_height)
            access_y = virt_height-1;
        return 0;
    }
    case 0xcf:
        //set color set
        //inbuf[0]: color set; 0: green, yellow, blue, red
        //                     1: white, cyan, magenta, orange
        printf("select color set %d\n",inbuf[0]);
        color_set = inbuf[0];
        redrawText();
        updateGraphicsColors();
        return 0;
    case 0xd4://screen new?
        return 0;
    default:
        break;
    }
    printf("HX20CrtDevice: unknown function %02x\n", fnc);
    exit(1);
}

void HX20CrtDevice::updateGraphicsColors() {
    int preset_num = (color_set == 0)?
                     settingsConfig->value("gfx/colorset1", 0).toInt():
                     settingsConfig->value("gfx/colorset2", 1).toInt();

    if(preset_num < 0 || preset_num >= settingsPresets->arraySize("gfx/colorsets"))
        return;
    Settings::Group *set =
    settingsPresets->array("gfx/colorsets",
                           preset_num);
    for(int i = 0; i < set->value("size").toInt(); i++) {
        QString idText = QString("%1").arg(i);
        graphicsview->color_map[i] =
        set->value(idText, QColor(Qt::black)).value<QColor>().rgb();
    }

    graphicsview->updateImage();
}

HX20CrtDevice::HX20CrtDevice() :
    virt_width(80),
    virt_height(25),
    win_width(32),
    win_height(16),
    cur_x(0),
    cur_y(0),
    win_x(0),
    win_y(0),
    access_x(0),
    access_y(0),
    color_set(0),
    graph_width(640),
    graph_height(480),
    text_color_1(Qt::green),
    text_color_2("orange"),
    text_background(Qt::black),
    text_border(Qt::black),

    background_color(0),
    cursor_margin(4),//meaning no margin
    horizontal_scroll_step(16),
    vertical_scroll_step(16),
    list_flag(false),
    settingsConfig(nullptr),
    settingsPresets(nullptr) {

    graphicsview = new HX20CrtGraphicsView();
    textview = new QTextEdit();

    graphicsview->width = graph_width;
    graphicsview->height = graph_height;
    graphicsview->updateImage();

    textview->setReadOnly(false);
    QFont textfont = textview->font();
    textfont.setFamilies({"Courier", "Mono"});
    textfont.setFixedPitch(true);
    textview->setFont(textfont);
    textview->setTextInteractionFlags(Qt::TextSelectableByMouse);

    char_data.resize(virt_width*virt_height);
    line_cont.resize(virt_height);

    memset(graphicsview->image_data.data(), 0, graph_width*graph_height);
    memset(char_data.data(), 0x20, virt_width*virt_height);
    memset(line_cont.data(), 0, virt_height);

    for(int i = 0; i < 256; i++) {
        text_char_map[i] = QString::fromUtf8(char_map_de[i]);
    }
    for(int i = 0; i < 128-32+32 && i/16*virt_width+(i%16) < (int)char_data.size(); i++) {
        int x = i % 16;
        int y = i / 16;
        char_data[y*virt_width+x] = i+32;
    }
    redrawText();
}

HX20CrtDevice::~HX20CrtDevice() =default;

void HX20CrtDevice::addDocksToMainWindow(QMainWindow *window,
        QMenu *devices_menu) {
    QDockWidget *d1 = new QDockWidget(window);
    d1->setObjectName("graphicsdock");
    d1->setWindowTitle("Graphics");
    d1->setWidget(graphicsview);
    DockWidgetTitleBar *titleBar1 = new DockWidgetTitleBar();
    connect(titleBar1, &DockWidgetTitleBar::configure,
    [this, window]() {
        assert(this->settingsConfig);
        assert(this->settingsPresets);
        auto cfg = std::make_unique<HX20CrtDeviceGfxCfg>(this->settingsConfig->group("gfx"),
                   this->settingsPresets->group("gfx"), window);
        if(cfg->exec() == QDialog::Accepted) {
        }
    });
    d1->setTitleBarWidget(titleBar1);
    window->addDockWidget(Qt::LeftDockWidgetArea, d1);

    connect(devices_menu->addAction(tr("Graphics")),
            &QAction::triggered,
    this,[dock=d1]() {
        dock->show();
    });

    QDockWidget *d2 = new QDockWidget(window);
    d2->setObjectName("textdock");
    d2->setWindowTitle("Text");
    d2->setWidget(textview);
    DockWidgetTitleBar *titleBar2 = new DockWidgetTitleBar();
    connect(titleBar2, &DockWidgetTitleBar::configure,
    [this, window]() {
        assert(this->settingsConfig);
        assert(this->settingsPresets);
        auto cfg = std::make_unique<HX20CrtDeviceTextCfg>(this->settingsConfig->group("text"),
                   this->settingsPresets->group("text"), window);
        if(cfg->exec() == QDialog::Accepted) {
        }
    });
    d2->setTitleBarWidget(titleBar2);
    window->addDockWidget(Qt::LeftDockWidgetArea, d2);

    connect(devices_menu->addAction(tr("Text")),
            &QAction::triggered,
    this,[dock=d2]() {
        dock->show();
    });

}

void initBuiltinCharset(Settings::Group *group, const char **char_map,
                        QString const &name, QString const &code) {
    //Find or create our group.
    Settings::Group *set = nullptr;
    for(int i = 0; i < group->arraySize("charsets"); i++) {
        if(group->array("charsets", i)->value("name").toString() == name &&
                group->array("charsets", i)->value("builtin").toString() == code) {
            set = group->array("charsets", i);
            break;
        }
    }
    if(!set) {
        set = group->array("charsets", group->arraySize("charsets"));
        set->setValue("name", name);
        set->setValue("builtin", code);
    }
    for(int i = 0; i < 256; i++) {
        QString tgt = QString::fromUtf8(char_map[i]);
        if(set->value(QString("%1").arg(i)).toString() != tgt)
            set->setValue(QString("%1").arg(i), tgt);
    }
}

void initBuiltinColorset(Settings::Group *group, const QRgb *color_map, size_t size,
                         QString const &name, QString const &code) {
    //Find or create our group.
    Settings::Group *set = nullptr;
    for(int i = 0; i < group->arraySize("colorsets"); i++) {
        if(group->array("colorsets", i)->value("name").toString() == name &&
                group->array("colorsets", i)->value("builtin").toString() == code) {
            set = group->array("colorsets", i);
            break;
        }
    }
    if(!set) {
        set = group->array("colorsets", group->arraySize("colorsets"));
        set->setValue("name", name);
        set->setValue("builtin", code);
    }
    if(set->value("size").toInt() != (int)size)
        set->setValue("size", QVariant((int)size));
    for(int i = 0; i < (int)size; i++) {
        QColor tgt(color_map[i]);
        if(set->value(QString("%1").arg(i)).value<QColor>() != tgt)
            set->setValue(QString("%1").arg(i), tgt);
    }
}

void HX20CrtDevice::setSettings(Settings::Group *settingsConfig,
                                Settings::Group *settingsPresets) {
    if(this->settingsConfig == settingsConfig &&
            this->settingsPresets == settingsPresets)
        return;
    if(this->settingsConfig) {
        disconnect(this->settingsConfig, &Settings::Group::changedChildKey,
                   this, &HX20CrtDevice::updateFromConfig);
    }
    if(this->settingsPresets) {
        disconnect(this->settingsPresets, &Settings::Group::changedChildKey,
                   this, &HX20CrtDevice::updateFromConfig);
    }
    this->settingsConfig = settingsConfig;
    this->settingsPresets = settingsPresets;

    initBuiltinCharset(settingsPresets->group("text"), char_map, "Default/USA(builtin)","default");
    initBuiltinCharset(settingsPresets->group("text"), char_map_fr, "France(builtin)","fr");
    initBuiltinCharset(settingsPresets->group("text"), char_map_de, "Germany(builtin)","de");
    initBuiltinCharset(settingsPresets->group("text"), char_map_en, "England(builtin)","en");
    initBuiltinCharset(settingsPresets->group("text"), char_map_dk_intl, "Denmark(ASCII; builtin)","dki");
    initBuiltinCharset(settingsPresets->group("text"), char_map_sv, "Sweden(builtin)","sv");
    initBuiltinCharset(settingsPresets->group("text"), char_map_it, "Italy(builtin)","it");
    initBuiltinCharset(settingsPresets->group("text"), char_map_es, "Spain(builtin)","es");
    initBuiltinCharset(settingsPresets->group("text"), char_map_no, "Norway(builtin)","no");
    initBuiltinCharset(settingsPresets->group("text"), char_map_dk_eurp, "Denmark(european; builtin)","dke");

    static QRgb const colors[] = {
        qRgb(0x00, 0xff, 0x00),
        qRgb(0xff, 0xff, 0x00),
        qRgb(0x00, 0x00, 0xff),
        qRgb(0xff, 0x00, 0x00),
        qRgb(0xff, 0xff, 0xff),
        qRgb(0x00, 0xff, 0xff),
        qRgb(0xff, 0x00, 0xff),
        qRgb(0xff, 0x7f, 0x00),
        qRgb(0x00, 0x00, 0x00),
    };

    initBuiltinColorset(settingsPresets->group("gfx"), colors+0, 4,
                        "Color Set 1(builtin)", "set1");
    initBuiltinColorset(settingsPresets->group("gfx"), colors+4, 4,
                        "Color Set 2(builtin)", "set2");
    initBuiltinColorset(settingsPresets->group("gfx"), colors+0, 9,
                        "Border Set(builtin)", "borders");


    if(this->settingsConfig) {
        connect(this->settingsConfig, &Settings::Group::changedChildKey,
                this, &HX20CrtDevice::updateFromConfig);
    }
    if(this->settingsPresets) {
        connect(this->settingsPresets, &Settings::Group::changedChildKey,
                this, &HX20CrtDevice::updateFromConfig);
    }
    updateFromConfig();
}

void HX20CrtDevice::updateFromConfig() {
    virt_width = settingsConfig->value("text/virtualSizeX", virt_width, true).toInt();
    virt_height = settingsConfig->value("text/virtualSizeY", virt_height, true).toInt();
    win_width = settingsConfig->value("text/windowSizeX", win_width, true).toInt();
    win_height = settingsConfig->value("text/windowSizeY", win_height, true).toInt();
    text_color_1 = settingsConfig->value("text/color1", text_color_1, true).value<QColor>();
    text_color_2 = settingsConfig->value("text/color2", text_color_2, true).value<QColor>();
    text_background = settingsConfig->value("text/background", text_background, true).value<QColor>();
    text_border = settingsConfig->value("text/border", text_border, true).value<QColor>();

    char_data.resize(virt_width * virt_height);
    line_cont.resize(virt_height);
    if(win_x + win_width > virt_width)
        win_x = virt_width - win_width;
    if(win_y + win_height > virt_height)
        win_y = virt_height - win_height;
    if(cur_x > virt_width)
        cur_x = virt_width-1;
    if(cur_y > virt_height)
        cur_y = virt_height-1;

    QPalette pal = textview->palette();
    pal.setColor(QPalette::ColorRole::Base, text_border);
    textview->setPalette(pal);

    if(settingsPresets->arraySize("text/charsets") >
            settingsConfig->value("text/charset", 2, true).toInt()) {
        Settings::Group *set = settingsPresets->array("text/charsets",
                               settingsConfig->value("text/charset").toInt());
        for(int i = 0; i < 256; i++) {
            text_char_map[i] = set->value(QString("%1").arg(i)).toString();
        }
    }

    redrawText();

    graph_width = settingsConfig->value("gfx/sizeX", 640, true).toInt();
    graph_height = settingsConfig->value("gfx/sizeY", 480, true).toInt();

    graphicsview->width = graph_width;
    graphicsview->height = graph_height;

    if(settingsPresets->arraySize("gfx/colorsets") >
            settingsConfig->value("gfx/bordercolorset", 2, true).toInt()) {
        Settings::Group *colorset =
        settingsPresets->array("gfx/colorsets",
                               settingsConfig->value("gfx/bordercolorset", 2, true).toInt());
        QString idText = QString("%1").arg(settingsConfig->value("gfx/bordercolor", 8, true).toInt());
        graphicsview->border_color =
        colorset->value(idText, QColor(Qt::black)).value<QColor>().rgb();
    } else {
        graphicsview->border_color = QColor(Qt::black).rgb();
    }

    updateGraphicsColors();
}
