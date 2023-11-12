
#include "dockwidgettitlebar.hpp"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QStyle>
#include <QAction>
#include <QPainter>
#include <QStyleOption>
#include <QTextDocument>
#include <QTextFrameFormat>
#include <QTextFrame>
#include <QAbstractTextDocumentLayout>
#include <QStylePainter>
#include <QApplication>

DockWidgetTitleBar::DockWidgetTitleBar(QWidget *parent, Qt::WindowFlags f)
    : QWidget(parent, f), m_lastDockWidget(nullptr),
      m_configureButton(nullptr), m_floatButton(nullptr), m_closeButton(nullptr),
      m_layoutStatus(NoLayout) {
    m_configureButton = new QToolButton();
    m_configureButton->setAutoRaise(true);
    //menuButton->setStyleSheet(QString("border: none;"));
    QIcon icon = QIcon::fromTheme("configure");
    m_configureButton->setIcon(icon);
    connect(m_configureButton, &QToolButton::clicked,
            this, &DockWidgetTitleBar::configure);
    m_floatButton = new QToolButton();
    m_floatButton->setAutoRaise(true);
    m_floatButton->setStyleSheet(QString("border: none;"));
    icon = style()->standardIcon(QStyle::SP_TitleBarNormalButton, 0, this);
    m_floatButton->setIcon(icon);
    m_closeButton = new QToolButton();
    m_closeButton->setAutoRaise(true);
    m_closeButton->setStyleSheet(QString("border: none;"));
    icon = style()->standardIcon(QStyle::SP_TitleBarCloseButton, 0, this);
    m_closeButton->setIcon(icon);
}

void DockWidgetTitleBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);

    QDockWidget *dockWidget = qobject_cast<QDockWidget *>(parentWidget());
    if(!dockWidget)
        return;


    QStylePainter p(this);

    if(dockWidget->isFloating()) {
        QStyleOptionFrame framOpt;
        framOpt.init(
        this);
        p.drawPrimitive(QStyle::PE_FrameDockWidget,
                        framOpt);
    }
    // Title must be painted after the frame, since the areas overlap, and
    // the title may wish to extend out to all sides (eg. Vista style)
    QStyleOptionDockWidget titleOpt;

    {
        // If we are in a floating tab, init from the parent because the attributes and the geometry
        // of the title bar should be taken from the floating window.
        titleOpt.initFrom(dockWidget);
        titleOpt.rect = contentsRect();
        titleOpt.title = dockWidget->windowTitle();
        titleOpt.closable = (dockWidget->features() & QDockWidget::DockWidgetClosable) != 0;
        titleOpt.movable = (dockWidget->features() & QDockWidget::DockWidgetMovable) != 0;
        titleOpt.floatable = (dockWidget->features() & QDockWidget::DockWidgetFloatable) != 0;
        titleOpt.verticalTitleBar = (dockWidget->features() & QDockWidget::DockWidgetVerticalTitleBar) != 0;
    }
    p.setFont(QApplication::font("QDockWidgetTitle"));
    titleOpt.fontMetrics = QFontMetrics(p.font());
    if(titleOpt.verticalTitleBar) {
        titleOpt.rect.adjust(0,0,0,-m_configureButton->height());
        titleOpt.title = titleOpt.fontMetrics.elidedText(titleOpt.title, Qt::TextElideMode::ElideMiddle, m_configureButton->y()-titleOpt.fontMetrics.descent()-8);
    } else {
        titleOpt.rect.adjust(0,0,-m_configureButton->width(),0);
        titleOpt.title = titleOpt.fontMetrics.elidedText(titleOpt.title, Qt::TextElideMode::ElideMiddle, m_configureButton->x()-titleOpt.fontMetrics.descent()-8);
    }
    p.drawControl(QStyle::CE_DockWidgetTitle, titleOpt);
}

void DockWidgetTitleBar::changeEvent(QEvent *event) {
    relayout();
}

void DockWidgetTitleBar::resizeEvent(QResizeEvent *event) {
    relayout();
}

