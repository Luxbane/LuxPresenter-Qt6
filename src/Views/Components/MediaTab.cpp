#include "Views/Components/MediaTab.h"

#include "Services/AppPaths.h"
#include "Services/DataService.h"
#include "Services/ThemeService.h"
#include "Views/Components/TabStrip.h"
#include "Views/UiUtil.h"

#include <QActionGroup>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace Lux {

namespace {

// Dual-mode delegate: 88x68 filmstrip card (Grid View) or compact 28px row with badge (List View).
class MediaCardDelegate : public QStyledItemDelegate
{
public:
    explicit MediaCardDelegate(MediaTab* tab, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_tab(tab) {}

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const override
    {
        if (m_tab && !m_tab->isGridView()) {
            const int w = opt.rect.width() > 0 ? opt.rect.width() : 240;
            return {w, 28};
        }
        return {96, 76};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        const QString path = idx.data(Qt::UserRole).toString();
        const QString hex = idx.data(Qt::UserRole + 1).toString();
        const int typeVal = idx.data(Qt::UserRole + 3).toInt();

        static const QStringList videoExts = {QStringLiteral("mp4"), QStringLiteral("wmv"), QStringLiteral("avi"),
                                              QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("m4v"),
                                              QStringLiteral("mpg"), QStringLiteral("webm"), QStringLiteral("flv"),
                                              QStringLiteral("ts")};
        static const QStringList audioExts = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("wma"),
                                              QStringLiteral("aac"), QStringLiteral("m4a"), QStringLiteral("ogg")};
        const QString ext = QFileInfo(path).suffix().toLower();
        const bool isVideo = (typeVal == static_cast<int>(MediaType::Video) || videoExts.contains(ext));
        const bool isAudio = (typeVal == static_cast<int>(MediaType::Audio) || audioExts.contains(ext));

        if (m_tab && !m_tab->isGridView()) {
            // === LIST VIEW MODE ===
            if (opt.state & QStyle::State_Selected) {
                p->fillRect(opt.rect, Theme::selectionBlue());
            }

            // 1. Mini thumbnail (32x22)
            const QRect thumb(opt.rect.left() + 4, opt.rect.top() + (opt.rect.height() - 22) / 2, 32, 22);
            QColor fill(0x0A, 0x18, 0x26);
            if (path.startsWith(QLatin1Char('#')) && QColor::isValidColorName(path))
                fill = QColor(path);
            else if (path.isEmpty() && QColor::isValidColorName(hex))
                fill = QColor(hex);
            p->fillRect(thumb, fill);

            bool drewPreview = false;
            if (!path.isEmpty() && !path.startsWith(QLatin1Char('#'))) {
                const QPixmap pm(path);
                if (!pm.isNull()) {
                    p->drawPixmap(thumb, pm.scaled(thumb.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    drewPreview = true;
                }
            }
            if (!drewPreview) {
                p->setFont(tahoma(9, true));
                if (isVideo) {
                    p->setPen(QColor(0x38, 0xBD, 0xF8));
                    p->drawText(thumb, Qt::AlignCenter, QStringLiteral("\u25B6"));
                } else if (isAudio) {
                    p->setPen(QColor(0xA8, 0x55, 0xF7));
                    p->drawText(thumb, Qt::AlignCenter, QStringLiteral("\u266B"));
                }
            }
            p->setPen(QColor(0x47, 0x55, 0x69));
            p->setBrush(Qt::NoBrush);
            p->drawRect(thumb);

            // 2. Type badge on the right
            const QString badgeText = isVideo ? QStringLiteral("Video") : (isAudio ? QStringLiteral("Audio") : QStringLiteral("Image"));
            p->setFont(tahoma(8));
            const int badgeWidth = p->fontMetrics().horizontalAdvance(badgeText) + 12;
            const QRect badge(opt.rect.right() - badgeWidth - 6, opt.rect.top() + (opt.rect.height() - 18) / 2, badgeWidth, 18);
            p->setPen(opt.state & QStyle::State_Selected ? QColor(0x93, 0xC5, 0xFD) : QColor(0x64, 0x74, 0x8B));
            p->setBrush(opt.state & QStyle::State_Selected ? QBrush(QColor(0x1E, 0x40, 0xAF)) : QBrush(QColor(0x33, 0x41, 0x55)));
            p->drawRoundedRect(badge, 2, 2);
            p->setPen(opt.state & QStyle::State_Selected ? Qt::white : QColor(0x94, 0xA3, 0xB8));
            p->drawText(badge, Qt::AlignCenter, badgeText);

            // 3. Name label in middle
            const QRect titleRect(thumb.right() + 8, opt.rect.top(), qMax(20, badge.left() - thumb.right() - 14), opt.rect.height());
            p->setFont(tahoma(9, true));
            p->setPen(opt.state & QStyle::State_Selected ? Qt::white : opt.palette.text().color());
            p->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                        p->fontMetrics().elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, titleRect.width()));

            p->restore();
            return;
        }

        // === GRID VIEW MODE (Kotak-kotak) ===
        if (opt.state & QStyle::State_Selected)
            p->fillRect(opt.rect, Theme::selectionBlue());
        const QRect card(opt.rect.left() + 4, opt.rect.top() + 4, 88, 68);
        QColor fill(0x0A, 0x18, 0x26);
        if (path.startsWith(QLatin1Char('#')) && QColor::isValidColorName(path))
            fill = QColor(path);
        else if (path.isEmpty() && QColor::isValidColorName(hex))
            fill = QColor(hex);
        p->fillRect(card, fill);

        bool drewPreview = false;
        if (!path.isEmpty() && !path.startsWith(QLatin1Char('#'))) {
            const QPixmap pm(path);
            if (!pm.isNull()) {
                p->drawPixmap(card, pm.scaled(card.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                drewPreview = true;
            }
        }

        if (!drewPreview) {
            if (isVideo) {
                QLinearGradient g(card.topLeft(), card.bottomRight());
                g.setColorAt(0, QColor(0x1E, 0x29, 0x3B));
                g.setColorAt(1, QColor(0x0F, 0x17, 0x2A));
                p->fillRect(card, g);

                p->setPen(QColor(0x38, 0xBD, 0xF8));
                p->setFont(tahoma(14, true));
                p->drawText(card.adjusted(0, -6, 0, -6), Qt::AlignCenter, QStringLiteral("\u25B6"));
            } else if (isAudio) {
                QLinearGradient g(card.topLeft(), card.bottomRight());
                g.setColorAt(0, QColor(0x33, 0x41, 0x55));
                g.setColorAt(1, QColor(0x1E, 0x29, 0x3B));
                p->fillRect(card, g);

                p->setPen(QColor(0xA8, 0x55, 0xF7));
                p->setFont(tahoma(14, true));
                p->drawText(card.adjusted(0, -6, 0, -6), Qt::AlignCenter, QStringLiteral("\u266B"));
            }
        }

        // Filmstrip dark strip at top
        p->fillRect(QRect(card.left(), card.top(), card.width(), 4), QColor(0, 0, 0, 0x30));

        // Name label on translucent black bar at bottom
        const QRect bar(card.left(), card.bottom() - 15, card.width(), 16);
        p->fillRect(bar, QColor(0, 0, 0, 0xD0));
        p->setFont(tahoma(8, true));
        p->setPen(Qt::white);
        p->drawText(bar.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    p->fontMetrics().elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, bar.width() - 8));

        // 2px border around card
        p->setPen(QPen(opt.state & QStyle::State_Selected ? Theme::selectionBlue() : QColor(0x33, 0x33, 0x33), 2));
        p->setBrush(Qt::NoBrush);
        p->drawRect(card.adjusted(1, 1, -1, -1));
        p->restore();
    }

private:
    MediaTab* m_tab = nullptr;
};

QPushButton* barButton(const QString& text, const QString& tip, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    b->setStyleSheet(QStringLiteral("QPushButton{font-size:10px; padding:2px 6px;}"));
    b->setToolTip(tip);
    return b;
}

} // namespace

MediaTab::MediaTab(DataService& data, QWidget* parent) : QWidget(parent), m_data(data)
{
    m_isGridView = m_data.settings().isMediaGridView;
    setObjectName(QStringLiteral("PanelBg"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 1. Folder selector: [folder icon] [Videos v] [...]
    auto* top = new QFrame(this);
    top->setObjectName(QStringLiteral("SurfaceBarBottom"));
    auto* tl = new QHBoxLayout(top);
    tl->setContentsMargins(6, 4, 6, 4);
    tl->setSpacing(0);
    auto* folderIcon = new QLabel(QStringLiteral("\U0001F4C1"), top);
    folderIcon->setFont(segoeEmoji(14));
    folderIcon->setContentsMargins(2, 0, 6, 0);
    m_folder = new QComboBox(top);
    m_folder->setFixedHeight(24);
    m_folder->addItems({QStringLiteral("Videos"), QStringLiteral("Images"), QStringLiteral("Audio")});
    m_browse = new QPushButton(QStringLiteral("..."), top);
    m_browse->setFixedSize(24, 24);
    m_browse->setStyleSheet(QStringLiteral("QPushButton{font-size:10px; padding:0;}"));
    m_browse->setToolTip(QStringLiteral("Browse Folder"));
    m_browse->setEnabled(true);
    tl->addWidget(folderIcon);
    tl->addWidget(m_folder, 1);
    tl->addSpacing(4);
    tl->addWidget(m_browse);
    root->addWidget(top);

    // 2. Filmstrip grid / List
    m_grid = new QListWidget(this);
    m_grid->setObjectName(QStringLiteral("MediaGrid"));
    m_grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_grid->setFrameShape(QFrame::NoFrame);
    m_grid->setItemDelegate(new MediaCardDelegate(this, m_grid));
    applyViewMode();
    root->addWidget(m_grid, 1);

    // 3. Bottom bar: [+ Add] [- Delete] ... gear, then sub-tabs
    auto* bottom = new QFrame(this);
    bottom->setObjectName(QStringLiteral("SurfaceBarTop"));
    auto* bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);
    auto* actions = new QWidget(bottom);
    auto* al = new QHBoxLayout(actions);
    al->setContentsMargins(6, 3, 6, 2);
    al->setSpacing(4);
    m_add = barButton(QStringLiteral("\u2795 Add"), QStringLiteral("Add Media"), actions);
    m_delete = barButton(QStringLiteral("\u2796 Delete"), QStringLiteral("Delete Media"), actions);
    m_add->setEnabled(true);
    m_delete->setEnabled(false);

    m_options = new QPushButton(QStringLiteral("\u2699"), actions);
    m_options->setFixedSize(22, 22);
    m_options->setFont(segoeSymbol(12));
    m_options->setToolTip(QStringLiteral("Media Options"));
    m_options->setCursor(Qt::PointingHandCursor);
    m_options->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; border:none; color:#94A3B8; font-size:12px; }"
        "QPushButton:hover { background:rgba(255,255,255,0.12); border-radius:3px; color:#F8FAFC; }"
    ));
    connect(m_options, &QPushButton::clicked, this, &MediaTab::onOptionsClicked);

