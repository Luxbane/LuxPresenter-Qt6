#include "Views/StageWindow.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QVBoxLayout>

namespace Lux {

StageWindow::StageWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(QStringLiteral("LuxPresenter - Stage Display (Confidence Monitor)"));
    setWindowIcon(QIcon(QStringLiteral(":/app_icon.png")));
    resize(960, 600);
    setMinimumSize(640, 420);

    setupUi();

    m_clockTimer.setInterval(1000);
    connect(&m_clockTimer, &QTimer::timeout, this, [this] {
        if (m_clockLabel) {
            m_clockLabel->setText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
        }
    });
    m_clockTimer.start();
}

void StageWindow::setupUi()
{
    setStyleSheet(QStringLiteral(
        "QWidget {"
        "    background-color: #06090E;"
        "    color: #F8FAFC;"
        "    font-family: 'Segoe UI', Tahoma, sans-serif;"
        "}"
        "QFrame#HeaderBar {"
        "    background-color: #0E131F;"
        "    border-bottom: 2px solid #20283A;"
        "}"
        "QFrame#CurrentCard {"
        "    background-color: #0B0F17;"
        "    border: 2px solid #2A3346;"
        "    border-radius: 10px;"
        "}"
        "QFrame#NextCard {"
        "    background-color: #101622;"
        "    border: 1px solid #232B3B;"
        "    border-radius: 8px;"
        "}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 1. Header Bar
    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("HeaderBar"));
    header->setFixedHeight(54);
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(20, 8, 20, 8);

    m_titleLabel = new QLabel(QStringLiteral("LuxPresenter Stage Display"), header);
    QFont tf;
    tf.setFamily(QStringLiteral("Segoe UI"));
    tf.setPixelSize(18);
    tf.setBold(true);
    m_titleLabel->setFont(tf);
    m_titleLabel->setStyleSheet(QStringLiteral("color: #38BDF8;"));
    hl->addWidget(m_titleLabel, 1);

    m_blackoutBadge = new QLabel(QStringLiteral("BLACKOUT ACTIVE"), header);
    QFont bf;
    bf.setFamily(QStringLiteral("Segoe UI"));
    bf.setPixelSize(12);
    bf.setBold(true);
    m_blackoutBadge->setFont(bf);
    m_blackoutBadge->setStyleSheet(QStringLiteral("background-color: #DC2626; color: white; padding: 4px 10px; border-radius: 4px; font-weight: bold;"));
    m_blackoutBadge->setVisible(false);
    hl->addWidget(m_blackoutBadge);
    hl->addSpacing(16);

    m_clockLabel = new QLabel(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), header);
    QFont cf;
    cf.setFamily(QStringLiteral("Segoe UI"));
    cf.setPixelSize(22);
    cf.setBold(true);
    m_clockLabel->setFont(cf);
    m_clockLabel->setStyleSheet(QStringLiteral("color: #F8FAFC; letter-spacing: 1px;"));
    hl->addWidget(m_clockLabel);

    root->addWidget(header);

    // 2. Main Stage Content Area
    auto* mainContent = new QWidget(this);
    auto* ml = new QVBoxLayout(mainContent);
    ml->setContentsMargins(16, 14, 16, 14);
    ml->setSpacing(12);

    // Current Slide Card
    auto* currentCard = new QFrame(mainContent);
    currentCard->setObjectName(QStringLiteral("CurrentCard"));
    auto* ccl = new QVBoxLayout(currentCard);
    ccl->setContentsMargins(18, 12, 18, 14);
    ccl->setSpacing(6);

    auto* curLabel = new QLabel(QStringLiteral("CURRENT SLIDE"), currentCard);
    curLabel->setStyleSheet(QStringLiteral("color: #38BDF8; font-size: 11px; font-weight: 800; letter-spacing: 1.5px;"));
    ccl->addWidget(curLabel);

    m_currentSlideText = new QLabel(QStringLiteral("Ready for worship..."), currentCard);
    QFont curF;
    curF.setFamily(QStringLiteral("Segoe UI"));
    curF.setPixelSize(32);
    curF.setBold(true);
    m_currentSlideText->setFont(curF);
    m_currentSlideText->setAlignment(Qt::AlignCenter);
    m_currentSlideText->setWordWrap(true);
    m_currentSlideText->setStyleSheet(QStringLiteral("color: #FFFFFF; font-weight: bold; line-height: 1.3;"));
    ccl->addWidget(m_currentSlideText, 1);

    ml->addWidget(currentCard, 65);

    // Next Slide Card
    auto* nextCard = new QFrame(mainContent);
    nextCard->setObjectName(QStringLiteral("NextCard"));
    auto* ncl = new QVBoxLayout(nextCard);
    ncl->setContentsMargins(18, 10, 18, 12);
    ncl->setSpacing(4);

    auto* nextLabel = new QLabel(QStringLiteral("NEXT SLIDE"), nextCard);
    nextLabel->setStyleSheet(QStringLiteral("color: #F59E0B; font-size: 11px; font-weight: 800; letter-spacing: 1.5px;"));
    ncl->addWidget(nextLabel);

    m_nextSlideText = new QLabel(QStringLiteral("--"), nextCard);
    QFont nextF;
    nextF.setFamily(QStringLiteral("Segoe UI"));
    nextF.setPixelSize(20);
    nextF.setBold(true);
    m_nextSlideText->setFont(nextF);
    m_nextSlideText->setAlignment(Qt::AlignCenter);
    m_nextSlideText->setWordWrap(true);
    m_nextSlideText->setStyleSheet(QStringLiteral("color: #94A3B8;"));
    ncl->addWidget(m_nextSlideText, 1);

    ml->addWidget(nextCard, 35);

    root->addWidget(mainContent, 1);

    // 3. Bottom Alert Banner
    m_alertBanner = new QLabel(this);
    m_alertBanner->setFixedHeight(42);
    QFont af;
    af.setFamily(QStringLiteral("Segoe UI"));
    af.setPixelSize(16);
    af.setBold(true);
    m_alertBanner->setFont(af);
    m_alertBanner->setAlignment(Qt::AlignCenter);
    m_alertBanner->setStyleSheet(QStringLiteral("background-color: #DC2626; color: #FFFFFF; font-weight: bold; letter-spacing: 0.5px;"));
    m_alertBanner->setVisible(false);
    root->addWidget(m_alertBanner);
}

