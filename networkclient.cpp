/*
 * Copyright 2010 Kyle M Hall <kyle.m.hall@gmail.com>
 *
 * This file is part of Libki.
 *
 * Libki is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Libki is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Libki.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "networkclient.h"
#include "utils.h"

#include <QDir>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSslError>
#include <QUdpSocket>

#define VERSION "2.2.27"

NetworkClient::NetworkClient(QApplication *app) : QObject() {
  qDebug("ENTER NetworkClient::NetworkClient");
  this->app = app;

  qDebug() << "SSL version use for build: "
           << QSslSocket::sslLibraryBuildVersionString();
  qDebug() << "SSL version use for run-time: "
           << QSslSocket::sslLibraryVersionNumber();

  fileCounter = 0;

  QSettings settings;
  settings.setIniCodec("UTF-8");

  nodeName = getClientName();

  nodeLocation = settings.value("node/location").toString();
  qDebug() << "LOCATION: " << nodeLocation;
  nodeType = settings.value("node/type").toString();
  qDebug() << "TYPE: " << nodeType;
  nodeAgeLimit = settings.value("node/age_limit").toString();
  qDebug() << "AGE LIMIT: " << nodeAgeLimit;

  QString action = settings.value("node/logoutAction").toString();

  if (action == "logout") {
    actionOnLogout = LogoutAction::Logout;
  } else if (action == "reboot") {
    actionOnLogout = LogoutAction::Reboot;
  } else {
    actionOnLogout = LogoutAction::NoAction;
  }

  clientStatus = "online";

  qDebug() << "HOST: " << settings.value("server/host").toString();
  serviceURL.setHost(settings.value("server/host").toString());
  serviceURL.setPort(settings.value("server/port").toInt());
  serviceURL.setScheme(settings.value("server/scheme").toString());
  serviceURL.setPath("/api/client/v1_0");

  nodeIPAddress = getIPv4Address();
  nodeMACAddress = getMACAddress();
  nodeHostname = getHostname();

  urlQuery.addQueryItem("node", nodeName);
  urlQuery.addQueryItem("location", nodeLocation);
  urlQuery.addQueryItem("type", nodeType);
  urlQuery.addQueryItem("ipaddress", nodeIPAddress);
  urlQuery.addQueryItem("macaddress", nodeMACAddress);
  urlQuery.addQueryItem("hostname", nodeHostname);

  registerNode();
  registerNodeTimer = new QTimer(this);
  connect(registerNodeTimer, SIGNAL(timeout()), this, SLOT(registerNode()));
  registerNodeTimer->start(1000 * 10);

  checkForInternetConnectivity();
  checkForInternetConnectivityTimer = new QTimer(this);
  connect(checkForInternetConnectivityTimer, SIGNAL(timeout()), this, SLOT(checkForInternetConnectivity()));
  checkForInternetConnectivityTimer->start(1000 * 10);

  uploadPrintJobsTimer = new QTimer(this);
  connect(uploadPrintJobsTimer, SIGNAL(timeout()), this,
          SLOT(uploadPrintJobs()));

  updateUserDataTimer = new QTimer(this);
  connect(updateUserDataTimer, SIGNAL(timeout()), this,
          SLOT(getUserDataUpdate()));

  qDebug("LEAVE NetworkClient::NetworkClient");
}

void NetworkClient::attemptLogin(QString aUsername, QString aPassword) {
  qDebug("ENTER NetworkClient::attemptLogin");

  username = aUsername;
  password = aPassword;

  QUrl url = QUrl(serviceURL);
  QUrlQuery query = QUrlQuery(urlQuery);
  query.addQueryItem("version", VERSION);
  query.addQueryItem("action", "login");
  query.addQueryItem("username", username);
  query.addQueryItem("password", password);
  url.setQuery(query);

  qDebug() << "LOGIN URL: " << url.toString();
  qDebug() << "NetworkClient::attemptLogin";

  QNetworkAccessManager *nam;
  nam = new QNetworkAccessManager(this);
  QObject::connect(nam, SIGNAL(finished(QNetworkReply *)), this,
                   SLOT(processAttemptLoginReply(QNetworkReply *)));
  QObject::connect(
      nam, SIGNAL(sslErrors(QNetworkReply *, const QList<QSslError> &)), this,
      SLOT(handleSslErrors(QNetworkReply *, const QList<QSslError> &)));

  /*QNetworkReply* reply = */ nam->get(QNetworkRequest(url));
  qDebug("LEAVE NetworkClient::attemptLogin");
}