    al->addWidget(m_add);
    al->addWidget(m_delete);
    al->addStretch(1);
    al->addWidget(m_options);
    bl->addWidget(actions);

    m_subTabs = new TabStrip(TabStrip::Density::Small, /*topBorder=*/false, bottom);
    for (const char* name : {"Videos", "Images", "Audio"})
        m_subTabs->addTab(QString::fromLatin1(name));
    bl->addWidget(m_subTabs);
    root->addWidget(bottom);

    // Connect top dropdown and bottom sub-tabs
    connect(m_folder, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_subTabs->currentIndex() != i)
            m_subTabs->setCurrentIndex(i);
        refresh();
    });
    connect(m_subTabs, &TabStrip::currentChanged, this, [this](int i) {
        if (m_folder->currentIndex() != i)
            m_folder->setCurrentIndex(i);
        refresh();
    });

    // Connect Add, Delete, Browse
    connect(m_add, &QPushButton::clicked, this, &MediaTab::onAddClicked);
    connect(m_delete, &QPushButton::clicked, this, &MediaTab::onDeleteClicked);
    connect(m_browse, &QPushButton::clicked, this, &MediaTab::onBrowseClicked);

    // Connect selection and double-click
    connect(m_grid, &QListWidget::itemSelectionChanged, this, &MediaTab::onSelectionChanged);
    connect(m_grid, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (!it)
            return;
        const QString id = it->data(Qt::UserRole + 2).toString();
        for (const MediaItem& m : m_data.mediaItems()) {
            if (m.id == id) {
                emit mediaActivated(m);
                break;
            }
        }
    });

    refresh();
}

