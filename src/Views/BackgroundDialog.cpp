#include "Views/BackgroundDialog.h"
#include "Models/Slide.h"
#include "Services/AppPaths.h"
#include "Services/BackgroundThumbnails.h"
#include "Services/BackgroundUtil.h"
#include "Services/DataService.h"
#include "Services/ThemeService.h"
#include "Views/Components/SlideCanvas.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace Lux {

namespace {
constexpr int kValueRole = Qt::UserRole;
constexpr int kBuiltinRole = Qt::UserRole + 1;
constexpr int kIdRole = Qt::UserRole + 2;
const QSize kThumb(76, 54);

QPixmap placeholder(const QString& text, const QColor& bg)
{
    QPixmap pm(kThumb);
    pm.fill(bg);
    QPainter p(&pm);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(10);
    f.setBold(true);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, text);
    return pm;
}
} // namespace

BackgroundDialog::BackgroundDialog(DataService& data, const QString& current, QWidget* parent)
    : QDialog(parent), m_data(data)
{
    setObjectName(QStringLiteral("BackgroundDialog"));
    setWindowTitle(QStringLiteral("Background Options"));
    resize(680, 480);
    setMinimumSize(540, 400);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 1. Top content area (Monitor Preview on Left + Thumbnail Grid on Right)
    auto* contentWidget = new QWidget(this);
    auto* body = new QHBoxLayout(contentWidget);
    body->setContentsMargins(10, 10, 10, 6);
    body->setSpacing(8);

    // Left: Monitor preview panel
    auto* leftPanel = new QWidget(contentWidget);
    leftPanel->setFixedWidth(210);
    auto* leftLay = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);

    // Monitor frame (dark border & bezel)
    auto* monitorFrame = new QFrame(leftPanel);
    monitorFrame->setFixedSize(210, 130);
    monitorFrame->setStyleSheet(QStringLiteral("QFrame { background: #1A1A1A; border: 1px solid #888888; }"));
    auto* monitorLay = new QVBoxLayout(monitorFrame);
    monitorLay->setContentsMargins(1, 1, 1, 1);
    monitorLay->setSpacing(0);

    m_canvas = new SlideCanvas(monitorFrame);
    m_canvas->setObjectName(QStringLiteral("bgPreview"));
    Slide sample;
    sample.content = QStringLiteral("AMAZING GRACE");
    m_canvas->setSlide(sample);
    monitorLay->addWidget(m_canvas);

    m_videoBadge = new QLabel(QStringLiteral("▶ VIDEO"), monitorFrame);
    m_videoBadge->setStyleSheet(QStringLiteral(
        "QLabel { background: rgba(0,0,0,180); color: #00CFFF; font-family: Tahoma, 'Segoe UI'; "
        "font-size: 8pt; font-weight: bold; border-radius: 2px; padding: 2px 4px; }"));
    m_videoBadge->adjustSize();
    m_videoBadge->move(210 - m_videoBadge->width() - 6, 130 - m_videoBadge->height() - 6);
    m_videoBadge->hide();
    leftLay->addWidget(monitorFrame);

    // Monitor stand neck
    auto* neckContainer = new QWidget(leftPanel);
    auto* neckLay = new QHBoxLayout(neckContainer);
    neckLay->setContentsMargins(0, 0, 0, 0);
    neckLay->setAlignment(Qt::AlignCenter);
    auto* standNeck = new QWidget(neckContainer);
    standNeck->setFixedSize(36, 10);
    const bool isDark = ThemeService::instance().isDark();
    if (isDark) {
        standNeck->setStyleSheet(QStringLiteral("background: #4A4D55; border: 1px solid #333333; border-top: none; border-bottom-left-radius: 2px; border-bottom-right-radius: 2px;"));
    } else {
        standNeck->setStyleSheet(QStringLiteral("background: #C0C0C0; border: 1px solid #888888; border-top: none; border-bottom-left-radius: 2px; border-bottom-right-radius: 2px;"));
    }
    neckLay->addWidget(standNeck);
    leftLay->addWidget(neckContainer);

    // Stand upper base
    auto* base1Container = new QWidget(leftPanel);
    auto* base1Lay = new QHBoxLayout(base1Container);
    base1Lay->setContentsMargins(0, 0, 0, 0);
    base1Lay->setAlignment(Qt::AlignCenter);
    auto* standBase1 = new QWidget(base1Container);
    standBase1->setFixedSize(76, 4);
    standBase1->setStyleSheet(isDark ? QStringLiteral("background: #3A3D44;") : QStringLiteral("background: #A0A0A0;"));
    base1Lay->addWidget(standBase1);
    leftLay->addWidget(base1Container);

    // Stand lower base
    auto* base2Container = new QWidget(leftPanel);
    auto* base2Lay = new QHBoxLayout(base2Container);
    base2Lay->setContentsMargins(0, 0, 0, 0);
    base2Lay->setAlignment(Qt::AlignCenter);
    auto* standBase2 = new QWidget(base2Container);
    standBase2->setFixedSize(100, 2);
    standBase2->setStyleSheet(isDark ? QStringLiteral("background: #2E3036;") : QStringLiteral("background: #888888;"));
    base2Lay->addWidget(standBase2);
    leftLay->addWidget(base2Container);

    leftLay->addSpacing(8);

    m_override = new QCheckBox(QStringLiteral("Override Global Options\nfor this Background"), leftPanel);
    m_override->setFont(QFont(QStringLiteral("Tahoma"), 9));
    m_override->setEnabled(false);
    leftLay->addWidget(m_override);

    m_note = new QLabel(leftPanel);
    m_note->setObjectName(QStringLiteral("bgNote"));
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("font-size: 10px; color: #DC2626;"));
    leftLay->addSpacing(4);
    leftLay->addWidget(m_note);

    leftLay->addStretch(1);
    body->addWidget(leftPanel);

    // Right: Thumbnail grid panel
    auto* rightFrame = new QFrame(contentWidget);
    rightFrame->setObjectName(QStringLiteral("BgThumbnailFrame"));
    auto* rightLay = new QGridLayout(rightFrame);
    rightLay->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(rightFrame);
    m_list->setObjectName(QStringLiteral("bgList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(kThumb);
    m_list->setGridSize(QSize(90, 68));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    rightLay->addWidget(m_list, 0, 0);

    m_emptyLabel = new QLabel(QStringLiteral("No backgrounds added yet.\nClick + Add to add image or video backgrounds."), rightFrame);
    m_emptyLabel->setObjectName(QStringLiteral("BgEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    rightLay->addWidget(m_emptyLabel, 0, 0);

    body->addWidget(rightFrame, 1);
    root->addWidget(contentWidget, 1);

    // 2. Action Bar (+ Add, Add Multiple, - Delete)
    auto* actionBar = new QFrame(this);
    actionBar->setObjectName(QStringLiteral("BgActionBar"));
    auto* actLay = new QHBoxLayout(actionBar);
    actLay->setContentsMargins(10, 6, 10, 6);
    actLay->setSpacing(6);

    m_addBtn = new QPushButton(QStringLiteral("+ Add"), actionBar);
    m_addBtn->setObjectName(QStringLiteral("bgAddFiles"));

    auto* addMenu = new QMenu(m_addBtn);
    auto* actFile = addMenu->addAction(QStringLiteral("Add Image/Video..."));
    auto* actColor = addMenu->addAction(QStringLiteral("Add Colour..."));
    m_addBtn->setMenu(addMenu);

    m_addMultipleBtn = new QPushButton(QStringLiteral("Add Multiple"), actionBar);
    m_addMultipleBtn->setObjectName(QStringLiteral("bgAddMultiple"));

    m_remove = new QPushButton(QStringLiteral("− Delete"), actionBar);
    m_remove->setObjectName(QStringLiteral("bgRemove"));

    actLay->addWidget(m_addBtn);
    actLay->addWidget(m_addMultipleBtn);
    actLay->addWidget(m_remove);
    actLay->addStretch(1);
    root->addWidget(actionBar);

    // 3. Footer Bar (Also show on live output checkbox + OK / Cancel)
    auto* footerBar = new QFrame(this);
    footerBar->setObjectName(QStringLiteral("BgFooterBar"));
    auto* footerLay = new QHBoxLayout(footerBar);
    footerLay->setContentsMargins(10, 8, 10, 8);
    footerLay->setSpacing(6);

    m_live = new QCheckBox(QStringLiteral("Also show on the live output"), footerBar);
    m_live->setObjectName(QStringLiteral("bgApplyLive"));
    m_live->setFont(QFont(QStringLiteral("Tahoma"), 10));
    m_live->setChecked(false); // Default NOT checked!

    m_okBtn = new QPushButton(QStringLiteral("OK"), footerBar);
    m_okBtn->setObjectName(QStringLiteral("bgOk"));
    m_okBtn->setFixedSize(72, 24);
    m_okBtn->setDefault(true);

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), footerBar);
    m_cancelBtn->setObjectName(QStringLiteral("bgCancel"));
    m_cancelBtn->setFixedSize(72, 24);

    footerLay->addWidget(m_live);
    footerLay->addStretch(1);
    footerLay->addWidget(m_okBtn);
    footerLay->addWidget(m_cancelBtn);
    root->addWidget(footerBar);

    // Signal connections
    connect(m_list, &QListWidget::itemSelectionChanged, this, &BackgroundDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { accept(); });
    connect(actFile, &QAction::triggered, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Add Background"), QString(), Backgrounds::fileDialogFilter());
        const QStringList problems = addFiles(files);
        if (!problems.isEmpty())
            QMessageBox::warning(this, QStringLiteral("Background Options"), problems.join(QLatin1Char('\n')));
    });
    connect(actColor, &QAction::triggered, this, [this] {
        const QColor c = QColorDialog::getColor(QColor(0, 24, 51), this, QStringLiteral("Background Colour"));
        if (c.isValid())
            addColor(c.name(QColor::HexRgb));
    });
    connect(m_addMultipleBtn, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Add Multiple Backgrounds"), QString(), Backgrounds::fileDialogFilter());
        const QStringList problems = addFiles(files);
        if (!problems.isEmpty())
            QMessageBox::warning(this, QStringLiteral("Background Options"), problems.join(QLatin1Char('\n')));
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Background Options"),
                                  QStringLiteral("Remove this background from the list? (The file itself is not deleted.)")) == QMessageBox::Yes)
            removeSelected();
    });
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(&BackgroundThumbnails::instance(), &BackgroundThumbnails::ready, this, [this](const QString& path) {
        for (int i = 0; i < m_list->count() && i < m_items.size(); ++i) {
            if (m_items.at(i).filePath == path) {
                m_list->item(i)->setIcon(iconFor(m_items.at(i)));
                break;
            }
        }
        if (selectedValue() == path) {
            m_canvas->setBackground(path);
        }
    });

    reload(Backgrounds::usable(current));
}

