#include "Views/Components/PresentationTab.h"

#include "Models/PresentationDocModels.h"
#include "Services/DataService.h"
#include "Services/PageImageCache.h"
#include "Services/ThemeService.h"
#include "Views/UiUtil.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace Lux {

namespace {

enum Role { PageRole = Qt::UserRole, LiveRole, PdfRole };

// 96x74 slide card: thumbnail (UniformToFill), "Slide N" badge top-left, green LIVE badge top-right.
class SlideCardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {106, 84}; }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        p->save();
        if (opt.state & QStyle::State_Selected)
            p->fillRect(opt.rect, QColor(0x38, 0xBD, 0xF8));
        const QRect card(opt.rect.left() + 5, opt.rect.top() + 5, 96, 74);
        p->fillRect(card, QColor(0x0B, 0x19, 0x2C));
        const QString pdf = idx.data(PdfRole).toString();
        if (!pdf.isEmpty()) {
            const qreal dpr = p->device()->devicePixelRatioF();
            const QRect inner = card.adjusted(1, 1, -1, -1);
            QImage img = PageImageCache::instance().page(pdf, idx.data(PageRole).toInt() - 1,
                                                         QSize(qRound(inner.width() * dpr * 1.5), qRound(inner.height() * dpr * 1.5)));
            if (!img.isNull()) {
                // UniformToFill: cover the card, crop the overflow
                const qreal k = qMax(inner.width() / qreal(img.width()), inner.height() / qreal(img.height()));
                const QSizeF src(inner.width() / k, inner.height() / k);
                p->setRenderHint(QPainter::SmoothPixmapTransform);
                p->drawImage(QRectF(inner), img, QRectF((img.width() - src.width()) / 2, (img.height() - src.height()) / 2, src.width(), src.height()));
            }
        }
        p->setPen(QPen(QColor(0x33, 0x41, 0x55), 1.5));
        p->setBrush(Qt::NoBrush);
        p->drawRect(card.adjusted(1, 1, -1, -1));
        p->setFont(tahoma(8, true));
        const QString badge = QStringLiteral("Slide %1").arg(idx.data(PageRole).toInt());
        const QRect badgeRect(card.left() + 1, card.top() + 1, p->fontMetrics().horizontalAdvance(badge) + 8, 13);
        p->fillRect(badgeRect, QColor(0x0F, 0x17, 0x2A, 0xD0));
        p->setPen(QColor(0x38, 0xBD, 0xF8));
        p->drawText(badgeRect, Qt::AlignCenter, badge);
        if (idx.data(LiveRole).toBool()) {
            const QRect live(card.right() - 28, card.top() + 1, 28, 13);
            p->fillRect(live, QColor(0x16, 0xA3, 0x4A));
            p->setPen(Qt::white);
            p->drawText(live, Qt::AlignCenter, QStringLiteral("LIVE"));
        }
        p->restore();
    }
};

} // namespace