void MediaTab::refresh()
{
    const QString currentSelectedId = selectedMedia() ? selectedMedia()->id : QString();
    m_grid->clear();
    const int tab = m_subTabs ? m_subTabs->currentIndex() : 0;
    static const QStringList videoExts = {QStringLiteral("mp4"), QStringLiteral("wmv"), QStringLiteral("avi"),
                                          QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("m4v"),
                                          QStringLiteral("mpg"), QStringLiteral("webm"), QStringLiteral("flv"),
                                          QStringLiteral("ts")};
    static const QStringList audioExts = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("wma"),
                                          QStringLiteral("aac"), QStringLiteral("m4a"), QStringLiteral("ogg")};

    QListWidgetItem* toSelect = nullptr;
    for (const MediaItem& m : m_data.mediaItems()) {
        const QString ext = QFileInfo(m.filePath).suffix().toLower();
        bool match = false;
        if (tab == 0) { // Videos
            match = (m.type == MediaType::Video || videoExts.contains(ext));
        } else if (tab == 1) { // Images
            match = (m.type == MediaType::Image || (!videoExts.contains(ext) && !audioExts.contains(ext) && m.type != MediaType::Audio));
        } else if (tab == 2) { // Audio
            match = (m.type == MediaType::Audio || audioExts.contains(ext));
        }
        if (!match)
            continue;

        auto* it = new QListWidgetItem(m.name, m_grid);
        it->setData(Qt::UserRole, m.filePath);
        it->setData(Qt::UserRole + 1, m.hexColor);
        it->setData(Qt::UserRole + 2, m.id);
        it->setData(Qt::UserRole + 3, static_cast<int>(m.type));
        if (!currentSelectedId.isEmpty() && m.id == currentSelectedId)
            toSelect = it;
    }
    if (!toSelect && m_grid->count() > 0)
        toSelect = m_grid->item(0);
    if (toSelect) {
        toSelect->setSelected(true);
        m_grid->setCurrentItem(toSelect);
    }
    m_delete->setEnabled(!m_grid->selectedItems().isEmpty());
    if (m_grid->selectedItems().isEmpty())
        emit mediaCleared();
}