void BackgroundDialog::reload(const QString& select)
{
    const QStringList deleted = m_data.settings().deletedBuiltinBackgrounds;
    m_items.clear();
    for (const BackgroundItem& b : Backgrounds::builtinColors()) {
        if (!deleted.contains(b.id) && !deleted.contains(b.hexColor))
            m_items.append(b);
    }
    m_items += m_data.backgrounds();

    m_list->clear();
    for (const BackgroundItem& b : m_items) {
        auto* it = new QListWidgetItem(iconFor(b), QString());
        const QString value = Backgrounds::valueOf(b);
        const bool missing = b.type != BackgroundType::Color && !QFileInfo(b.filePath).isFile();
        it->setToolTip(b.name + (missing ? QStringLiteral(" (missing)") : QString()));
        it->setData(kValueRole, value);
        it->setData(kBuiltinRole, b.id.startsWith(QLatin1String("builtin-")));
        it->setData(kIdRole, b.id);
        m_list->addItem(it);
    }
    selectValue(select);
    if (m_list->selectedItems().isEmpty() && m_list->count() > 0)
        m_list->setCurrentRow(0);
    onSelectionChanged();
    if (m_emptyLabel)
        m_emptyLabel->setVisible(m_list->count() == 0);
}

QIcon BackgroundDialog::iconFor(const BackgroundItem& b) const
{
    if (b.type == BackgroundType::Color) {
        QPixmap pm(kThumb);
        pm.fill(QColor(b.hexColor));
        return QIcon(pm);
    }
    if (!QFileInfo(b.filePath).isFile())
        return QIcon(placeholder(QStringLiteral("MISSING"), QColor(0x7F, 0x1D, 0x1D)));
    auto& th = BackgroundThumbnails::instance();
    const QImage img = th.image(b.filePath, 320);
    if (img.isNull()) {
        const bool pending = th.state(b.filePath, 320) == BackgroundThumbnails::State::Pending;
        return QIcon(placeholder(pending ? QStringLiteral("...") : (b.type == BackgroundType::Video ? QStringLiteral("▶") : QStringLiteral("UNREADABLE")),
                                 QColor(0x33, 0x41, 0x55)));
    }
    QPixmap pm(kThumb);
    pm.fill(Qt::black);
    QPainter p(&pm);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(0, 0, kThumb.width(), kThumb.height()), img);
    if (b.type == BackgroundType::Video) {
        // Centered semi-transparent play triangle matching Image 2
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 180));
        QPolygonF tri;
        const qreal cx = kThumb.width() / 2.0;
        const qreal cy = kThumb.height() / 2.0;
        const qreal sz = 10.0;
        tri << QPointF(cx - sz * 0.5, cy - sz * 0.8)
            << QPointF(cx + sz * 0.8, cy)
            << QPointF(cx - sz * 0.5, cy + sz * 0.8);
        p.drawPolygon(tri);
    }
    return QIcon(pm);
}

