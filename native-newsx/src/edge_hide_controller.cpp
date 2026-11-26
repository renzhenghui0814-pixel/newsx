#include "edge_hide_controller.h"
#include <QGuiApplication>
#include <QCursor>
#include <QRegion>
#include <algorithm>
#include <array>
#include <cmath>
#include <windows.h>

namespace newsx {
namespace { constexpr int edge_width = 5; }

EdgeHideController::EdgeHideController(QObject* parent) : QObject(parent) {
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(50);
    connect(&timer_, &QTimer::timeout, this, &EdgeHideController::tick);
}
void EdgeHideController::set_window(QWindow* value) {
    if (window_ == value) return;
    if (window_) {
        window_->setMask(QRegion());
        disconnect(window_, nullptr, this, nullptr);
    }
    window_ = value;
    if (window_) {
        position_ = target_ = axis_position();
        applied_ = window_->position();
        expanded_x_ = window_->x(); expanded_y_ = window_->y();
        connect(window_, &QWindow::visibleChanged, this, [this](bool visible) { if (visible) reveal(); });
    }
    emit windowChanged();
}
void EdgeHideController::set_enabled(bool value) {
    if (enabled_ == value) return;
    enabled_ = value;
    outside_.invalidate();
    if (enabled_) {
        if (window_ && !animating_) {
            expanded_x_ = window_->x();
            position_ = target_ = axis_position();
            expanded_y_ = window_->y(); applied_ = window_->position();
        }
        clock_.start(); timer_.start();
    } else {
        reveal();
        if (!animating_) {
            timer_.stop();
            if (window_) window_->setMask(QRegion());
        }
    }
    emit enabledChanged();
}
void EdgeHideController::set_blocked(bool value) {
    if (blocked_ == value) return;
    blocked_ = value;
    outside_.invalidate();
    if (blocked_) reveal();
    emit blockedChanged();
}
QRect EdgeHideController::savedGeometry() const {
    if (!window_) return {};
    return hidden_ || animating_ ? QRect(expanded_x_, expanded_y_, window_->width(), window_->height()) : window_->geometry();
}
void EdgeHideController::set_hidden(bool value) {
    if (hidden_ == value) return;
    hidden_ = value; emit hiddenChanged();
}
void EdgeHideController::set_animating(bool value) {
    if (animating_ == value) return;
    animating_ = value;
    timer_.setInterval(value ? 16 : 50);
    emit animatingChanged();
}
void EdgeHideController::prepare_dock() {
    if (!window_) return;
    if (!hidden_) screen_ = QGuiApplication::screenAt(window_->geometry().center());
    if (!screen_) screen_ = window_->screen();
    if (!screen_) screen_ = QGuiApplication::primaryScreen();
    if (!screen_) return;
    area_ = screen_->availableGeometry();
    if (!hidden_) {
        const auto geometry = window_->geometry();
        // Compare panel edges, not its center; a wide or tall panel may be
        // closer to a different edge than the screen quadrant suggests.
        const std::array<std::pair<Qt::Edge, int>, 4> distances{{
            {Qt::LeftEdge, std::max(0, geometry.left() - area_.left())},
            {Qt::RightEdge, std::max(0, area_.right() - geometry.right())},
            {Qt::TopEdge, std::max(0, geometry.top() - area_.top())},
            {Qt::BottomEdge, std::max(0, area_.bottom() - geometry.bottom())}
        }};
        auto closest = distances.front();
        for (const auto& candidate : distances) {
            if (candidate.second < closest.second ||
                (candidate.second == closest.second && candidate.first == edge_)) closest = candidate;
        }
        if (edge_ != closest.first) { edge_ = closest.first; emit dockEdgeChanged(); }
    }
    expanded_x_ = std::clamp(window_->x(), area_.left(), std::max(area_.left(), area_.right() + 1 - window_->width()));
    expanded_y_ = std::clamp(window_->y(), area_.top(), std::max(area_.top(), area_.bottom() + 1 - window_->height()));
    switch (edge_) {
    case Qt::LeftEdge: expanded_x_ = area_.left(); break;
    case Qt::RightEdge: expanded_x_ = area_.right() + 1 - window_->width(); break;
    case Qt::TopEdge: expanded_y_ = area_.top(); break;
    case Qt::BottomEdge: expanded_y_ = area_.bottom() + 1 - window_->height(); break;
    }
}
double EdgeHideController::axis_position() const {
    return window_ ? (vertical_edge() ? window_->y() : window_->x()) : 0;
}
double EdgeHideController::hidden_target() const {
    switch (edge_) {
    case Qt::LeftEdge: return area_.left() - window_->width() + edge_width;
    case Qt::RightEdge: return area_.right() + 1 - edge_width;
    case Qt::TopEdge: return area_.top() - window_->height() + edge_width;
    case Qt::BottomEdge: return area_.bottom() + 1 - edge_width;
    }
    return 0;
}
QRect EdgeHideController::trigger_area() const {
    switch (edge_) {
    case Qt::LeftEdge: return {area_.left(), expanded_y_, edge_width, window_->height()};
    case Qt::RightEdge: return {area_.right() + 1 - edge_width, expanded_y_, edge_width, window_->height()};
    case Qt::TopEdge: return {expanded_x_, area_.top(), window_->width(), edge_width};
    case Qt::BottomEdge: return {expanded_x_, area_.bottom() + 1 - edge_width, window_->width(), edge_width};
    }
    return {};
}
void EdgeHideController::move_to(double value) {
    target_ = value;
    if (!animating_) { position_ = axis_position(); velocity_ = 0; clock_.restart(); }
    set_animating(true);
    timer_.start();
}
void EdgeHideController::reveal() {
    outside_.invalidate();
    if (!window_ || !hidden_) return;
    prepare_dock();
    set_hidden(false);
    move_to(vertical_edge() ? expanded_y_ : expanded_x_);
    window_->raise(); // Reveal without stealing keyboard focus from another app.
}
bool EdgeHideController::owned_popup_open() const {
    for (auto* candidate : QGuiApplication::topLevelWindows()) {
        if (candidate == window_ || !candidate->isVisible()) continue;
        for (auto* owner = candidate->transientParent(); owner; owner = owner->transientParent())
            if (owner == window_) return true;
    }
    return false;
}
bool EdgeHideController::pointer_over_panel() const {
    POINT point{};
    if (!window_ || !GetCursorPos(&point)) return false;
    const HWND target = WindowFromPoint(point);
    return target && GetAncestor(target, GA_ROOT) == reinterpret_cast<HWND>(window_->winId());
}
void EdgeHideController::clip_to_screen() {
    if (!window_) return;
    if ((!enabled_ && !animating_) || (!hidden_ && !animating_) || area_.isEmpty()) {
        window_->setMask(QRegion());
        return;
    }
    // Keep the tucked part invisible even when there is an adjacent monitor.
    const QRect visible = window_->geometry().intersected(area_);
    window_->setMask(QRegion(visible.translated(-window_->position())));
}
void EdgeHideController::animate(double dt) {
    if (!window_ || !animating_) return;
    // Exact critically damped spring: preserve position AND velocity on reversal.
    const double omega = hidden_ ? 16.0 : 21.0;
    const double delta = position_ - target_;
    const double change = (velocity_ + omega * delta) * dt;
    const double decay = std::exp(-omega * dt);
    position_ = target_ + (delta + change) * decay;
    velocity_ = (velocity_ - omega * change) * decay;
    const bool done = std::abs(position_ - target_) < 0.35 && std::abs(velocity_) < 3;
    if (done) { position_ = target_; velocity_ = 0; }
    if (vertical_edge()) window_->setPosition(expanded_x_, qRound(position_));
    else window_->setPosition(qRound(position_), expanded_y_);
    applied_ = window_->position();
    clip_to_screen();
    if (done) {
        set_animating(false); clip_to_screen(); emit settled();
        if (!enabled_) timer_.stop();
    }
}
void EdgeHideController::tick() {
    const double dt = clock_.isValid() ? std::clamp(clock_.restart() / 1000.0, 0.001, 0.05) : 0.016;
    if (!window_ || !window_->isVisible()) { outside_.invalidate(); return; }
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
        outside_.invalidate();
        if (window_->position() != applied_) {
            set_hidden(false); set_animating(false);
            window_->setMask(QRegion());
            expanded_x_ = window_->x();
            position_ = target_ = axis_position();
            expanded_y_ = window_->y(); velocity_ = 0; applied_ = window_->position();
        }
        return;
    }
    if (enabled_) {
        if (hidden_ && (!screen_ || area_ != screen_->availableGeometry())) {
            prepare_dock();
            move_to(hidden_target());
        }
        const auto cursor = QCursor::pos();
        const auto edge = trigger_area();
        if (blocked_ || owned_popup_open() || pointer_over_panel() || (hidden_ && edge.contains(cursor))) {
            outside_.invalidate();
            if (hidden_) reveal();
        } else if (!hidden_ && !animating_) {
            if (!outside_.isValid()) outside_.start();
            if (outside_.elapsed() >= 300) {
                prepare_dock();
                if (!area_.isEmpty()) {
                    set_hidden(true);
                    move_to(hidden_target());
                }
            }
        }
    }
    animate(dt);
}
}
