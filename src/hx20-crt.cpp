
#include <gtkmm.h>
#include <poll.h>

#include "hx20-ser-proto.hpp"
#include "hx20-crt-dev.hpp"
#include "hx20-disk-dev.hpp"

struct Conn {
    HX20SerialConnection *conn;
    struct pollfd *pfd;

    bool
    handle_input(Glib::IOCondition cond) {
        pfd->revents = 0;

        if(cond & Glib::IO_IN)
            pfd->revents |= POLLIN;
        if(cond & Glib::IO_OUT)
            pfd->revents |= POLLOUT;
        if(cond & Glib::IO_PRI)
            pfd->revents |= POLLPRI;
        if(cond & Glib::IO_ERR)
            pfd->revents |= POLLERR;
        if(cond & Glib::IO_HUP)
            pfd->revents |= POLLHUP;
        if(cond & Glib::IO_NVAL)
            pfd->revents |= POLLNVAL;

        if(conn->handleEvents(pfd, 1) < 0) {
            //TODO output a message?
            Gtk::Main::quit();
            return false;
        }
        return true;
    }
};

bool
delete_event(GdkEventAny *event) {
    Gtk::Main::quit();
    return false;
}

int main(int argc, char **argv) {
    Gtk::Main kit(argc, argv);

    /*****************************************************************************/
    /* create a new window */
    Gtk::Window window(Gtk::WINDOW_TOPLEVEL);

    window.signal_delete_event().connect(sigc::ptr_fun(&delete_event));

    window.set_title("");

    HX20CrtDevice crt_dev;
    HX20DiskDevice disk_dev;

    HX20SerialConnection conn("/dev/ttyUSB4");
    conn.registerDevice(&crt_dev);
    conn.registerDevice(&disk_dev);

    int nfds = conn.getNfds();
    struct pollfd *pfd = (struct pollfd *)malloc(nfds * sizeof(struct pollfd));
    conn.fillPollFd(pfd);

    int i;
    for(i = 0; i < nfds; i++) {
        struct Conn *c = new Conn();
        Glib::IOCondition cond = (Glib::IOCondition)0;
        if(pfd[i].events & POLLIN)
            cond |= Glib::IO_IN;
        if(pfd[i].events & POLLOUT)
            cond |= Glib::IO_OUT;
        if(pfd[i].events & POLLPRI)
            cond |= Glib::IO_PRI;
        if(pfd[i].events & POLLERR)
            cond |= Glib::IO_ERR;
        if(pfd[i].events & POLLHUP)
            cond |= Glib::IO_HUP;
        if(pfd[i].events & POLLNVAL)
            cond |= Glib::IO_NVAL;
        c->conn = &conn;
        c->pfd = pfd+i;
        Glib::signal_io().connect(
        sigc::mem_fun(*c, &Conn::handle_input),
        pfd[i].fd,
        cond);
    }

    Gtk::HBox hbox(false, 0);

    hbox.pack_start(crt_dev.getWidget(), true, true, 0);

    window.add(hbox);

    setlocale(LC_NUMERIC, "C");

    window.show_all();

    Gtk::Main::run();

    return 0;
}
