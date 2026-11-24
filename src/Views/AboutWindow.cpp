#include "Views/AboutWindow.h"

#include "Views/UiUtil.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Lux {

namespace {
QLabel* linkRow(const QString& prefix, const QString& url, QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setTextFormat(Qt::RichText);
    l->setOpenExternalLinks(true);
    l->setFont(tahoma(12));
    l->setText(QStringLiteral("<span style='color:#475569'>%1</span> <a href='%2' style='color:#0284C7; text-decoration:none'>%2</a>")
                   .arg(prefix, url));
    l->setContentsMargins(0, 2, 0, 2);
    return l;
}
} // namespace

AboutWindow::AboutWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About LuxPresenter"));
    setWindowIcon(QIcon(QStringLiteral(":/app_icon.png")));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setFixedSize(460, 330);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(0);

    auto* main = new QHBoxLayout;
    main->setSpacing(18);
    auto* logo = new QLabel(this);
    logo->setPixmap(QPixmap(QStringLiteral(":/app_icon.png")).scaled(68, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedSize(68, 68);
    main->addWidget(logo, 0, Qt::AlignTop);

    auto* info = new QVBoxLayout;
    info->setSpacing(0);
    auto* name = new QLabel(QStringLiteral("LuxPresenter"), this);
    QFont nf = tahoma(20, true);
    nf.setFamily(QStringLiteral("Segoe UI"));
    name->setFont(nf);
    auto* version = new QLabel(QStringLiteral("Version %1").arg(QStringLiteral(LUX_VERSION)), this);
    version->setStyleSheet(QStringLiteral("color:#64748B;"));
    version->setFont(tahoma(12));
    version->setContentsMargins(0, 1, 0, 3);
    auto* created = new QLabel(QStringLiteral("Created by <b>Luxbane</b>"), this);
    created->setFont(tahoma(12));
    created->setContentsMargins(0, 2, 0, 2);
    info->addWidget(name);
    info->addWidget(version);
    info->addWidget(created);
    info->addSpacing(4);
    info->addWidget(linkRow(QStringLiteral("\U0001F4BB GitHub:"), QStringLiteral("https://github.com/Luxbane/LuxPresenter-Qt6"), this));
    info->addWidget(linkRow(QStringLiteral("\U0001F4F7 Instagram:"), QStringLiteral("https://www.instagram.com/luxbane._"), this));
    info->addWidget(linkRow(QStringLiteral("\u2615 Ko-fi:"), QString::fromLatin1(kKofiUrl), this));
    info->addSpacing(10);
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color:#CBD5E1;"));
    info->addWidget(sep);
    info->addSpacing(10);
    auto* f1 = new QLabel(QStringLiteral("\u2022 Dual-display hardware accelerated projector output"), this);
    f1->setFont(tahoma(12));
    auto* f2 = new QLabel(QStringLiteral("\u2022 Ctrl+C = Clear, Ctrl+B = Black, Space = Go Live"), this);
    f2->setFont(tahoma(12));
    f1->setContentsMargins(0, 0, 0, 4);
    f2->setContentsMargins(0, 0, 0, 2);
    info->addWidget(f1);
    info->addWidget(f2);
    info->addStretch(1);
    main->addLayout(info, 1);
    root->addLayout(main, 1);

    auto* bar = new QFrame(this);
    bar->setStyleSheet(QStringLiteral("QFrame#AboutBar{border:none; border-top:1px solid #E2E8F0;}"));
    bar->setObjectName(QStringLiteral("AboutBar"));
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(0, 10, 0, 0);
    auto* kofi = new QPushButton(QStringLiteral("Support on Ko-fi \u2615"), bar);
    kofi->setFixedHeight(26);
    kofi->setStyleSheet(QStringLiteral("QPushButton{background:#FFFBEB; color:#B45309; border:1px solid #FCD34D; font-weight:600; font-size:12px; padding:2px 10px;}"));
    connect(kofi, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kKofiUrl))); });
    auto* ok = new QPushButton(QStringLiteral("OK"), bar);
    ok->setFixedSize(82, 26);
    ok->setDefault(true);
    ok->setStyleSheet(QStringLiteral("QPushButton{font-weight:600; font-size:12px; border-color:#94A3B8;}"));
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    bl->addWidget(kofi);
    bl->addStretch(1);
    bl->addWidget(ok);
    root->addWidget(bar);
    root->setContentsMargins(20, 18, 20, 16);
}

} // namespace Lux