PresentationTab::PresentationTab(DataService& data, PresentationService& service, QWidget* parent)
    : QWidget(parent), m_data(data), m_service(service)
{
    setObjectName(QStringLiteral("PanelBg"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 1. Document selector: [icon] [document v] [+] / filter box
    auto* top = new QFrame(this);
    top->setObjectName(QStringLiteral("SurfaceBarBottom"));
    auto* grid = new QGridLayout(top);
    grid->setContentsMargins(6, 4, 6, 4);
    grid->setHorizontalSpacing(0);
    grid->setVerticalSpacing(4);
    auto* icon = new QLabel(QStringLiteral("\U0001F4FD"), top);
    icon->setFont(segoeEmoji(14));
    icon->setContentsMargins(2, 0, 6, 0);
    m_docs = new QComboBox(top);
    m_docs->setFixedHeight(24);
    m_addSmall = new QPushButton(QStringLiteral("\u2795"), top);
    m_addSmall->setFixedSize(26, 24);
    m_addSmall->setStyleSheet(QStringLiteral("QPushButton{font-size:11px; padding:0;}"));
    m_addSmall->setToolTip(QStringLiteral("Import New Presentation"));
    m_filter = new QLineEdit(top);
    m_filter->setFixedHeight(22);
    m_filter->setPlaceholderText(QStringLiteral("Filter Documents..."));
    m_filter->setToolTip(QStringLiteral("Filter Documents..."));
    grid->addWidget(icon, 0, 0);
    grid->addWidget(m_docs, 0, 1);
    grid->addWidget(m_addSmall, 0, 2);
    grid->setColumnMinimumWidth(2, 30);
    grid->addWidget(m_filter, 1, 0, 1, 3);
    root->addWidget(top);

    // 2. Slide thumbnail grid
    m_slides = new QListWidget(this);
    m_slides->setObjectName(QStringLiteral("SlideGrid"));
    m_slides->setViewMode(QListView::IconMode);
    m_slides->setResizeMode(QListView::Adjust);
    m_slides->setMovement(QListView::Static);
    m_slides->setWrapping(true);
    m_slides->setSpacing(0);
    m_slides->setUniformItemSizes(true);
    m_slides->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_slides->setFrameShape(QFrame::NoFrame);
    m_slides->setItemDelegate(new SlideCardDelegate(m_slides));
    m_slides->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_slides, 1);

    m_loading = new QLabel(QStringLiteral("Loading Document..."), m_slides);
    m_loading->setAlignment(Qt::AlignCenter);
    m_loading->setStyleSheet(QStringLiteral("background:rgba(0,0,0,150); color:white; font-weight:600; font-size:11px;"));
    m_loading->hide();

    m_menu = new QMenu(this);
    m_aGoLive = m_menu->addAction(QString::fromUtf8("\u25B6 Go Live"));
    m_aAdd = m_menu->addAction(QString::fromUtf8("\u2795 Add Presentation to Schedule"));

    // 3. Bottom bar
    auto* bottom = new QFrame(this);
    bottom->setObjectName(QStringLiteral("SurfaceBarTop"));
    auto* bl = new QHBoxLayout(bottom);
    bl->setContentsMargins(6, 3, 6, 3);
    bl->setSpacing(4);
    m_import = new QPushButton(QStringLiteral("\u2795 Import Presentation"), bottom);
    m_delete = new QPushButton(QStringLiteral("\u2796 Delete Presentation"), bottom);
    for (QPushButton* b : {m_import, m_delete})
        b->setStyleSheet(QStringLiteral("QPushButton{font-size:10px; padding:2px 8px;}"));
    m_import->setToolTip(QStringLiteral("Import Presentation Document"));
    m_delete->setToolTip(QStringLiteral("Remove Selected Presentation"));
    m_delete->setEnabled(false);
    auto* module = new QLabel(QStringLiteral("Presentation Module"), bottom);
    module->setFont(tahoma(10));
    module->setStyleSheet(QStringLiteral("color:#64748B;"));
    // Both buttons keep their full text at the narrowest left column; the caption gives way first.
    for (QPushButton* b : {m_import, m_delete})
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 24);
    module->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // Both buttons keep their full text at the narrowest left column; the caption gives way first.
    for (QPushButton* b : {m_import, m_delete})
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 24);
    module->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bl->addWidget(m_import);
    bl->addWidget(m_delete);
    bl->addStretch(1);
    bl->addWidget(module);
    root->addWidget(bottom);

    connect(m_import, &QPushButton::clicked, this, &PresentationTab::importDocument);
    connect(m_addSmall, &QPushButton::clicked, this, &PresentationTab::importDocument);
    connect(m_delete, &QPushButton::clicked, this, &PresentationTab::deleteDocument);
    connect(m_docs, &QComboBox::currentIndexChanged, this, [this] { if (!m_quiet) onDocumentChosen(); });
    connect(m_filter, &QLineEdit::textChanged, this, [this] { fillCombo(m_doc.isValid() ? QString() : QString()); });
    connect(&m_service, &PresentationService::opened, this, &PresentationTab::onOpened);
    connect(&m_service, &PresentationService::failed, this, &PresentationTab::onFailed);

    // Click stages the slide in Preview, double-click goes live (PresentationViewModel).
    connect(m_slides, &QListWidget::itemPressed, this, [this](QListWidgetItem* it) {
        if (m_doc.isValid() && it)
            emit slideStaged(m_doc, m_slides->row(it));
    });
    connect(m_slides, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_quiet && row >= 0 && m_doc.isValid())
            emit slideStaged(m_doc, row);   // keyboard navigation of the grid
    });
    connect(m_slides, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (m_doc.isValid() && it)
            emit slideActivated(m_doc, m_slides->row(it));
    });
    connect(m_slides, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_doc.isValid())
            return;
        if (QListWidgetItem* it = m_slides->itemAt(pos))
            m_slides->setCurrentItem(it);
        m_menu->popup(m_slides->viewport()->mapToGlobal(pos));
    });
    connect(m_aGoLive, &QAction::triggered, this, [this] {
        if (m_doc.isValid())
            emit slideActivated(m_doc, qMax(0, m_slides->currentRow()));
    });
    connect(m_aAdd, &QAction::triggered, this, [this] {
        if (m_doc.isValid())
            emit addToScheduleRequested(m_doc);
    });

    refresh();
}