void NetworkClient::processAttemptLoginReply(QNetworkReply *reply) {
  qDebug("ENTER NetworkClient::processAttemptLogoutReply");

  handleNetworkReplyErrors(reply);

  QByteArray result;
  result = reply->readAll();

  QScriptValue sc;
  QScriptEngine engine;
  sc = engine.evaluate("(" + QString(result) + ")");

  if (sc.property("authenticated").toBoolean() == true) {
    qDebug("Login Authenticated");

    int units = sc.property("units").toInteger();
    int hold_items_count = sc.property("hold_items_count").toInteger();

    doLoginTasks(units, hold_items_count);
  } else {
    qDebug("Login Failed");

    QString errorCode = sc.property("error").toString();
    qDebug() << "Error Code: " << errorCode;

    username.clear();
    password.clear();

    emit loginFailed(errorCode);
  }

  reply->abort();
  reply->deleteLater();
  reply->manager()->deleteLater();

  qDebug("LEAVE NetworkClient::processAttemptLogoutReply");
}

void NetworkClient::attemptLogout() {
  qDebug("ENTER NetworkClient::attemptLogout");

  QNetworkAccessManager *nam;
  nam = new QNetworkAccessManager(this);
  QObject::connect(nam, SIGNAL(finished(QNetworkReply *)), this,
                   SLOT(processAttemptLogoutReply(QNetworkReply *)));
  QObject::connect(
      nam, SIGNAL(sslErrors(QNetworkReply *, const QList<QSslError> &)), this,
      SLOT(handleSslErrors(QNetworkReply *, const QList<QSslError> &)));

  QUrl url = QUrl(serviceURL);
  QUrlQuery query = QUrlQuery(urlQuery);
  query.addQueryItem("version", VERSION);
  query.addQueryItem("action", "logout");
  query.addQueryItem("username", username);
  query.addQueryItem("password", password);
  url.setQuery(query);

  /*QNetworkReply* reply =*/nam->get(QNetworkRequest(url));

  qDebug("LEAVE NetworkClient::attemptLogout");
}

void NetworkClient::processAttemptLogoutReply(QNetworkReply *reply) {
  qDebug("ENTER NetworkClient::processAttemptLogoutReply");

  handleNetworkReplyErrors(reply);

  QByteArray result;
  result = reply->readAll();

  QScriptValue sc;
  QScriptEngine engine;
  sc = engine.evaluate("(" + QString(result) + ")");

  if (sc.property("logged_out").toBoolean() == true) {
    doLogoutTasks();
  } else {
    emit logoutFailed();
  }

  reply->abort();
  reply->deleteLater();
  reply->manager()->deleteLater();

  qDebug("LEAVE NetworkClient::processAttemptLogoutReply");
}

void NetworkClient::getUserDataUpdate() {
  qDebug("ENTER NetworkClient::getUserDataUpdate");

  QNetworkAccessManager *nam = new QNetworkAccessManager(this);
  QObject::connect(nam, SIGNAL(finished(QNetworkReply *)), this,
                   SLOT(processGetUserDataUpdateReply(QNetworkReply *)));
  QObject::connect(
      nam, SIGNAL(sslErrors(QNetworkReply *, const QList<QSslError> &)), this,
      SLOT(handleSslErrors(QNetworkReply *, const QList<QSslError> &)));

  QUrl url = QUrl(serviceURL);
  QUrlQuery query = QUrlQuery(urlQuery);
  query.addQueryItem("version", VERSION);
  query.addQueryItem("action", "get_user_data");
  query.addQueryItem("username", username);
  query.addQueryItem("password", password);
  url.setQuery(query);

  /*QNetworkReply* reply =*/nam->get(QNetworkRequest(url));

  qDebug("LEAVE NetworkClient::getUserDataUpdate");
}

