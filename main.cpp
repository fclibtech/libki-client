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

#include <stdlib.h>

#include <QApplication>
#include <QProcess>
#include <QSettings>
#include <QWebView>

#include "loginwindow.h"
#include "logutils.h"
#include "networkclient.h"
#include "timerwindow.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  LogUtils::initLogging();

  QString os_username;

#ifdef Q_OS_WIN
  os_username = getenv("USERNAME");
#endif  // ifdef Q_OS_WIN
#ifdef Q_OS_UNIX
  os_username = getenv("USER");
#endif  // ifdef Q_OS_UNIX

  qDebug() << "OS Username: " << os_username;

  // Translate the application if the locale is available
  QString locale = QLocale::system().name();
  QString filename = QString("languages/libkiclient_") + locale;
  qDebug() << "LOCALE: " << locale;
  qDebug() << "LOCALE FILE: " << filename;
  QTranslator translator;

  if (translator.load(filename, ":/")) {
    app.installTranslator(&translator);
    qDebug() << "Translation file loaded" << filename;
  } else
    qDebug() << "Translation file not found:" << filename;

  /* Apply the stylesheet */
  QFile qss("libki.qss");
  qss.open(QFile::ReadOnly);
  app.setStyleSheet(qss.readAll());
  qss.close();

  QCoreApplication::setOrganizationName("Libki");
  QCoreApplication::setOrganizationDomain("libki.org");
  QCoreApplication::setApplicationName("Libki Kiosk Management System");
  QSettings::setDefaultFormat(QSettings::IniFormat);

  QSettings settings;
  settings.setIniCodec("UTF-8");

  QString startUserShell;
  startUserShell = settings.value("node/start_user_shell").toString();
  qDebug() << "start_user_shell: " << startUserShell;

  QString onlyRunFor;
  onlyRunFor = settings.value("node/onlyRunFor").toString();
  qDebug() << "onlyRunFor: " << onlyRunFor;

  if (!onlyRunFor.isEmpty()) {
    QStringList usernames = onlyRunFor.split(",");
    if ( ! usernames.contains(os_username) ) {
      qDebug() << "onlyRunFor does not match OS username";
      if (!startUserShell.isEmpty()) {
        qDebug() << "running user shell " << startUserShell;
        QProcess::startDetached('"' + startUserShell + '"');
      }
      qDebug() << "exiting.";
      exit(1);
    }
  }

  QString onlyStopFor;
  onlyStopFor = settings.value("node/onlyStopFor").toString();
  qDebug() << "onlyStopFor: " << onlyStopFor;

  if (!onlyStopFor.isEmpty()) {
      QStringList usernames = onlyStopFor.split(",");
      if ( usernames.contains(os_username) ) {
          qDebug() << "onlyStopFor matches OS username: " << os_username;
          if (!startUserShell.isEmpty()) {
              qDebug() << "running user shell " << startUserShell;
              QProcess::startDetached('"' + startUserShell + '"');
          }
          qDebug() << "exiting.";
          exit(1);
      }
  }

#ifdef Q_OS_WIN

  // If this is an MS Windows platform, use the keylocker programs to limit
  // mischief.
  QProcess::startDetached("taskkill /f /im explorer.exe");
  QProcess::startDetached("windows/on_startup.exe");
#endif  // ifdef Q_OS_WIN

  settings.setValue("session/ClientBehavior", "");
  settings.setValue("session/ReservationShowUsername", "");
  settings.setValue("session/LoggedInUser", "");

  settings.sync();

  LoginWindow *loginWindow = new LoginWindow();
  TimerWindow *timerWindow = new TimerWindow();
  NetworkClient *networkClient = new NetworkClient( &app );

  QObject::connect(
      loginWindow,
      SIGNAL(loginSucceeded(const QString &, const QString &, int, int)),
      timerWindow,
      SLOT(startTimer(const QString &, const QString &, int, int)));

  QObject::connect(timerWindow, SIGNAL(requestLogout()), networkClient,
                   SLOT(attemptLogout()));
  QObject::connect(networkClient, SIGNAL(logoutSucceeded()), timerWindow,
                   SLOT(stopTimer()));

  QObject::connect(timerWindow, SIGNAL(timerStopped()), loginWindow,
                   SLOT(displayLoginWindow()));

  QObject::connect(
      loginWindow, SIGNAL(attemptLogin(const QString &, const QString &)),
      networkClient, SLOT(attemptLogin(const QString &, const QString &)));

  QObject::connect(
      networkClient, SIGNAL(loginSucceeded(QString, QString, int, int)),
      loginWindow, SLOT(attemptLoginSuccess(QString, QString, int, int)));

  QObject::connect(networkClient, SIGNAL(loginFailed(QString)), loginWindow,
                   SLOT(attemptLoginFailure(QString)));

  QObject::connect(networkClient, SIGNAL(setReservationStatus(QString)),
                   loginWindow, SLOT(handleReservationStatus(QString)));

  QObject::connect(loginWindow, SIGNAL(displayingReservationMessage(QString)),
                   networkClient, SLOT(acknowledgeReservation(QString)));

  QObject::connect(networkClient, SIGNAL(handleBanners()), loginWindow,
                   SLOT(handleBanners()));

  QObject::connect(networkClient, SIGNAL(clientSuspended()), loginWindow,
                   SLOT(disableLogin()));

  QObject::connect(networkClient, SIGNAL(clientOnline()), loginWindow,
                   SLOT(enableLogin()));

  QObject::connect(networkClient, SIGNAL(timeUpdatedFromServer(int)),
                   timerWindow, SLOT(updateTimeLeft(int)));

  QObject::connect(networkClient, SIGNAL(messageRecieved(QString)), timerWindow,
                   SLOT(showMessage(QString)));

  QObject::connect(networkClient, SIGNAL(allowClose(bool)), loginWindow,
                   SLOT(setAllowClose(bool)));
  QObject::connect(networkClient, SIGNAL(allowClose(bool)), timerWindow,
                   SLOT(setAllowClose(bool)));

  QObject::connect(networkClient, SIGNAL(serverAccessWarning(QString)), loginWindow,
                   SLOT(showServerAccessWarning(QString)));
  QObject::connect(networkClient, SIGNAL(internetAccessWarning(QString)), loginWindow,
                   SLOT(showInternetAccessWarning(QString)));

  loginWindow->show();

  return app.exec();
}
