#pragma once

#include "Services/StageServer.h"

#include <QTime>
#include <QTimer>
#include <QWidget>

class QLabel;

namespace Lux {

class StageWindow : public QWidget
{
    Q_OBJECT
public:
    explicit StageWindow(QWidget* parent = nullptr);
    ~StageWindow() override = default;

    void updateState(const StageState& state);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    void setupUi();

    QLabel* m_titleLabel = nullptr;
    QLabel* m_clockLabel = nullptr;
    QLabel* m_blackoutBadge = nullptr;
    QLabel* m_currentSlideText = nullptr;
    QLabel* m_nextSlideText = nullptr;
    QLabel* m_alertBanner = nullptr;

    QTimer m_clockTimer;
    StageState m_state;
};

} // namespace Lux