void NetworkClient::processGetUserDataUpdateReply(QNetworkReply *reply) {
  qDebug("ENTER NetworkClient::processGetUserDataUpdateReply");

  handleNetworkReplyErrors(reply);

  QByteArray result;
  result = reply->readAll();

  qDebug() << "Server Result: " << result;

  QJsonDocument jd = QJsonDocument::fromJson(result);

  if (jd.isObject()) {
    QJsonObject jo = jd.object();

    QString status = jo["status"].toString();
    qDebug() << "STATUS: " << status;

    if (status == "Logged in") {
      QJsonArray messages = jo["messages"].toArray();
      qDebug() << "MESSAGE ARRAY SIZE: " << messages.size();

      for (int i = 0; i < messages.size(); i++) {
        QString m = messages[i].toString();
        qDebug() << "MESSAGE: " << m;
        emit messageRecieved(m);
      }

      QJsonValueRef units_json = jo["units"];
      QVariant units_variant = units_json.toVariant();
      int units = units_variant.toInt();
      qDebug() << "UNITS JASON: " << units_json;
      qDebug() << "UNITS VARIANT: " << units_variant;
      qDebug() << "UNITS: " << units;

      emit timeUpdatedFromServer(units);

      if (units < 1) {
        doLogoutTasks();
      }
    } else if (status == "Logged out") {
      doLogoutTasks();
    } else if (status == "Kicked") {
      doLogoutTasks();
    }
  }

  reply->abort();
  reply->deleteLater();
  reply->manager()->deleteLater();

  qDebug("LEAVE NetworkClient::processGetUserDataUpdateReply");
}

void NetworkClient::uploadPrintJobs() {
  qDebug() << "NetworkClient::uploadPrintJobs";

  QSettings printerSettings;
  printerSettings.beginGroup("printers");
  QStringList printers = printerSettings.allKeys();
  qDebug() << "PRINTER: " << printers;

  foreach (const QString &printer, printers) {
    qDebug() << "FOUND PRINTER: " << printer;
    qDebug() << "PATH: " << printerSettings.value(printer).toString();

    QString directory = printerSettings.value(printer).toString();
    QDir dir(directory);

    if (!dir.exists()) {
      qDebug() << "Directory does not exist: " << directory;
      bool s = dir.mkpath(directory);
      qDebug() << "Attempt to create directory result: " << s;
    }

    dir.setFilter(QDir::Files);
    dir.setSorting(QDir::Time | QDir::Reversed);

    QFileInfoList list = dir.entryInfoList();

    for (int i = 0; i < list.size(); ++i) {
      QString printedFileSuffix = ".printed";

      QFileInfo fileInfo = list.at(i);
      QString absoluteFilePath = fileInfo.absoluteFilePath();
      QString fileName = fileInfo.fileName();
      // qDebug() << "Found Print Job File: " << absoluteFilePath;

      if (fileName.endsWith(printedFileSuffix)) {
        // qDebug() << "PRINT JOB ALREADY PROCCESSED: " << fileName;
        continue;
      }
      qDebug() << "SENDING PRINT JOB: " << fileName;

      QString fileCounterString = QString::number(fileCounter);
      fileCounter++;

      QString newAbsoluteFilePath =
          absoluteFilePath + "." + fileCounterString + printedFileSuffix;
      bool renamed = QFile::rename(absoluteFilePath, newAbsoluteFilePath);
      if ( !renamed ) {
          qDebug() << "RENAME FROM " << absoluteFilePath << " TO " << printedFileSuffix << " FAILED! SKIPPING FILE.";
          continue;
      }

      QFile *file = new QFile(newAbsoluteFilePath);
      bool opened = file->open(QIODevice::ReadOnly);
      if ( !opened ) {
          qDebug() << "OPENDING FILE " << newAbsoluteFilePath << " FAILED! SKIPPING FILE.";
          continue;
      }

      QHttpMultiPart *multiPart =
          new QHttpMultiPart(QHttpMultiPart::FormDataType);

      // We con't delete the file object now, delete it with the multiPart
      file->setParent(multiPart);

      QHttpPart clientNamePart;
      clientNamePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                               QVariant("form-data; name=client_name"));
      QByteArray clientNameQBA;
      clientNameQBA.append(nodeName);
      clientNamePart.setBody(clientNameQBA);
      multiPart->append(clientNamePart);

      QHttpPart userNamePart;
      userNamePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                             QVariant("form-data; name=username"));
      QByteArray userNameQBA;
      userNameQBA.append(username);
      userNamePart.setBody(userNameQBA);
      multiPart->append(userNamePart);

      QHttpPart printerNamePart;
      printerNamePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                QVariant("form-data; name=printer"));
      QByteArray printerNameQBA;
      printerNameQBA.append(printer);
      printerNamePart.setBody(printerNameQBA);
      multiPart->append(printerNamePart);

      QHttpPart printJobPart;
      printJobPart.setHeader(
          QNetworkRequest::ContentDispositionHeader,
          QVariant("form-data; name=print_file; filename=" + fileName));
      printJobPart.setBodyDevice(file);
      multiPart->append(printJobPart);

      QHttpPart fileNamePart;
      fileNamePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                             QVariant("form-data; name=filename"));
      QByteArray fileNameQBA;
      fileNameQBA.append(fileName);
      fileNamePart.setBody(fileNameQBA);
      multiPart->append(fileNamePart);

      QUrl printUrl = QUrl(serviceURL);
      printUrl.setPath("/api/client/v1_0/print");
      QNetworkRequest request(printUrl);

      QNetworkAccessManager *networkManager = new QNetworkAccessManager(this);
      QObject::connect(
          networkManager,
          SIGNAL(sslErrors(QNetworkReply *, const QList<QSslError> &)), this,
          SLOT(handleSslErrors(QNetworkReply *, const QList<QSslError> &)));

      QNetworkReply *reply = networkManager->post(request, multiPart);
      multiPart->setParent(reply);  // delete the multiPart with the reply

      // TODO: delete file after finished signal emits
      // https://stackoverflow.com/questions/5153157/passing-an-argument-to-a-slot
      connect(networkManager, SIGNAL(finished(QNetworkReply *)), this,
              SLOT(uploadPrintJobReply(QNetworkReply *)));
      connect(reply, SIGNAL(uploadProgress(qint64, qint64)), this,
              SLOT(handleUploadProgress(qint64, qint64)));
    }
  }

  qDebug() << "LEAVE NetworkClient::uploadPrintJobs";
}

