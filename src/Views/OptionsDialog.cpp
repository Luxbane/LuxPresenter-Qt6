#include "Views/OptionsDialog.h"
#include "Services/AdBlockService.h"
#include "Services/StageServer.h"
#include "Services/ThemeService.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QLineEdit>
#include <QUrl>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
namespace Lux {
namespace {
void selectData(QComboBox* c, const QString& data)
{
    int i = c->findData(data, Qt::DisplayRole, Qt::MatchFixedString);
    if (i < 0)
        i = c->findData(data);
    if (i >= 0)
        c->setCurrentIndex(i);
}
QComboBox* combo(const QString& name, const QList<QPair<QString, QString>>& items)
{
    auto* c = new QComboBox;
    c->setObjectName(name);
    for (const auto& it : items)
        c->addItem(it.first, it.second);
    return c;
}
QCheckBox* check(const QString& name, const QString& text, bool on)
{
    auto* c = new QCheckBox(text);
    c->setObjectName(name);
    c->setChecked(on);
    return c;
}
QSpinBox* margin(const QString& name, int v)
{
    auto* s = new QSpinBox;
    s->setObjectName(name);
    s->setRange(-20, 40);
    s->setSuffix(QStringLiteral(" %"));
    s->setValue(v);
    return s;
}

QWidget* buildMonitorGraphic(QWidget* parent)
{
    auto* container = new QWidget(parent);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignCenter);

    // Monitor frame (outer bezel)
    auto* screenFrame = new QFrame(container);
    screenFrame->setFixedSize(130, 80);
    screenFrame->setStyleSheet(QStringLiteral(
        "QFrame { background: #111111; border: 3px solid #333333; border-radius: 4px; }"
    ));
    auto* screenInner = new QVBoxLayout(screenFrame);
    screenInner->setContentsMargins(3, 3, 3, 3);
    screenInner->setAlignment(Qt::AlignCenter);

    auto* screenBg = new QFrame(screenFrame);
    screenBg->setStyleSheet(QStringLiteral("QFrame { background: #001833; border: none; }"));
    auto* sampleText = new QLabel(QStringLiteral("LORD YOU ARE BEAUTIFUL,\nI RAISE MY HANDS TO YOU,\nNO ONE ELSE CAN TOUCH\nMY HEART LIKE YOU DO."), screenBg);
    sampleText->setAlignment(Qt::AlignCenter);
    sampleText->setWordWrap(true);
    sampleText->setStyleSheet(QStringLiteral(
        "QLabel { color: #FFFFFF; font-family: Tahoma, 'Segoe UI'; font-size: 7.5pt; font-weight: bold; background: transparent; line-height: 10px; }"
    ));
    auto* bgLay = new QVBoxLayout(screenBg);
    bgLay->setContentsMargins(2, 2, 2, 2);
    bgLay->addWidget(sampleText);
    screenInner->addWidget(screenBg);
    lay->addWidget(screenFrame, 0, Qt::AlignCenter);

    // Stand stem
    auto* stem = new QWidget(container);
    stem->setFixedSize(16, 12);
    stem->setStyleSheet(QStringLiteral("background: #333333; border: none;"));
    lay->addWidget(stem, 0, Qt::AlignCenter);

    // Stand base
    auto* base = new QWidget(container);
    base->setFixedSize(65, 8);
    base->setStyleSheet(QStringLiteral("background: #222222; border-radius: 4px;"));
    lay->addWidget(base, 0, Qt::AlignCenter);