void StageWindow::updateState(const StageState& state)
{
    m_state = state;

    if (m_titleLabel) {
        m_titleLabel->setText(!state.title.isEmpty() ? state.title : QStringLiteral("LuxPresenter Stage Display"));
    }
    if (m_blackoutBadge) {
        m_blackoutBadge->setVisible(state.isBlack);
    }
    if (m_currentSlideText) {
        if (state.isClear) {
            m_currentSlideText->setStyleSheet(QStringLiteral("color: #475569; font-weight: bold;"));
        } else {
            m_currentSlideText->setStyleSheet(QStringLiteral("color: #FFFFFF; font-weight: bold;"));
        }
        m_currentSlideText->setText(!state.currentSlide.isEmpty() ? state.currentSlide : (state.isBlack ? QStringLiteral("[ Screen Blackout ]") : QString()));
    }
    if (m_nextSlideText) {
        m_nextSlideText->setText(!state.nextSlide.isEmpty() ? state.nextSlide : QStringLiteral("(End of item)"));
    }
    if (m_alertBanner) {
        if (!state.alertMessage.trimmed().isEmpty()) {
            m_alertBanner->setText(QStringLiteral("ALERT: %1").arg(state.alertMessage));
            m_alertBanner->setVisible(true);
        } else {
            m_alertBanner->setVisible(false);
        }
    }
}

void StageWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        hide();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_F11) {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace Lux
