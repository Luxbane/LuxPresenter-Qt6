#pragma once
#include "Models/AppSettings.h"
#include <QDialog>
#include <QStringList>
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
namespace Lux {
class AdBlockService;
// Options window (General / Songs / Online). Port of the reference non-Advanced options; the Advanced page of the
// reference (video engine, "Preview Live Output") is intentionally not offered. Every control edits one field of
// AppSettings; settings() returns the edited copy, the owner validates, saves and applies it.
class OptionsDialog : public QDialog
{
    Q_OBJECT
public:
    struct Monitor { QString deviceName; QString text; };
    // ds (optional) lets the Online page show the filter-list status and update the lists.
    OptionsDialog(const AppSettings& current, const QList<Monitor>& monitors, AdBlockService* ads = nullptr, QWidget* parent = nullptr);
    AppSettings settings() const;               // the edited values (sanitised)
    QStringList categories() const;
    void selectCategory(int index);
private:
    QWidget* buildGeneral();
    QWidget* buildSongs();
    QWidget* buildOnline();
    void updateEnabledStates();
    AppSettings m_base;
    AdBlockService* m_ads = nullptr;
    class QLabel* m_adStatus = nullptr;
    class QPushButton* m_adUpdate = nullptr;
    class QLabel* m_adResult = nullptr;
    QListWidget* m_list = nullptr;
    QStackedWidget* m_pages = nullptr;
    // General
    QRadioButton *m_light = nullptr, *m_dark = nullptr;
    QComboBox* m_outputDisplay = nullptr;
    QSpinBox *m_marginTop = nullptr, *m_marginBottom = nullptr, *m_marginLeft = nullptr, *m_marginRight = nullptr;
    QCheckBox* m_stageServerEnabled = nullptr;
    QLineEdit* m_stageUrlEdit = nullptr;
    QPushButton* m_stageCopyBtn = nullptr;
    QPushButton* m_stageOpenBtn = nullptr;
    // Songs
    QComboBox *m_textAlign = nullptr, *m_vertAlign = nullptr, *m_fontSize = nullptr;
    QFontComboBox* m_unused = nullptr;
    QComboBox* m_fontFamily = nullptr;
    QCheckBox *m_bold = nullptr;
    QCheckBox *m_showLabels = nullptr, *m_indent = nullptr, *m_autoWrap = nullptr, *m_capAll = nullptr,
              *m_capFirst = nullptr, *m_capKeywords = nullptr;
    QComboBox *m_labelLocation = nullptr, *m_labelSize = nullptr;
    QPlainTextEdit* m_keywords = nullptr;
    QComboBox *m_songTransition = nullptr, *m_slideTransition = nullptr, *m_screenTransition = nullptr, *m_duration = nullptr;
    // Online
    QCheckBox* m_adBlock = nullptr;
    QPlainTextEdit* m_whitelist = nullptr;
};
} // namespace Lux