QSize DockWidgetTitleBar::sizeHint() const {
    ensurePolished();

    QDockWidget *dockWidget = qobject_cast<QDockWidget *>(parentWidget());

    if(!dockWidget)
        return QSize(0, 0);

    QFontMetrics titleFontMetrics = fontMetrics();

    int mw = style()->pixelMetric(QStyle::PM_DockWidgetTitleMargin, nullptr,
                                  this);
    int fw = style()->pixelMetric(QStyle::PM_DockWidgetFrameWidth, nullptr,
                                  this);

    int titleWidth = titleFontMetrics.boundingRect(dockWidget->windowTitle()).width();
    if(dockWidget->features() & QDockWidget::DockWidgetVerticalTitleBar) {
        return QSize(
               qMax(QWidget::sizeHint().width(), titleFontMetrics.height() + 2*mw),
               QWidget::sizeHint().height()
               + titleWidth + 2*fw + 3*mw);
    } else {
        return QSize(
               QWidget::sizeHint().width() + titleWidth + 2*fw + 3*mw,
               qMax(QWidget::sizeHint().height(), titleFontMetrics.height() + 2*mw));
    }
}

QSize DockWidgetTitleBar::minimumSizeHint() const {
    ensurePolished();

    QDockWidget *dockWidget = qobject_cast<QDockWidget *>(parentWidget());

    if(!dockWidget)
        return QSize(0, 0);

    int mw = style()->pixelMetric(QStyle::PM_DockWidgetTitleMargin, nullptr,
                                  this);
    int fw = style()->pixelMetric(QStyle::PM_DockWidgetFrameWidth, nullptr,
                                  this);
    if(dockWidget->features() & QDockWidget::DockWidgetVerticalTitleBar) {
        int titleHeight = this->sizeHint().width();
        return QSize(
               QWidget::minimumSizeHint().width(),
               QWidget::minimumSizeHint().height()
               + titleHeight + 2*fw + 3*mw);
    } else {
        int titleHeight = this->sizeHint().height();
        return QSize(
               QWidget::minimumSizeHint().width()
               + titleHeight + 2*fw + 3*mw,
               QWidget::minimumSizeHint().height());
    }
}

void DockWidgetTitleBar::relayout() {
    QDockWidget *dockWidget = qobject_cast<QDockWidget *>(parentWidget());

    if(m_lastDockWidget != dockWidget) {
        if(m_lastDockWidget) {
            //disconnect our connections
            disconnect(m_floatConnection);
            disconnect(m_closeConnection);
        }
        m_layoutStatus = NoLayout;
        m_lastDockWidget = dockWidget;
    }

    if(dockWidget) {
        if((dockWidget->features() & QDockWidget::DockWidgetFloatable) &&
                !m_floatConnection) {
            m_floatConnection = connect(m_floatButton, &QToolButton::clicked,
            dockWidget, [dockWidget]() {
                dockWidget->setFloating(!dockWidget->isFloating());
            });
        }
        if(!(dockWidget->features() & QDockWidget::DockWidgetFloatable) &&
                m_floatConnection) {
            disconnect(m_floatConnection);
        }
        if((dockWidget->features() & QDockWidget::DockWidgetClosable) &&
                !m_closeConnection) {
            m_closeConnection = connect(m_closeButton, &QToolButton::clicked,
                                        dockWidget, &QDockWidget::close);
        }
        if(!(dockWidget->features() & QDockWidget::DockWidgetClosable) &&
                m_closeConnection) {
            disconnect(m_closeConnection);
        }

        if(((dockWidget->features() & QDockWidget::DockWidgetVerticalTitleBar)?VerticalLayout:HorizontalLayout) != m_layoutStatus) {
            m_layoutStatus = ((dockWidget->features() & QDockWidget::DockWidgetVerticalTitleBar)?VerticalLayout:HorizontalLayout);
            if(dockWidget->features() & QDockWidget::DockWidgetVerticalTitleBar) {
                QVBoxLayout *layout = new QVBoxLayout();
                setLayout(layout);

                layout->addStretch();
                layout->addWidget(m_configureButton);
                if(dockWidget->features() & QDockWidget::DockWidgetFloatable) {
                    layout->addWidget(m_floatButton);
                }
                if(dockWidget->features() & QDockWidget::DockWidgetClosable) {
                    layout->addWidget(m_closeButton);
                }
            } else {
                QHBoxLayout *layout = new QHBoxLayout();
                setLayout(layout);

                layout->addStretch();
                layout->addWidget(m_configureButton);
                if(dockWidget->features() & QDockWidget::DockWidgetFloatable) {
                    layout->addWidget(m_floatButton);
                }
                if(dockWidget->features() & QDockWidget::DockWidgetClosable) {
                    layout->addWidget(m_closeButton);
                }
                layout->setSpacing(0);
            }
        }
    }
}