std::optional<MediaItem> MediaTab::selectedMedia() const
{
    auto* it = m_grid->currentItem();
    if (!it) {
        const auto items = m_grid->selectedItems();
        if (!items.isEmpty())
            it = items.first();
    }
    if (!it)
        return std::nullopt;
    const QString id = it->data(Qt::UserRole + 2).toString();
    for (const MediaItem& m : m_data.mediaItems())
        if (m.id == id)
            return m;
    return std::nullopt;
}

void MediaTab::onAddClicked()
{
    QString title, filter;
    const int tab = m_subTabs ? m_subTabs->currentIndex() : 0;
    if (tab == 0) {
        title = QStringLiteral("Select Video File");
        filter = QStringLiteral("Video Files (*.mp4 *.wmv *.avi *.mov *.mkv *.m4v *.mpg *.webm *.flv *.ts);;All Files (*.*)");
    } else if (tab == 1) {
        title = QStringLiteral("Select Image File");
        filter = QStringLiteral("Image Files (*.jpg *.jpeg *.png *.bmp *.webp *.gif);;All Files (*.*)");
    } else {
        title = QStringLiteral("Select Audio File");
        filter = QStringLiteral("Audio Files (*.mp3 *.wav *.wma *.aac *.m4a *.ogg);;All Files (*.*)");
    }

    const QStringList files = QFileDialog::getOpenFileNames(this, title, QString(), filter);
    if (files.isEmpty())
        return;

    QString lastId;
    static const QStringList videoExts = {QStringLiteral("mp4"), QStringLiteral("wmv"), QStringLiteral("avi"),
                                          QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("m4v"),
                                          QStringLiteral("mpg"), QStringLiteral("webm"), QStringLiteral("flv"),
                                          QStringLiteral("ts")};
    static const QStringList audioExts = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("wma"),
                                          QStringLiteral("aac"), QStringLiteral("m4a"), QStringLiteral("ogg")};

    for (const QString& file : files) {
        MediaItem m;
        m.name = QFileInfo(file).completeBaseName();
        m.filePath = file;
        const QString ext = QFileInfo(file).suffix().toLower();
        if (videoExts.contains(ext) || tab == 0)
            m.type = MediaType::Video;
        else if (audioExts.contains(ext) || tab == 2)
            m.type = MediaType::Audio;
        else
            m.type = MediaType::Image;

        m_data.addMediaItem(m);
        lastId = m.id;
    }

    refresh();

    if (!lastId.isEmpty()) {
        for (int i = 0; i < m_grid->count(); ++i) {
            if (m_grid->item(i)->data(Qt::UserRole + 2).toString() == lastId) {
                m_grid->setCurrentRow(i);
                m_grid->item(i)->setSelected(true);
                break;
            }
        }
    }
}

void MediaTab::onDeleteClicked()
{
    const auto items = m_grid->selectedItems();
    if (items.isEmpty())
        return;
    for (auto* it : items) {
        const QString id = it->data(Qt::UserRole + 2).toString();
        const QString path = it->data(Qt::UserRole).toString();
        m_data.deleteMediaItem(id);
        if (!path.isEmpty()) {
            QFile::remove(AppPaths::thumbnailPathFor(path));
        }
    }
    refresh();
}

