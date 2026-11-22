#include "Views/SplashScreen.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QVBoxLayout>

#ifndef LUX_VERSION
#define LUX_VERSION "1.0.1"
#endif

namespace Lux {

SplashScreen::SplashScreen(QWidget* parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(500, 280);

    // Center on primary screen
    if (QScreen* s = QGuiApplication::primaryScreen()) {
        const QRect geom = s->geometry();
        move(geom.center() - rect().center());
    }

    setupUi();
}

void SplashScreen::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(36, 32, 36, 28);
    root->setSpacing(0);

    // Header (Logo + Title + Subtitle)
    auto* topLayout = new QHBoxLayout;
    topLayout->setSpacing(20);

    auto* logoLabel = new QLabel(this);
    logoLabel->setPixmap(QPixmap(QStringLiteral(":/app_icon.png")).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setFixedSize(64, 64);
    topLayout->addWidget(logoLabel, 0, Qt::AlignVCenter);

    auto* titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(3);

    auto* appTitle = new QLabel(QStringLiteral("LuxPresenter"), this);
    QFont tf;
    tf.setFamily(QStringLiteral("Segoe UI"));
    tf.setPixelSize(24);
    tf.setBold(true);
    appTitle->setFont(tf);
    appTitle->setStyleSheet(QStringLiteral("color: #F8FAFC;"));

    auto* subtitle = new QLabel(QStringLiteral("Starting your presentation workspace"), this);
    QFont sf;
    sf.setFamily(QStringLiteral("Segoe UI"));
    sf.setPixelSize(12);
    subtitle->setFont(sf);
    subtitle->setStyleSheet(QStringLiteral("color: #94A3B8;"));

    titleLayout->addWidget(appTitle);
    titleLayout->addWidget(subtitle);
    topLayout->addLayout(titleLayout, 1);

    root->addLayout(topLayout);
    root->addStretch(1);

    // Status message
    m_statusLabel = new QLabel(QStringLiteral("Initializing workspace..."), this);
    QFont stf;
    stf.setFamily(QStringLiteral("Segoe UI"));
    stf.setPixelSize(12);
    m_statusLabel->setFont(stf);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #CBD5E1;"));
    root->addWidget(m_statusLabel);
    root->addSpacing(8);

    // Progress Bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar {"
        "    background-color: #1E293B;"
        "    border: none;"
        "    border-radius: 3px;"
        "}"
        "QProgressBar::chunk {"
        "    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0284C7, stop:1 #38BDF8);"
        "    border-radius: 3px;"
        "}"));
    root->addWidget(m_progressBar);
    root->addSpacing(10);

    // Bottom Version text
    auto* bottomLayout = new QHBoxLayout;
    auto* verLabel = new QLabel(QStringLiteral("v%1").arg(QStringLiteral(LUX_VERSION)), this);
    QFont vf;
    vf.setFamily(QStringLiteral("Segoe UI"));
    vf.setPixelSize(10);
    verLabel->setFont(vf);
    verLabel->setStyleSheet(QStringLiteral("color: #64748B;"));
    bottomLayout->addStretch(1);
    bottomLayout->addWidget(verLabel);
    root->addLayout(bottomLayout);
}

void SplashScreen::setProgress(int percent, const QString& statusMessage)
{
    if (m_progressBar)
        m_progressBar->setValue(percent);
    if (m_statusLabel && !statusMessage.isEmpty())
        m_statusLabel->setText(statusMessage);

    QCoreApplication::processEvents();
}

void SplashScreen::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = rect().adjusted(1, 1, -1, -1);
    QPainterPath path;
    path.addRoundedRect(r, 14, 14);

    QLinearGradient grad(r.topLeft(), r.bottomRight());
    grad.setColorAt(0.0, QColor(0x0C, 0x14, 0x27));
    grad.setColorAt(1.0, QColor(0x19, 0x22, 0x36));

    p.fillPath(path, grad);
    p.setPen(QPen(QColor(0x2D, 0x3D, 0x5C), 1.5));
    p.drawPath(path);
}

} // namespace Lux