void NetworkClient::handleUploadProgress(qint64 bytesSent, qint64 bytesTotal) {
  qDebug() << "Uploaded " << bytesSent << "of" << bytesTotal;
}

void NetworkClient::uploadPrintJobReply(QNetworkReply *reply) {
  qDebug("ENTER NetworkClient::uploadPrintJobReply");

  handleNetworkReplyErrors(reply);

  if (reply->error() == QNetworkReply::NoError) {
    reply->abort();
    reply->deleteLater();
    reply->manager()->deleteLater();
  } else {
    qDebug() << "Network Error: " << reply->errorString();
    qDebug() << "Retrying network request.";

    QNetworkRequest request = reply->request();

    QHttpMultiPart *multiPart = reply->findChild<QHttpMultiPart *>();
    qDebug() << "Found multiPart " << multiPart;

    QNetworkAccessManager *networkManager = new QNetworkAccessManager(this);
    QObject::connect(
        networkManager,
        SIGNAL(sslErrors(QNetworkReply *, const QList<QSslError> &)), this,
        SLOT(handleSslErrors(QNetworkReply *, const QList<QSslError> &)));

    QNetworkReply *reply = networkManager->post(request, multiPart);

    multiPart->setParent(reply);  // delete the multiPart with the reply

    connect(networkManager, SIGNAL(finished(QNetworkReply *)), this,
            SLOT(uploadPrintJobReply(QNetworkReply *)));
    connect(reply, SIGNAL(uploadProgress(qint64, qint64)), this,
            SLOT(handleUploadProgress(qint64, qint64)));
  };

  qDebug("LEAVE NetworkClient::uploadPrintJobReply");
}

