#include "Services/StageServer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>

namespace Lux {

StageServer::StageServer(quint16 port, QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_port(port)
{
    connect(m_server, &QTcpServer::newConnection, this, &StageServer::onNewConnection);
    start(port);
}

StageServer::~StageServer()
{
    stop();
}

bool StageServer::start(quint16 port)
{
    if (isRunning())
        return true;

    // Try binding to 0.0.0.0 (in-addr any) so all network interfaces (Wi-Fi, LAN, hotspot) can connect.
    for (quint16 p = port; p < port + 10; ++p) {
        if (m_server->listen(QHostAddress::Any, p)) {
            m_port = p;
            return true;
        }
    }
    return false;
}

void StageServer::stop()
{
    if (m_server && m_server->isListening()) {
        m_server->close();
    }
}

bool StageServer::isRunning() const
{
    return m_server && m_server->isListening();
}

QList<QString> StageServer::getLocalIpAddresses()
{
    QList<QString> privateIps;
    QList<QString> otherIps;

    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;

        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            const QString s = ip.toString();
            // Match private network ranges: 192.168.x.x, 10.x.x.x, 172.16.x.x - 172.31.x.x
            if (s.startsWith(QStringLiteral("192.168.")) || s.startsWith(QStringLiteral("10."))) {
                privateIps.append(s);
            } else if (s.startsWith(QStringLiteral("172."))) {
                const QStringList parts = s.split(QLatin1Char('.'));
                if (parts.size() >= 2) {
                    const int sec = parts[1].toInt();
                    if (sec >= 16 && sec <= 31)
                        privateIps.append(s);
                    else
                        otherIps.append(s);
                }
            } else if (!s.startsWith(QStringLiteral("169.254.")) && !s.startsWith(QStringLiteral("127."))) {
                otherIps.append(s);
            }
        }
    }

    return !privateIps.isEmpty() ? privateIps : (!otherIps.isEmpty() ? otherIps : QList<QString>{QStringLiteral("127.0.0.1")});
}

QString StageServer::primaryLocalIpAddress()
{
    const auto ips = getLocalIpAddresses();
    return !ips.isEmpty() ? ips.first() : QStringLiteral("127.0.0.1");
}

QString StageServer::stageUrl() const
{
    return QStringLiteral("http://%1:%2/stage").arg(primaryLocalIpAddress()).arg(m_port);
}

void StageServer::updateState(const StageState& state)
{
    m_state = state;
    emit stateUpdated();
}

StageState StageServer::currentState() const
{
    return m_state;
}

void StageServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        if (!socket)
            continue;

        connect(socket, &QTcpSocket::readyRead, this, &StageServer::onClientReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void StageServer::onClientReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    const QByteArray data = socket->readAll();
    handleHttpRequest(socket, data);
}

