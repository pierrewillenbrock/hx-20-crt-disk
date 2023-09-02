
#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE

class QDockWidget;
class QLabel;
class QToolButton;

QT_END_NAMESPACE

class VerticalLabel;

class DockWidgetTitleBar : public QWidget {
    Q_OBJECT
public:
    DockWidgetTitleBar(QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    QToolButton *configureButton() const { return m_configureButton; }
protected:
    virtual void changeEvent(QEvent *event) override;
    virtual void resizeEvent(QResizeEvent *event) override;
    virtual void paintEvent(QPaintEvent* event) override;
private:
    QDockWidget *m_lastDockWidget;
    QMetaObject::Connection m_floatConnection;
    QMetaObject::Connection m_closeConnection;
    QToolButton *m_configureButton;
    QToolButton *m_floatButton;
    QToolButton *m_closeButton;
    enum LayoutStatus {
        NoLayout, HorizontalLayout, VerticalLayout
    };
    LayoutStatus m_layoutStatus;
    void relayout();
signals:
    void configure();
};