void NetworkClient::registerNode() {
  qDebug("ENTER NetworkClient::registerNode");

  QNetworkAccessManager *nam;
  nam = new QNetworkAccessManager(this);
  QObject::connect(nam, SIGNAL(finished(QNetworkReply *)), this,
                   SLOT(processRegisterNodeReply(QNetworkReply *)));

  QObject::connect(nam, SIGNAL(sslErrors(QNetworkReply *, QList<QSslError>)),
                   this,
                   SLOT(handleSslErrors(QNetworkReply *, QList<QSslError>)));

  QUrl url = QUrl(serviceURL);
  QUrlQuery query = QUrlQuery(urlQuery);
  query.addQueryItem("version", VERSION);
  query.addQueryItem("action", "register_node");
  query.addQueryItem("node_name", nodeName);
  query.addQueryItem("age_limit", nodeAgeLimit);
  url.setQuery(query);

  /*QNetworkReply* reply =*/nam->get(QNetworkRequest(url));

  qDebug("LEAVE NetworkClient::registerNode");
}

void NetworkClient::handleSslErrors(QNetworkReply *reply,
                                    QList<QSslError> error) {
  reply->ignoreSslErrors(error);
}

void NetworkClient::processRegisterNodeReply(QNetworkReply *reply) {
  qDebug("ENTER NetworkClient::processRegisterNodeReply");

  handleNetworkReplyErrors(reply);

  QByteArray result;
  result = reply->readAll();

  qDebug() << "Server Result: " << result;

  QScriptValue sc;
  QScriptEngine engine;
  sc = engine.evaluate("(" + QString(result) + ")");

  if (!sc.property("registered").toBoolean()) {
    qDebug("Node Registration FAILED");
  }

  // TODO: Rename this to something like 'auto-login guest session'
  //  This feature is not related to session locking
  if (sc.property("unlock").toBoolean()) {
    qDebug("Unlocking...");
    username = sc.property("username").toString();
    doLoginTasks(sc.property("minutes").toInteger(), 0);
  }

  if (sc.property("shutdown").toBoolean()) {
    qDebug("Received shutdown message from server");

    emit allowClose(true);

#ifdef Q_OS_WIN
    QProcess::startDetached("shutdown -s -f -t 0");
#endif  // ifdef Q_OS_WIN

#ifdef Q_OS_UNIX
    // For this to work, sudo must be installed and the line
    // %shutdown ALL=(root) NOPASSWD: /sbin/reboot FIXME
    // needs to be added to /etc/sudoers
    QProcess::startDetached("sudo shutdown 0");
#endif  // ifdef Q_OS_UNIX
  }

  if (sc.property("suspend").toBoolean()) {
#ifdef Q_OS_WIN
    QProcess::startDetached("rundll32.exe powrprof.dll,SetSuspendState 0,1,0");
#endif  // ifdef Q_OS_WIN

#ifdef Q_OS_UNIX
    QProcess::startDetached("systemctl suspend -i");
#endif  // ifdef Q_OS_UNIX
  }

  if (sc.property("restart").toBoolean()) {
    emit allowClose(true);

#ifdef Q_OS_WIN
    QProcess::startDetached("shutdown -r -f -t 0");
#endif  // ifdef Q_OS_WIN

#ifdef Q_OS_UNIX
    // For this to work, sudo must be installed and the line
    // %shutdown ALL=(root) NOPASSWD: /sbin/reboot
    // needs to be added to /etc/sudoers
    QProcess::startDetached("sudo reboot");
#endif  // ifdef Q_OS_UNIX
  }

  if (sc.property("wakeup").toBoolean()) {
    QStringList MAC_addresses = sc.engine()->fromScriptValue<QStringList>(
        sc.property("wol_mac_addresses"));
    wakeOnLan(MAC_addresses, sc.property("wol_host").toString(),
              sc.property("wol_port").toInteger());
  }

  QString styleSheet = sc.property("ClientStyleSheet").toString();
  if (!styleSheet.isEmpty()) {
      this->app->setStyleSheet(styleSheet);
  }

  QSettings settings;
  settings.setIniCodec("UTF-8");

  QString bannerTopURL = settings.value("session/BannerTopURL").toString();
  QString bannerBottomURL =
      settings.value("session/BannerBottomURL").toString();

  settings.setValue("session/ClientBehavior",
                    sc.property("ClientBehavior").toString());
  settings.setValue("session/ReservationShowUsername",
                    sc.property("ReservationShowUsername").toString());
  settings.setValue("session/EnableClientSessionLocking",
                    sc.property("EnableClientSessionLocking").toString());
  settings.setValue("session/EnableClientPasswordlessMode",
                    sc.property("EnableClientPasswordlessMode").toString());
  settings.setValue("session/TermsOfService",
                    sc.property("TermsOfService").toString());
  settings.setValue("session/TermsOfServiceDetails",
                    sc.property("TermsOfServiceDetails").toString());

  settings.setValue("session/BannerTopURL",
                    sc.property("BannerTopURL").toString());
  settings.setValue("session/BannerTopWidth",
                    sc.property("BannerTopWidth").toString());
  settings.setValue("session/BannerTopHeight",
                    sc.property("BannerTopHeight").toString());

  settings.setValue("session/BannerBottomURL",
                    sc.property("BannerBottomURL").toString());
  settings.setValue("session/BannerBottomWidth",
                    sc.property("BannerBottomWidth").toString());
  settings.setValue("session/BannerBottomHeight",
                    sc.property("BannerBottomHeight").toString());

  settings.setValue("session/LogoURL",
                    sc.property("LogoURL").toString());
  settings.setValue("session/LogoWidth",
                    sc.property("LogoWidth").toString());
  settings.setValue("session/LogoHeight",
                    sc.property("LogoHeight").toString());

  settings.setValue("session/inactivityLogout",
                    sc.property("inactivityLogout").toString());
  settings.setValue("session/inactivityWarning",
                    sc.property("inactivityWarning").toString());

  settings.setValue("session/InternetConnectivityURLs",
                    sc.property("InternetConnectivityURLs").toString());

  settings.setValue("session/ClientTimeNotificationFrequency",
                    sc.property("ClientTimeNotificationFrequency").toString());
  settings.setValue("session/ClientTimeWarningThreshold",
                    sc.property("ClientTimeWarningThreshold").toString());

  QString logoURL = settings.value("images/logo").toString();
  if ( ! sc.property("Logo").toString().isEmpty() ) {
    settings.setValue("images/logo",
                      sc.property("Logo").toString());

    settings.setValue("images/logo_height",
                      sc.property("LogoHeight").toString());

    settings.setValue("images/logo_width",
                      sc.property("LogoWidth").toString());
  }

  settings.sync();

  if (
      (logoURL != sc.property("Logo").toString()) ||
      (bannerTopURL != sc.property("BannerTopURL").toString()) ||
      (bannerBottomURL != sc.property("BannerBottomURL").toString())
  ) {
    emit handleBanners();  // TODO: Emit only if a banner url has changed
  }

  QString reserved_for = sc.property("reserved_for").toString();
  emit setReservationStatus(reserved_for);

  QString status = sc.property("status").toString();
  if (status != clientStatus) {
    if (status == "suspended") {
      emit clientSuspended();
    } else if (status == "online") {
      emit clientOnline();
    }
  }
  clientStatus = status;

  reply->abort();
  reply->deleteLater();
  reply->manager()->deleteLater();

  qDebug("LEAVE NetworkClient::processRegisterNodeReply");
}

