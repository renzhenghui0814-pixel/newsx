#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QScreen>
#include <QTimer>
#include <QElapsedTimer>
#include <QtQml/qqmlregistration.h>

namespace newsx {
class EdgeHideController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QWindow* hostWindow READ window WRITE set_window NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE set_enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool blocked READ blocked WRITE set_blocked NOTIFY blockedChanged)
    Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged)
    Q_PROPERTY(bool animating READ animating NOTIFY animatingChanged)
    Q_PROPERTY(int dockEdge READ dock_edge NOTIFY dockEdgeChanged)
public:
    explicit EdgeHideController(QObject* parent = nullptr);
    QWindow* window() const { return window_; }
    bool enabled() const { return enabled_; }
    bool blocked() const { return blocked_; }
    bool hidden() const { return hidden_; }
    bool animating() const { return animating_; }
    int dock_edge() const { return edge_; }
    void set_window(QWindow* window);
    void set_enabled(bool value);
    void set_blocked(bool value);
    Q_INVOKABLE QRect savedGeometry() const;
    Q_INVOKABLE void reveal();
signals:
    void windowChanged();
    void enabledChanged();
    void blockedChanged();
    void hiddenChanged();
    void animatingChanged();
    void dockEdgeChanged();
    void settled();
private:
    QPointer<QWindow> window_;
    QPointer<QScreen> screen_;
    QTimer timer_;
    QElapsedTimer clock_, outside_;
    QRect area_;
    QPoint applied_;
    double position_ = 0, velocity_ = 0, target_ = 0;
    int expanded_x_ = 0, expanded_y_ = 0;
    bool enabled_ = false, blocked_ = false, hidden_ = false, animating_ = false;
    Qt::Edge edge_ = Qt::RightEdge;
    bool vertical_edge() const { return edge_ == Qt::TopEdge || edge_ == Qt::BottomEdge; }
    double axis_position() const;
    double hidden_target() const;
    QRect trigger_area() const;
    void tick();
    void prepare_dock();
    void move_to(double target);
    void animate(double seconds);
    void set_hidden(bool value);
    void set_animating(bool value);
    void clip_to_screen();
    bool owned_popup_open() const;
    bool pointer_over_panel() const;
};
}
