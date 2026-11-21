#pragma once

#include "Services/UpdateService.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTextBrowser;

namespace Lux {

class UpdateDialog : public QDialog
{
    Q_OBJECT
public:
    explicit UpdateDialog(const UpdateInfo& info, QWidget* parent = nullptr);
    ~UpdateDialog() override = default;

    QString releaseUrl() const { return m_info.releaseUrl; }

private:
    void setupUi();
    void applyTheme();

    UpdateInfo m_info;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_versionLabel = nullptr;
    QTextBrowser* m_browser = nullptr;
    QPushButton* m_laterBtn = nullptr;
    QPushButton* m_downloadBtn = nullptr;
};

} // namespace Lux