void NetworkClient::checkForInternetConnectivity() {
  qDebug("ENTER NetworkClient::checkForInternetConnectivity");

  QList<QString> list;

  QSettings settings;
  settings.setIniCodec("UTF-8");
  QString internetConnectivityURLs = settings.value("session/InternetConnectivityURLs").toString();
  //qDebug() << "URLS: " << internetConnectivityURLs;
  if ( internetConnectivityURLs != "null" ) {
      list = internetConnectivityURLs.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
  }
  //qDebug() << "URLS LIST: " << list.join(" ");

  if ( list.size() ) {
      // Select a URL from the list at random to test connectivity
      QString url = list.at(qrand() % list.size());

      qDebug() << "CHECKING URL: " << url;

      QNetworkAccessManager *nam;
      nam = new QNetworkAccessManager(this);
      QObject::connect(nam, SIGNAL(finished(QNetworkReply *)), this,
               SLOT(processCheckForInternetConnectivityReply(QNetworkReply *)));
      QObject::connect(
          nam, SIGNAL(sslErrors(QNetworkReply *, const QList<QSslError> &)), this,
          SLOT(handleSslErrors(QNetworkReply *, const QList<QSslError> &)));

      nam->get(QNetworkRequest(QUrl(url)));
  }

  qDebug("LEAVE NetworkClient::checkForInternetConnectivity");
}