    return container;
}
} // namespace
OptionsDialog::OptionsDialog(const AppSettings& current, const QList<Monitor>& monitors, AdBlockService* ads, QWidget* parent)
    : QDialog(parent), m_base(current), m_ads(ads)
{
    setObjectName(QStringLiteral("OptionsDialog"));
    setWindowTitle(QStringLiteral("Options"));
    setModal(true);
    resize(740, 560);
    m_base.sanitize();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    // Left sidebar (Category list in bordered box + monitor preview graphic)
    auto* leftCol = new QVBoxLayout;
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftCol->setSpacing(10);

    auto* catBox = new QFrame;
    catBox->setObjectName(QStringLiteral("CategoryBox"));
    catBox->setFixedWidth(150);
    auto* catLay = new QVBoxLayout(catBox);
    catLay->setContentsMargins(0, 0, 0, 0);
    catLay->setSpacing(0);

    m_list = new QListWidget(catBox);
    m_list->setObjectName(QStringLiteral("OptionsCategories"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setFixedWidth(148);
    m_list->setFixedHeight(3 * 38);
    m_list->setSpacing(0);
    catLay->addWidget(m_list);
    leftCol->addWidget(catBox);

    leftCol->addStretch(1);

    auto* monitorWidget = buildMonitorGraphic(this);
    leftCol->addWidget(monitorWidget, 0, Qt::AlignCenter);

    body->addLayout(leftCol);

    // Right settings panel wrapped in a bordered card
    auto* rightCard = new QFrame(this);
    rightCard->setObjectName(QStringLiteral("SettingsCard"));
    auto* rightLay = new QVBoxLayout(rightCard);
    rightLay->setContentsMargins(14, 14, 14, 14);

    m_pages = new QStackedWidget(rightCard);
    for (const auto& p : {std::make_pair(QStringLiteral("General"), buildGeneral()),
                          std::make_pair(QStringLiteral("Songs"), buildSongs()),
                          std::make_pair(QStringLiteral("Online"), buildOnline())}) {
        m_list->addItem(p.first);
        m_pages->addWidget(p.second);
    }
    connect(m_list, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_list->setCurrentRow(0);

    rightLay->addWidget(m_pages);
    body->addWidget(rightCard, 1);
    root->addLayout(body, 1);

    // Bottom action bar (Preview Live Output on left, enlarged OK/Cancel on right)
    auto* bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(2, 4, 2, 2);

    auto* previewLive = new QCheckBox(QStringLiteral("Preview Live Output"), this);
    previewLive->setChecked(false);
    bottomRow->addWidget(previewLive);
    bottomRow->addStretch(1);

    auto* buttons = new QDialogButtonBox(this);
    auto* okBtn = buttons->addButton(QDialogButtonBox::Ok);
    auto* cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);

    okBtn->setFixedSize(88, 28);
    cancelBtn->setFixedSize(88, 28);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    bottomRow->addWidget(buttons);
    root->addLayout(bottomRow);
    // output display choices
    selectData(m_outputDisplay, QString());
    for (const Monitor& m : monitors)
        m_outputDisplay->addItem(m.text, m.deviceName);
    if (!m_base.outputDisplay.isEmpty()) {
        int i = m_outputDisplay->findData(m_base.outputDisplay);
        if (i < 0) {
            m_outputDisplay->addItem(QStringLiteral("%1 (not connected)").arg(m_base.outputDisplay), m_base.outputDisplay);
            i = m_outputDisplay->count() - 1;
        }
        m_outputDisplay->setCurrentIndex(i);
    }
    updateEnabledStates();
}
QStringList OptionsDialog::categories() const
{
    QStringList l;
    for (int i = 0; i < m_list->count(); ++i)
        l << m_list->item(i)->text();
    return l;
}
void OptionsDialog::selectCategory(int index) { m_list->setCurrentRow(index); }
QWidget* OptionsDialog::buildGeneral()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("General"), w);
    title->setObjectName(QStringLiteral("OptionsPageTitle"));
    v->addWidget(title);

    auto* theme = new QGroupBox(QStringLiteral("Theme"));
    auto* tl = new QHBoxLayout(theme);
    m_light = new QRadioButton(QStringLiteral("Light"));
    m_dark = new QRadioButton(QStringLiteral("Dark"));
    m_light->setObjectName(QStringLiteral("optThemeLight"));
    m_dark->setObjectName(QStringLiteral("optThemeDark"));
    (m_base.isDarkMode ? m_dark : m_light)->setChecked(true);
    tl->addWidget(m_light);
    tl->addWidget(m_dark);
    tl->addStretch(1);
    v->addWidget(theme);
    auto* out = new QGroupBox(QStringLiteral("Projector output"));
    auto* ol = new QFormLayout(out);
    m_outputDisplay = new QComboBox;
    m_outputDisplay->setObjectName(QStringLiteral("optOutputDisplay"));
    m_outputDisplay->addItem(QStringLiteral("Automatic (prefer secondary display)"), QString());
    ol->addRow(QStringLiteral("Output display:"), m_outputDisplay);
    v->addWidget(out);
    auto* mg = new QGroupBox(QStringLiteral("Screen margin (distance of the text from the edges of the output)"));
    auto* ml = new QFormLayout(mg);
    m_marginTop = margin(QStringLiteral("optMarginTop"), m_base.marginTop);
    m_marginBottom = margin(QStringLiteral("optMarginBottom"), m_base.marginBottom);
    m_marginLeft = margin(QStringLiteral("optMarginLeft"), m_base.marginLeft);
    m_marginRight = margin(QStringLiteral("optMarginRight"), m_base.marginRight);
    ml->addRow(QStringLiteral("Top:"), m_marginTop);
    ml->addRow(QStringLiteral("Bottom:"), m_marginBottom);
    ml->addRow(QStringLiteral("Left:"), m_marginLeft);
    ml->addRow(QStringLiteral("Right:"), m_marginRight);
    v->addWidget(mg);

    auto* stageBox = new QGroupBox(QStringLiteral("Stage Display (Confidence Monitor)"));
    auto* sbl = new QVBoxLayout(stageBox);
    sbl->setSpacing(8);
    auto* desc = new QLabel(QStringLiteral("Worship leaders, singers, and stage TVs can access the live confidence monitor in their browser over Wi-Fi without any HDMI splitter:"), stageBox);
    desc->setWordWrap(true);
    sbl->addWidget(desc);

    auto* urlRow = new QHBoxLayout;
    const QString stageUrl = QStringLiteral("http://%1:8080/stage").arg(StageServer::primaryLocalIpAddress());
    auto* urlEdit = new QLineEdit(stageUrl, stageBox);
    urlEdit->setReadOnly(true);
    urlRow->addWidget(urlEdit, 1);

    auto* copyBtn = new QPushButton(QStringLiteral("Copy Link"), stageBox);
    connect(copyBtn, &QPushButton::clicked, urlEdit, [urlEdit] {
        QGuiApplication::clipboard()->setText(urlEdit->text());
    });
    urlRow->addWidget(copyBtn);

    auto* openBtn = new QPushButton(QStringLiteral("Open in Browser"), stageBox);
    connect(openBtn, &QPushButton::clicked, urlEdit, [urlEdit] {
        QDesktopServices::openUrl(QUrl(urlEdit->text()));
    });
    urlRow->addWidget(openBtn);
    sbl->addLayout(urlRow);

    v->addWidget(stageBox);
    v->addStretch(1);
    return w;
}
QWidget* OptionsDialog::buildSongs()
{
    auto* w = new QWidget;
    w->setObjectName(QStringLiteral("SongsContent"));
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Songs"), w);
    title->setObjectName(QStringLiteral("OptionsPageTitle"));
    v->addWidget(title);

    // formatting
    auto* fmt = new QGroupBox(QStringLiteral("Text"));
    auto* f = new QFormLayout(fmt);
    m_textAlign = combo(QStringLiteral("optTextAlign"), {{QStringLiteral("Left"), QStringLiteral("Left")}, {QStringLiteral("Center"), QStringLiteral("Center")}, {QStringLiteral("Right"), QStringLiteral("Right")}});
    m_vertAlign = combo(QStringLiteral("optVertAlign"), {{QStringLiteral("Top"), QStringLiteral("Top")}, {QStringLiteral("Center"), QStringLiteral("Center")}, {QStringLiteral("Bottom"), QStringLiteral("Bottom")}});
    selectData(m_textAlign, m_base.songTextAlignment);
    selectData(m_vertAlign, m_base.songVerticalAlignment);
    m_fontFamily = new QComboBox;
    m_fontFamily->setObjectName(QStringLiteral("optFontFamily"));
    QStringList fonts = AppSettings::fontFamilies();
    if (!fonts.contains(m_base.songFontFamily, Qt::CaseInsensitive))
        fonts.prepend(m_base.songFontFamily);
    m_fontFamily->addItems(fonts);
    m_fontFamily->setCurrentIndex(std::max(0, m_fontFamily->findText(m_base.songFontFamily, Qt::MatchFixedString)));
    m_fontSize = new QComboBox;
    m_fontSize->setObjectName(QStringLiteral("optFontSize"));
    for (int s = 6; s <= 96; s += (s < 24 ? 1 : 4))
        m_fontSize->addItem(QString::number(s), s);
    if (m_fontSize->findData(m_base.songFontSize) < 0)
        m_fontSize->addItem(QString::number(m_base.songFontSize), m_base.songFontSize);
    m_fontSize->setCurrentIndex(m_fontSize->findData(m_base.songFontSize));
    m_bold = check(QStringLiteral("optBold"), QStringLiteral("Bold (forces bold on every song)"), m_base.songFontBold);
    f->addRow(QStringLiteral("Text alignment:"), m_textAlign);
    f->addRow(QStringLiteral("Vertical alignment:"), m_vertAlign);
    f->addRow(QStringLiteral("Font:"), m_fontFamily);
    f->addRow(QStringLiteral("Font size:"), m_fontSize);
    f->addRow(QString(), m_bold);
    v->addWidget(fmt);
    // labels
    auto* lab = new QGroupBox(QStringLiteral("Verse / chorus labels"));
    auto* l = new QFormLayout(lab);
    m_showLabels = check(QStringLiteral("optShowLabels"), QStringLiteral("Show verse labels"), m_base.showVerseLabels);
    QList<QPair<QString, QString>> locs;
    for (const QString& s : AppSettings::labelLocations())
        locs.append({s, s});
    QList<QPair<QString, QString>> sizes;
    for (const QString& s : AppSettings::labelSizes())
        sizes.append({s, s});
    m_labelLocation = combo(QStringLiteral("optLabelLocation"), locs);
    m_labelSize = combo(QStringLiteral("optLabelSize"), sizes);
    selectData(m_labelLocation, m_base.labelLocation);
    selectData(m_labelSize, m_base.labelSize);
    m_indent = check(QStringLiteral("optIndentLabel"), QStringLiteral("Indent label from the edge"), m_base.indentLabel);
    l->addRow(m_showLabels);
    l->addRow(QStringLiteral("Location:"), m_labelLocation);
    l->addRow(QStringLiteral("Size:"), m_labelSize);
    l->addRow(m_indent);
    v->addWidget(lab);
    // words
    auto* words = new QGroupBox(QStringLiteral("Word formatting"));
    auto* wl = new QVBoxLayout(words);
    m_autoWrap = check(QStringLiteral("optAutoWrap"), QStringLiteral("Wrap long lines automatically"), m_base.autoWrapWords);
    m_capAll = check(QStringLiteral("optCapAll"), QStringLiteral("Capitalise all words"), m_base.capitalizeAllWords);
    m_capFirst = check(QStringLiteral("optCapFirst"), QStringLiteral("Capitalise the first character of each line"), m_base.capitalizeFirstCharEachLine);
    m_capKeywords = check(QStringLiteral("optCapKeywords"), QStringLiteral("Always capitalise these words (one per line):"), m_base.autoCapitalizeKeywords);
    m_keywords = new QPlainTextEdit(m_base.capitalizeKeywords);
    m_keywords->setObjectName(QStringLiteral("optKeywords"));
    m_keywords->setMaximumHeight(70);
    for (QWidget* c : {static_cast<QWidget*>(m_autoWrap), static_cast<QWidget*>(m_capAll), static_cast<QWidget*>(m_capFirst),
                       static_cast<QWidget*>(m_capKeywords), static_cast<QWidget*>(m_keywords)})
        wl->addWidget(c);
    v->addWidget(words);
    // transitions
    auto* tr = new QGroupBox(QStringLiteral("Transitions"));
    auto* t = new QFormLayout(tr);
    const QList<QPair<QString, QString>> kinds{{QStringLiteral("Blend (fade)"), QStringLiteral("Blend")}, {QStringLiteral("Cut"), QStringLiteral("Cut")}};
    m_songTransition = combo(QStringLiteral("optSongTransition"), kinds);
    m_slideTransition = combo(QStringLiteral("optSlideTransition"), kinds);
    m_screenTransition = combo(QStringLiteral("optScreenTransition"), kinds);
    m_duration = combo(QStringLiteral("optDuration"), {{QStringLiteral("Fast (0.25 s)"), QStringLiteral("250")}, {QStringLiteral("Normal (0.5 s)"), QStringLiteral("500")},
                                                      {QStringLiteral("Smooth (0.75 s)"), QStringLiteral("750")}, {QStringLiteral("Slow (1 s)"), QStringLiteral("1000")}});
    selectData(m_songTransition, m_base.songTransition);
    selectData(m_slideTransition, m_base.slideTransition);
    selectData(m_screenTransition, m_base.screenTransition);
    selectData(m_duration, QString::number(m_base.transitionDurationMs));
    t->addRow(QStringLiteral("Background change:"), m_songTransition);
    t->addRow(QStringLiteral("Slide text change:"), m_slideTransition);
    t->addRow(QStringLiteral("Black / Logo / Clear:"), m_screenTransition);
    t->addRow(QStringLiteral("Duration:"), m_duration);
    v->addWidget(tr);
    v->addStretch(1);
    connect(m_showLabels, &QCheckBox::toggled, this, [this] { updateEnabledStates(); });
    connect(m_labelLocation, &QComboBox::currentIndexChanged, this, [this] { updateEnabledStates(); });
    connect(m_capKeywords, &QCheckBox::toggled, this, [this] { updateEnabledStates(); });
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; } QWidget#SongsContent { background: transparent; }"));
    scroll->setWidget(w);
    return scroll;
}
QWidget* OptionsDialog::buildOnline()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Online"), w);
    title->setObjectName(QStringLiteral("OptionsPageTitle"));
    v->addWidget(title);

    auto* g = new QGroupBox(QStringLiteral("uBlock Origin (Online browser)"));
    auto* gl = new QVBoxLayout(g);
    m_adBlock = check(QStringLiteral("optAdBlock"), QStringLiteral("Enable uBlock Origin in the Online browser"), m_base.adBlockEnabled);
    gl->addWidget(m_adBlock);

    auto* desc = new QLabel(QStringLiteral(
        "LuxPresenter uses the full uBlock Origin 1.76.2 browser extension (Manifest V2) "
        "to block website advertisements, banners, pop-ups, and trackers.\n\n"
        "Filter lists, custom rules, and site exceptions are managed directly by uBlock Origin "
        "and persist in the application profile. Website cookies and cache are automatically cleaned up when LuxPresenter exits."));
    desc->setObjectName(QStringLiteral("OptionsDescText"));
    desc->setWordWrap(true);
    gl->addWidget(desc);

    gl->addWidget(new QLabel(QStringLiteral("Sites on which blocking is switched off (one host name per line):")));
    m_whitelist = new QPlainTextEdit(m_base.adBlockWhitelist.join(QLatin1Char('\n')));
    m_whitelist->setObjectName(QStringLiteral("optWhitelist"));
    gl->addWidget(m_whitelist);
    v->addWidget(g);

    v->addStretch(1);
    return w;
}
void OptionsDialog::updateEnabledStates()
{
    const bool labels = m_showLabels->isChecked();
    m_labelLocation->setEnabled(labels);
    m_labelSize->setEnabled(labels);
    // the indent moves a left/right label away from the edge; a centred label has nothing to indent from
    const bool sideLabel = !m_labelLocation->currentText().endsWith(QStringLiteral("Center"));
    m_indent->setEnabled(labels && sideLabel);
    m_keywords->setEnabled(m_capKeywords->isChecked());
}
AppSettings OptionsDialog::settings() const
{
    AppSettings s = m_base;
    s.isDarkMode = m_dark->isChecked();
    s.outputDisplay = m_outputDisplay->currentData().toString();
    s.marginTop = m_marginTop->value();
    s.marginBottom = m_marginBottom->value();
    s.marginLeft = m_marginLeft->value();
    s.marginRight = m_marginRight->value();
    s.songTextAlignment = m_textAlign->currentData().toString();
    s.songVerticalAlignment = m_vertAlign->currentData().toString();
    s.songFontFamily = m_fontFamily->currentText();
    s.songFontSize = m_fontSize->currentData().toInt();
    s.songFontBold = m_bold->isChecked();
    s.showVerseLabels = m_showLabels->isChecked();
    s.labelLocation = m_labelLocation->currentData().toString();
    s.labelSize = m_labelSize->currentData().toString();
    s.indentLabel = m_indent->isChecked();
    s.autoWrapWords = m_autoWrap->isChecked();
    s.capitalizeAllWords = m_capAll->isChecked();
    s.capitalizeFirstCharEachLine = m_capFirst->isChecked();
    s.autoCapitalizeKeywords = m_capKeywords->isChecked();
    s.capitalizeKeywords = m_keywords->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts).join(QStringLiteral("\r\n"));
    s.songTransition = m_songTransition->currentData().toString();
    s.slideTransition = m_slideTransition->currentData().toString();
    s.screenTransition = m_screenTransition->currentData().toString();
    s.transitionDurationMs = m_duration->currentData().toInt();
    s.adBlockEnabled = m_adBlock->isChecked();
    s.adBlockWhitelist = m_whitelist->toPlainText().split(QRegularExpression(QStringLiteral("[\\r\\n,;\\s]+")), Qt::SkipEmptyParts);
    s.sanitize();
    return s;
}
} // namespace Lux