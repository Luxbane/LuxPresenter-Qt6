#pragma once

#include <QProgressBar>
#include <QWidget>

class QLabel;

namespace Lux {

class SplashScreen : public QWidget
{
    Q_OBJECT
public:
    explicit SplashScreen(QWidget* parent = nullptr);
    ~SplashScreen() override = default;

    void setProgress(int percent, const QString& statusMessage);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    void setupUi();

    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
};

} // namespace Lux