void MediaTab::onBrowseClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Media Folder"));
    if (dir.isEmpty())
        return;

    QDir d(dir);
    static const QStringList filters = {
        QStringLiteral("*.mp4"), QStringLiteral("*.wmv"), QStringLiteral("*.avi"), QStringLiteral("*.mov"),
        QStringLiteral("*.mkv"), QStringLiteral("*.m4v"), QStringLiteral("*.mpg"), QStringLiteral("*.webm"),
        QStringLiteral("*.flv"), QStringLiteral("*.ts"),  QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
        QStringLiteral("*.png"), QStringLiteral("*.bmp"), QStringLiteral("*.webp"), QStringLiteral("*.gif"),
        QStringLiteral("*.mp3"), QStringLiteral("*.wav"), QStringLiteral("*.wma"), QStringLiteral("*.aac"),
        QStringLiteral("*.m4a"), QStringLiteral("*.ogg")
    };
    const QFileInfoList list = d.entryInfoList(filters, QDir::Files, QDir::Name);
    static const QStringList videoExts = {QStringLiteral("mp4"), QStringLiteral("wmv"), QStringLiteral("avi"),
                                          QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("m4v"),
                                          QStringLiteral("mpg"), QStringLiteral("webm"), QStringLiteral("flv"),
                                          QStringLiteral("ts")};
    static const QStringList audioExts = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("wma"),
                                          QStringLiteral("aac"), QStringLiteral("m4a"), QStringLiteral("ogg")};

    for (const QFileInfo& fi : list) {
        MediaItem m;
        m.name = fi.completeBaseName();
        m.filePath = fi.absoluteFilePath();
        const QString ext = fi.suffix().toLower();
        if (videoExts.contains(ext))
            m.type = MediaType::Video;
        else if (audioExts.contains(ext))
            m.type = MediaType::Audio;
        else
            m.type = MediaType::Image;
        m_data.addMediaItem(m);
    }
    refresh();
}

void MediaTab::onSelectionChanged()
{
    m_delete->setEnabled(!m_grid->selectedItems().isEmpty());
    auto opt = selectedMedia();
    if (opt)
        emit mediaSelected(*opt);
    else
        emit mediaCleared();
}

void MediaTab::setGridView(bool grid)
{
    if (m_isGridView == grid)
        return;
    m_isGridView = grid;

    AppSettings s = m_data.settings();
    s.isMediaGridView = grid;
    m_data.saveSettings(s);

    applyViewMode();
}

void MediaTab::applyViewMode()
{
    if (!m_grid)
        return;

    const int curRow = m_grid->currentRow();

    if (m_isGridView) {
        m_grid->setViewMode(QListView::IconMode);
        m_grid->setFlow(QListView::LeftToRight);
        m_grid->setWrapping(true);
        m_grid->setResizeMode(QListView::Adjust);
        m_grid->setMovement(QListView::Static);
        m_grid->setSpacing(0);
        m_grid->setUniformItemSizes(true);
    } else {
        m_grid->setViewMode(QListView::ListMode);
        m_grid->setFlow(QListView::TopToBottom);
        m_grid->setWrapping(false);
        m_grid->setResizeMode(QListView::Fixed);
        m_grid->setMovement(QListView::Static);
        m_grid->setSpacing(1);
        m_grid->setUniformItemSizes(false);
    }
    m_grid->reset();
    if (curRow >= 0 && curRow < m_grid->count()) {
        m_grid->setCurrentRow(curRow);
        m_grid->item(curRow)->setSelected(true);
    }
    m_grid->viewport()->update();
}

void MediaTab::onOptionsClicked()
{
    if (!m_options)
        return;

    QMenu menu(this);

    auto* gridAction = menu.addAction(QStringLiteral("Grid View (Thumbnails)"));
    gridAction->setCheckable(true);
    gridAction->setChecked(m_isGridView);

    auto* listAction = menu.addAction(QStringLiteral("List View"));
    listAction->setCheckable(true);
    listAction->setChecked(!m_isGridView);

    auto* group = new QActionGroup(&menu);
    group->addAction(gridAction);
    group->addAction(listAction);

    connect(gridAction, &QAction::triggered, this, [this] { setGridView(true); });
    connect(listAction, &QAction::triggered, this, [this] { setGridView(false); });

    const QPoint pos = m_options->mapToGlobal(QPoint(m_options->width() - menu.sizeHint().width(), -menu.sizeHint().height()));
    menu.exec(pos);
}

} // namespace Lux