void StageServer::handleHttpRequest(QTcpSocket* socket, const QByteArray& requestData)
{
    const QString requestStr = QString::fromUtf8(requestData);
    const QString firstLine = requestStr.section(QLatin1Char('\n'), 0, 0).trimmed();
    const QStringList tokens = firstLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    if (tokens.size() < 2) {
        socket->disconnectFromHost();
        return;
    }

    const QString method = tokens[0];
    const QString path = tokens[1];

    if (path.startsWith(QStringLiteral("/api/stage"))) {
        const QByteArray body = buildJsonResponse();
        QByteArray response;
        response += "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: application/json; charset=utf-8\r\n";
        response += "Access-Control-Allow-Origin: *\r\n";
        response += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
        response += "Connection: close\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "\r\n";
        response += body;

        socket->write(response);
        socket->disconnectFromHost();
        return;
    }

    if (path == QStringLiteral("/") || path.startsWith(QStringLiteral("/stage"))) {
        const QByteArray body = buildHtmlPage();
        QByteArray response;
        response += "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/html; charset=utf-8\r\n";
        response += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
        response += "Connection: close\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "\r\n";
        response += body;

        socket->write(response);
        socket->disconnectFromHost();
        return;
    }

    // 404 Not Found
    const QByteArray notFoundBody = "<html><body><h1>404 Not Found</h1></body></html>";
    QByteArray response;
    response += "HTTP/1.1 404 Not Found\r\n";
    response += "Content-Type: text/html\r\n";
    response += "Connection: close\r\n";
    response += "Content-Length: " + QByteArray::number(notFoundBody.size()) + "\r\n";
    response += "\r\n";
    response += notFoundBody;

    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray StageServer::buildJsonResponse() const
{
    QJsonObject obj;
    obj[QStringLiteral("currentSlide")] = m_state.currentSlide;
    obj[QStringLiteral("nextSlide")] = m_state.nextSlide;
    obj[QStringLiteral("title")] = m_state.title;
    obj[QStringLiteral("time")] = !m_state.time.isEmpty() ? m_state.time : QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    obj[QStringLiteral("alertMessage")] = m_state.alertMessage;
    obj[QStringLiteral("isBlack")] = m_state.isBlack;
    obj[QStringLiteral("isClear")] = m_state.isClear;

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray StageServer::buildHtmlPage() const
{
    // High-contrast, responsive Single Page Application designed for stage monitors (mobile, tablet, Smart TV).
    const char* html = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>LuxPresenter - Stage Display</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background-color: #05070B;
      color: #FFFFFF;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      height: 100vh;
      display: flex;
      flex-direction: column;
      overflow: hidden;
      user-select: none;
    }
    header {
      background: #0D1117;
      border-bottom: 2px solid #21262D;
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 10px 24px;
      font-weight: 600;
    }
    .song-title {
      color: #38BDF8;
      font-size: clamp(16px, 2.5vw, 24px);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      max-width: 65%;
    }
    .clock-badge {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .clock {
      color: #F8FAFC;
      font-size: clamp(20px, 3vw, 30px);
      font-weight: 700;
      font-variant-numeric: tabular-nums;
      letter-spacing: 1px;
    }
    .blackout-indicator {
      display: none;
      background: #EF4444;
      color: #FFFFFF;
      font-size: 12px;
      font-weight: 800;
      padding: 4px 10px;
      border-radius: 4px;
      letter-spacing: 0.5px;
      animation: pulse 1.5s infinite;
    }
    @keyframes pulse {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.5; }
    }
    main {
      flex: 1;
      display: flex;
      flex-direction: column;
      padding: 16px 24px;
      gap: 16px;
      overflow: hidden;
    }
    .card-current {
      flex: 65;
      background: #0B0E14;
      border: 2px solid #30363D;
      border-radius: 12px;
      padding: 20px 28px;
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }
    .card-label {
      font-size: 12px;
      font-weight: 800;
      letter-spacing: 1.5px;
      text-transform: uppercase;
      margin-bottom: 8px;
    }
    .label-current { color: #38BDF8; }
    .label-next { color: #F59E0B; }
    .slide-text {
      flex: 1;
      display: flex;
      align-items: center;
      justify-content: center;
      text-align: center;
      font-size: clamp(24px, 4.5vw, 54px);
      font-weight: 700;
      line-height: 1.35;
      white-space: pre-wrap;
      overflow: hidden;
      text-shadow: 0 2px 4px rgba(0,0,0,0.8);
    }
    .card-next {
      flex: 35;
      background: #11141C;
      border: 1px solid #282F3B;
      border-radius: 10px;
      padding: 14px 24px;
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }
    .next-text {
      flex: 1;
      display: flex;
      align-items: center;
      justify-content: center;
      text-align: center;
      font-size: clamp(16px, 2.5vw, 30px);
      font-weight: 600;
      color: #94A3B8;
      line-height: 1.3;
      white-space: pre-wrap;
      overflow: hidden;
    }
    .alert-banner {
      display: none;
      background: #DC2626;
      color: #FFFFFF;
      font-size: clamp(16px, 2.2vw, 24px);
      font-weight: 700;
      padding: 10px 20px;
      text-align: center;
      letter-spacing: 0.5px;
      box-shadow: 0 -2px 10px rgba(0,0,0,0.5);
    }
    .clear-text { opacity: 0.2; }
  </style>
</head>
<body>
  <header>
    <div class="song-title" id="title">LuxPresenter Stage Display</div>
    <div class="clock-badge">
      <div class="blackout-indicator" id="blackoutBadge">BLACKOUT</div>
      <div class="clock" id="clock">--:--:--</div>
    </div>
  </header>

  <main>
    <div class="card-current">
      <div class="card-label label-current">CURRENT SLIDE</div>
      <div class="slide-text" id="currentSlide">Waiting for presenter...</div>
    </div>

    <div class="card-next">
      <div class="card-label label-next">NEXT SLIDE</div>
      <div class="next-text" id="nextSlide">--</div>
    </div>
  </main>

  <div class="alert-banner" id="alertBanner"></div>

  <script>
    const elTitle = document.getElementById('title');
    const elClock = document.getElementById('clock');
    const elBlackout = document.getElementById('blackoutBadge');
    const elCurrent = document.getElementById('currentSlide');
    const elNext = document.getElementById('nextSlide');
    const elAlert = document.getElementById('alertBanner');

    let lastData = null;

    async function pollStage() {
      try {
        const res = await fetch('/api/stage', { cache: 'no-store' });
        if (!res.ok) throw new Error('Network error');
        const data = await res.json();

        if (data.time) elClock.textContent = data.time;
        if (data.title !== undefined) elTitle.textContent = data.title || 'LuxPresenter Stage';

        elBlackout.style.display = data.isBlack ? 'block' : 'none';

        if (data.isClear) {
          elCurrent.classList.add('clear-text');
        } else {
          elCurrent.classList.remove('clear-text');
        }

        elCurrent.textContent = data.currentSlide || (data.isBlack ? '[ Screen Blackout ]' : '');
        elNext.textContent = data.nextSlide || '(End of item)';

        if (data.alertMessage && data.alertMessage.trim().length > 0) {
          elAlert.textContent = 'ALERT: ' + data.alertMessage;
          elAlert.style.display = 'block';
        } else {
          elAlert.style.display = 'none';
        }
      } catch (err) {
        // Retry silently on temporary connection blips
      } finally {
        setTimeout(pollStage, 300);
      }
    }

    pollStage();
  </script>
</body>
</html>
)rawhtml";

    return QByteArray(html);
}

} // namespace Lux