void BackgroundDialog::refreshIcons()
{
    for (int i = 0; i < m_list->count() && i < m_items.size(); ++i)
        m_list->item(i)->setIcon(iconFor(m_items.at(i)));
}

void BackgroundDialog::onSelectionChanged()
{
    const QString v = selectedValue();
    m_canvas->setBackground(v.isEmpty() ? QString::fromLatin1(Backgrounds::kDefaultColor) : v);
    QListWidgetItem* it = m_list->currentItem();
    m_remove->setEnabled(it != nullptr); // Any item (built-in or user-added) can be removed!
    if (m_override)
        m_override->setEnabled(it != nullptr);
    if (m_videoBadge) {
        m_videoBadge->setVisible(Backgrounds::isVideoPath(v));
        m_videoBadge->move(210 - m_videoBadge->width() - 6, 130 - m_videoBadge->height() - 6);
    }
    if (!v.isEmpty() && isMissing(v))
        m_note->setText(QStringLiteral("This file is missing."));
    else
        m_note->clear();
}

QString BackgroundDialog::selectedValue() const
{
    const QListWidgetItem* it = m_list->currentItem();
    return it ? it->data(kValueRole).toString() : QString();
}

bool BackgroundDialog::isMissing(const QString& value) const
{
    return !value.startsWith(QLatin1Char('#')) && !QFileInfo(value).isFile();
}