void NetworkClient::processCheckForInternetConnectivityReply(QNetworkReply *reply) {
  qDebug("ENTER NetworkClient::processCheckForInternetConnectivityReply");

  if ( reply->error() != QNetworkReply::NoError ) {
      emit internetAccessWarning(reply->errorString());
      qDebug() << "NetworkClient::processCheckForInternetConnectivityReply Network Reply Error: " << reply->errorString();
  } else {
      emit internetAccessWarning("");
  }

  reply->abort();
  reply->deleteLater();
  reply->manager()->deleteLater();

  qDebug("LEAVE NetworkClient::processCheckForInternetConnectivityReply");
}

void NetworkClient::clearMessage() {
  qDebug("ENTER NetworkClient::clearMessage");

  QNetworkAccessManager *nam = new QNetworkAccessManager(this);
  QObject::connect(nam, SIGNAL(finished(QNetworkReply *)), this,
                   SLOT(ignoreNetworkReply(QNetworkReply *)));
  QObject::connect(
      nam, SIGNAL(sslErrors(QNetworkReply *, const QList<QSslError> &)), this,
      SLOT(handleSslErrors(QNetworkReply *, const QList<QSslError> &)));
  QUrl url = QUrl(serviceURL);
  QUrlQuery query = QUrlQuery(urlQuery);
  query.addQueryItem("version", VERSION);
  query.addQueryItem("action", "clear_message");
  query.addQueryItem("username", username);
  query.addQueryItem("password", password);
  url.setQuery(query);
  nam->get(QNetworkRequest(url));

  qDebug("LEAVE NetworkClient::clearMessage");
}

void NetworkClient::acknowledgeReservation(QString reserved_for) {
  qDebug("ENTER NetworkClient::acknowledgeReservation");

  QNetworkAccessManager *nam = new QNetworkAccessManager(this);
  QObject::connect(nam, SIGNAL(finished(QNetworkReply *)), this,
                   SLOT(ignoreNetworkReply(QNetworkReply *)));
  QObject::connect(
      nam, SIGNAL(sslErrors(QNetworkReply *, const QList<QSslError> &)), this,
      SLOT(handleSslErrors(QNetworkReply *, const QList<QSslError> &)));
  QUrl url = QUrl(serviceURL);
  QUrlQuery query = QUrlQuery(urlQuery);
  query.addQueryItem("version", VERSION);
  query.addQueryItem("action", "acknowledge_reservation");
  query.addQueryItem("reserved_for", reserved_for);
  url.setQuery(query);

  nam->get(QNetworkRequest(url));

  qDebug("LEAVE NetworkClient::acknowledgeReservation");
}

void NetworkClient::ignoreNetworkReply(QNetworkReply *reply) {
  qDebug("ENTER NetworkClient::ignoreNetworkReply");

  handleNetworkReplyErrors(reply);

  reply->abort();
  reply->deleteLater();
  reply->manager()->deleteLater();

  qDebug("LEAVE NetworkClient::ignoreNetworkReply");
}

void NetworkClient::doLoginTasks(int units, int hold_items_count) {
  qDebug("ENTER NetworkClient::doLoginTasks");

#ifdef Q_OS_WIN
  // FIXME: We should delete print jobs at login as well in case a client crash
  // prevented the print jobs for getting cleaned up at logout time

  // If this is an MS Windows platform, use the keylocker programs to limit
  // mischief.
  QProcess::startDetached("c:/windows/explorer.exe");
  QProcess::startDetached("windows/on_login.exe");
#endif  // ifdef Q_OS_WIN

  uploadPrintJobsTimer->start(1000 * 2);
  updateUserDataTimer->start(1000 * 10);

  QSettings settings;
  settings.setIniCodec("UTF-8");
  settings.setValue("session/LoggedInUser", username);
  settings.sync();
  qDebug() << "SCRIPTLOGIN:" << settings.value("scriptlogin/enable").toString();
  if (settings.value("scriptlogin/enable").toString() == "1") {
    QProcess::startDetached(settings.value("scriptlogin/script").toString());
  }
  emit loginSucceeded(username, password, units, hold_items_count);

  qDebug("ENTER NetworkClient::doLoginTasks");
}