void PresentationTab::fillCombo(const QString& selectId)
{
    // the selected document stays selected while a filter hides it from the list
    const QString keep = !selectId.isEmpty() ? selectId : (m_docs->currentIndex() >= 0 ? m_docs->currentData().toString() : m_selectedId);
    const QString f = m_filter->text().trimmed();
    const bool wasQuiet = m_quiet;
    m_quiet = true;
    m_docs->clear();
    for (const PresentationDocItem& d : m_data.presentationDocs()) {
        if (!f.isEmpty() && !d.name.contains(f, Qt::CaseInsensitive) && !d.filePath.contains(f, Qt::CaseInsensitive))
            continue;
        m_docs->addItem(d.name, d.id);
    }
    const int idx = m_docs->findData(keep);
    m_docs->setCurrentIndex(idx);          // -1 when nothing is selected
    m_quiet = wasQuiet;
    m_selectedId = idx >= 0 ? keep : (m_docs->count() > 0 || !f.isEmpty() ? m_selectedId : QString());
    m_delete->setEnabled(m_docs->currentIndex() >= 0);
}

void PresentationTab::refresh()
{
    fillCombo();
}

bool PresentationTab::importPath(const QString& path)
{
    m_message.clear();
    if (PresentationService::isLegacyPowerPoint(path)) {
        m_message = QStringLiteral("Legacy PowerPoint 97-2003 binary formats (.ppt / .pps) require conversion.\n\n"
                                   "For optimal fidelity and presentation display, please save the presentation as modern .pptx "
                                   "or use LibreOffice Impress.");
        emit messageRequested(QStringLiteral("Legacy Binary Format (.ppt)"), m_message, false);
        return false;
    }
    if (!QFileInfo::exists(path)) {
        m_message = QStringLiteral("The file was not found: %1").arg(QDir::toNativeSeparators(path));
        emit messageRequested(QStringLiteral("Import Error"), QStringLiteral("Failed to import presentation: ") + m_message, true);
        return false;
    }
    if (!PresentationService::isSupportedFile(path)) {
        m_message = QStringLiteral("Format '.%1' is not supported. Please import PPTX, PPSX, ODP, or PDF.").arg(QFileInfo(path).suffix().toLower());
        emit messageRequested(QStringLiteral("Import Error"), QStringLiteral("Failed to import presentation: ") + m_message, true);
        return false;
    }
    m_pendingPath = path;
    m_importing = true;
    m_doc = PresentationService::Document();
    m_slides->clear();
    m_loading->resize(m_slides->size());
    m_loading->show();
    m_service.open(path);
    return true;
}

void PresentationTab::onOpened(const PresentationService::Document& doc)
{
    const bool importing = QFileInfo(doc.filePath) == QFileInfo(m_pendingPath) && !m_pendingPath.isEmpty();
    if (!importing)
        return;
    m_pendingPath.clear();
    m_loading->hide();
    m_doc = doc;
    // library entry (an import adds it, a selection of an existing entry leaves it as it is)
    QString id;
    for (const PresentationDocItem& d : m_data.presentationDocs())
        if (QFileInfo(d.filePath) == QFileInfo(doc.filePath))
            id = d.id;
    if (id.isEmpty() || m_importing) {
        PresentationDocItem item;
        item.name = doc.title;
        item.filePath = doc.filePath;
        item.fileExtension = QLatin1Char('.') + QFileInfo(doc.filePath).suffix().toLower();
        item.slideCount = doc.pageCount;
        item.dateAdded = QDateTime::currentDateTime();
        if (!id.isEmpty())
            item.id = id;
        m_data.addPresentationDoc(item);
        id = item.id;
    }
    m_importing = false;
    m_quiet = true;
    if (!m_filter->text().isEmpty())
        m_filter->clear();
    fillCombo(id);
    m_quiet = false;
    fillGrid();
}

