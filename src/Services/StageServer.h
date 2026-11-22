#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace Lux {

struct StageState
{
    QString currentSlide;
    QString nextSlide;
    QString title;
    QString time;
    QString alertMessage;
    bool isBlack = false;
    bool isClear = false;
};

class StageServer : public QObject
{
    Q_OBJECT
public:
    explicit StageServer(quint16 port = 8080, QObject* parent = nullptr);
    ~StageServer() override;

    bool start(quint16 port = 8080);
    void stop();
    bool isRunning() const;
    quint16 serverPort() const { return m_port; }

    // Returns all detected active private/LAN IPv4 addresses (e.g. 192.168.x.x, 10.x.x.x)
    static QList<QString> getLocalIpAddresses();

    // Returns the primary local IPv4 address, or "127.0.0.1" if offline
    static QString primaryLocalIpAddress();

    // Full URL for operators/stage users, e.g. "http://192.168.1.50:8080/stage"
    QString stageUrl() const;

    // Updates the current confidence monitor payload broadcast to clients
    void updateState(const StageState& state);
    StageState currentState() const;

signals:
    void stateUpdated();

private slots:
    void onNewConnection();
    void onClientReadyRead();

private:
    void handleHttpRequest(QTcpSocket* socket, const QByteArray& requestData);
    QByteArray buildJsonResponse() const;
    QByteArray buildHtmlPage() const;

    QTcpServer* m_server = nullptr;
    quint16 m_port = 8080;
    StageState m_state;
};

} // namespace Lux
