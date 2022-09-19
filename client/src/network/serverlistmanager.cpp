#include "serverlistmanager.h"
#include "src/config/appconfig.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QtDebug>

using namespace QtPromise;

namespace xpilot
{
    ServerListManager::ServerListManager(QObject *parent) :
        QObject(parent),
        nam(new QNetworkAccessManager)
    {

    }

    void ServerListManager::PerformServerListDownload(const QString &url)
    {
        DownloadServerList(url).then([&](QVector<NetworkServerInfo> serverList){
            if(serverList.size() > 0) {
                emit serverListDownloaded(serverList.size());
                AppConfig::getInstance()->CachedServers.clear();
                for(auto & server: serverList) {
                    AppConfig::getInstance()->CachedServers.append(server);
                }
                AppConfig::getInstance()->saveConfig();
            }
        }).fail([&](const QString &err){
            emit serverListDownloadError(err);
        });
    }

    QtPromise::QPromise<QVector<NetworkServerInfo>> ServerListManager::DownloadServerList(const QString &url)
    {
        return QtPromise::QPromise<QVector<NetworkServerInfo>>{[&](const auto resolve, const auto reject)
        {
            m_reply = nam->get(QNetworkRequest{url});

            QObject::connect(m_reply, &QNetworkReply::finished, [=]() {
                if(m_reply->error() == QNetworkReply::NoError) {
                    QJsonDocument json = QJsonDocument::fromJson(m_reply->readAll());
                    if(!json.isArray()) {
                        reject(QString{"Error parsing network server list: response is not an array."});
                        return;
                    }

                    QJsonArray servers = json.array();
                    QVector<NetworkServerInfo> serverList;

                    for(const QJsonValue &server : servers) {
                        NetworkServerInfo serverInfo;
                        serverInfo.Name = server["name"].toString();
                        serverInfo.Address = server["hostname_or_ip"].toString();
                        serverInfo.Location = server["location"].toString();
                        serverList.append(serverInfo);
                    }

                    resolve(serverList);
                }
                else {
                    reject(QString{m_reply->errorString()});
                }
            });
        }};
    }
}