void NetworkClient::doLogoutTasks() {
  qDebug("ENTER NetworkClient::doLogoutTasks");

  QSettings settings;
  settings.setIniCodec("UTF-8");
  settings.setValue("session/LoggedInUser", "");
  settings.sync();

  // Delete print jobs
  QSettings printerSettings;
  printerSettings.beginGroup("printers");
  QStringList printers = printerSettings.allKeys();
  foreach (const QString &printer, printers) {
    QString directory = printerSettings.value(printer).toString();
    QDir dir(directory);

    dir.setFilter(QDir::Files);

    QFileInfoList list = dir.entryInfoList();

    for (int i = 0; i < list.size(); ++i) {
      QFileInfo fileInfo = list.at(i);
      QString absoluteFilePath = fileInfo.absoluteFilePath();
      QFile::remove(absoluteFilePath);
    }
  }

  uploadPrintJobsTimer->stop();
  updateUserDataTimer->stop();

  username.clear();
  password.clear();

#ifdef Q_OS_WIN

  // If this is an MS Windows platform, use the keylocker programs to limit
  // mischief.
  QProcess::startDetached("taskkill /f /im explorer.exe");
  QProcess::startDetached("windows/on_logout.exe");

  if (actionOnLogout == LogoutAction::Logout) {
    emit allowClose(true);
    QProcess::startDetached("shutdown -l -f");
  } else if (actionOnLogout == LogoutAction::Reboot) {
    emit allowClose(true);
    QProcess::startDetached("shutdown -r -f -t 0");
  }
#endif  // ifdef Q_OS_WIN

#ifdef Q_OS_UNIX

  if (actionOnLogout == LogoutAction::Logout) {
    emit allowClose(true);

    // Restart KDE 4
    QProcess::startDetached(
        "qdbus org.kde.ksmserver /KSMServer org.kde.KSMServerInterface.logout "
        "-0 -1 -1");

    // Restart Gnome
    QProcess::startDetached("gnome-session-save --kill --silent");

    // Restart Unity
    QProcess::startDetached("gnome-session-quit --no-prompt");

    // Restart XFCE 4
    QProcess::startDetached("/usr/bin/xfce4-session-logout");

    // Restart Mate
    QProcess::startDetached("mate-session-save --force-logout");
  } else if (actionOnLogout == LogoutAction::Reboot) {
    emit allowClose(true);

    // For this to work, sudo must be installed and the line
    // %shutdown ALL=(root) NOPASSWD: /sbin/reboot
    // needs to be added to /etc/sudoers
    QProcess::startDetached("sudo reboot");
  }
#endif  // ifdef Q_OS_UNIX
  qDebug() << "SCRIPTLOGOUT:"
           << settings.value("scriptlogout/enable").toString();
  if (settings.value("scriptlogout/enable").toString() == "1") {
    QProcess::startDetached(settings.value("scriptlogout/script").toString());
  }
  emit logoutSucceeded();

  qDebug("LEAVE NetworkClient::doLogoutTasks");
}

void NetworkClient::wakeOnLan(QStringList MAC_addresses, QString host,
                              qint64 port) {
  qDebug("ENTER NetworkClient::wakeOnLan");

  QHostAddress host_address;
  host_address.setAddress(host);

  for (int i = 0; i < MAC_addresses.size(); i++) {
    char address[6];
    char packet[102];

    memset(packet, 0xff, 6);

    for (int j = 0; j < 6; j++) {
      address[j] = MAC_addresses.at(i).section(":", j, j).toInt(Q_NULLPTR, 16);
    }

    for (int j = 1; j <= 16; j++) {
      memcpy(&packet[j * 6], &address, 6 * sizeof(char));
    }

    QUdpSocket udpSocket;
    udpSocket.writeDatagram(packet, 102, host_address, port);
  }

  qDebug("LEAVE NetworkClient::wakeOnLan");
}

void NetworkClient::handleNetworkReplyErrors(QNetworkReply *reply) {
  if ( reply->error() != QNetworkReply::NoError ) {
      QString e = QString::number(reply->error());
      qDebug() << "ERROR: Server Access Warning: " << e << " :: " << reply->errorString();

      QString s = e + ": " + reply->errorString();
      serverAccessWarning(s);
  } else {
      serverAccessWarning("");
  }
}