bool BackgroundDialog::applyToLive() const { return m_live->isChecked(); }
void BackgroundDialog::setApplyToLive(bool on) { m_live->setChecked(on); }
bool BackgroundDialog::overrideGlobal() const { return m_override ? m_override->isChecked() : false; }
void BackgroundDialog::setOverrideGlobal(bool on) { if (m_override) m_override->setChecked(on); }
int BackgroundDialog::itemCount() const { return m_list->count(); }

void BackgroundDialog::selectValue(const QString& value)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(kValueRole).toString().compare(value, Qt::CaseInsensitive) == 0) {
            m_list->setCurrentRow(i);
            return;
        }
}

QStringList BackgroundDialog::addFiles(const QStringList& paths)
{
    QStringList problems;
    QList<BackgroundItem> toAdd;
    for (const QString& path : paths) {
        BackgroundItem b;
        QString err;
        if (!Backgrounds::itemFromFile(path, &b, &err)) {
            problems << err;
            continue;
        }
        if (b.type == BackgroundType::Image && !QImageReader(path).canRead()) {
            problems << QStringLiteral("%1: the image cannot be read (damaged, or the format is not supported by this build).").arg(QFileInfo(path).fileName());
            continue;
        }
        bool dup = false;
        for (const BackgroundItem& e : m_items + toAdd)
            dup = dup || (e.type != BackgroundType::Color && QFileInfo(e.filePath) == QFileInfo(b.filePath));
        if (dup) {
            problems << QStringLiteral("%1: already in the list.").arg(QFileInfo(path).fileName());
            continue;
        }
        toAdd << b;
    }
    if (!toAdd.isEmpty()) {
        if (!m_data.addBackgrounds(toAdd))
            problems << QStringLiteral("The list could not be saved: %1").arg(m_data.lastError());
        reload(toAdd.last().filePath);
    }
    return problems;
}

bool BackgroundDialog::addColor(const QString& hex)
{
    if (!Backgrounds::isColor(hex))
        return false;
    const BackgroundItem b = Backgrounds::itemFromColor(hex);
    for (const BackgroundItem& e : m_items)
        if (e.type == BackgroundType::Color && e.hexColor.compare(b.hexColor, Qt::CaseInsensitive) == 0) {
            selectValue(b.hexColor);   // already there: just select it
            return true;
        }
    if (!m_data.addBackground(b))
        return false;
    reload(b.hexColor);
    return true;
}

bool BackgroundDialog::removeSelected()
{
    const QListWidgetItem* it = m_list->currentItem();
    if (!it)
        return false;
    const QString id = it->data(kIdRole).toString();
    const QString filePath = it->data(kValueRole).toString();
    const bool isBuiltin = it->data(kBuiltinRole).toBool();
    bool ok = false;
    if (isBuiltin) {
        AppSettings s = m_data.settings();
        if (!s.deletedBuiltinBackgrounds.contains(id)) {
            s.deletedBuiltinBackgrounds.append(id);
            m_data.saveSettings(s);
        }
        ok = true;
    } else {
        ok = m_data.deleteBackground(id);
        if (!filePath.isEmpty() && !filePath.startsWith(QLatin1Char('#'))) {
            QFile::remove(AppPaths::thumbnailPathFor(filePath));
        }
    }
    reload();
    return ok;
}

} // namespace Lux