void PresentationTab::onFailed(const QString& filePath, const QString& message)
{
    if (m_pendingPath.isEmpty() || QFileInfo(filePath) != QFileInfo(m_pendingPath))
        return;
    m_pendingPath.clear();
    m_importing = false;
    m_loading->hide();
    m_message = message;
    m_doc = PresentationService::Document();
    m_slides->clear();
    emit messageRequested(QStringLiteral("Error Loading Slides"), QStringLiteral("Failed to load slides: ") + message, true);
}

void PresentationTab::onDocumentChosen()
{
    m_delete->setEnabled(m_docs->currentIndex() >= 0);
    const QString id = m_docs->currentData().toString();
    m_selectedId = id;
    for (const PresentationDocItem& d : m_data.presentationDocs()) {
        if (d.id == id) {
            m_pendingPath = d.filePath;
            m_doc = PresentationService::Document();
            m_slides->clear();
            m_loading->resize(m_slides->size());
            m_loading->show();
            m_service.open(d.filePath);
            return;
        }
    }
}

void PresentationTab::selectDocumentByPath(const QString& filePath)
{
    for (int i = 0; i < m_docs->count(); ++i) {
        for (const PresentationDocItem& d : m_data.presentationDocs()) {
            if (d.id == m_docs->itemData(i).toString() && QFileInfo(d.filePath) == QFileInfo(filePath)) {
                m_quiet = true;
                m_docs->setCurrentIndex(i);
                m_quiet = false;
                onDocumentChosen();           // (re)load even when the entry was already current
                return;
            }
        }
    }
}

void PresentationTab::fillGrid()
{
    m_quiet = true;
    m_slides->clear();
    for (int i = 0; i < m_doc.pageCount; ++i) {
        auto* it = new QListWidgetItem(m_slides);
        it->setData(PageRole, i + 1);
        it->setData(LiveRole, i == m_livePage);
        it->setData(PdfRole, m_doc.pdfPath);
    }
    m_quiet = false;
}

void PresentationTab::setSelectedPage(int page)
{
    if (page < 0 || page >= m_slides->count())
        return;
    m_quiet = true;
    m_slides->setCurrentRow(page);
    m_slides->scrollToItem(m_slides->item(page));
    m_quiet = false;
}

void PresentationTab::markLive(int page)
{
    m_livePage = page;
    for (int i = 0; i < m_slides->count(); ++i)
        m_slides->item(i)->setData(LiveRole, i == page);
    m_slides->viewport()->update();
}

bool PresentationTab::removeDocument(const QString& docId)
{
    const bool current = m_docs->currentIndex() >= 0 && m_docs->currentData().toString() == docId;
    QString filePath;
    for (const PresentationDocItem& d : m_data.presentationDocs()) {
        if (d.id == docId) {
            filePath = d.filePath;
            break;
        }
    }
    const bool ok = m_data.deletePresentationDoc(docId);
    if (!filePath.isEmpty()) {
        PresentationService::clearCacheFor(filePath);
    }
    if (current || m_selectedId == docId) {
        m_doc = PresentationService::Document();
        m_slides->clear();
        m_livePage = -1;
        m_selectedId.clear();
    }
    fillCombo();
    return ok;
}

void PresentationTab::importDocument()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import Presentation Document into LuxPresenter"), QString(),
                                                      PresentationService::supportedFilter());
    if (path.isEmpty())
        return;
    importPath(path);
}

void PresentationTab::deleteDocument()
{
    const QString id = m_docs->currentData().toString();
    if (id.isEmpty())
        return;
    const auto res = QMessageBox::question(this, QStringLiteral("Confirm Deletion"),
                                           QStringLiteral("Are you sure you want to remove '%1' from the presentation library?").arg(m_docs->currentText()),
                                           QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes)
        removeDocument(id);
}

} // namespace Lux
