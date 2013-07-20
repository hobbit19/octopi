#include "mainwindow.h"
#include "../../src/strconstants.h"
#include "../../src/uihelper.h"
#include "../../src/package.h"
#include "../../src/pacmanhelperclient.h"

#include <QTimer>
#include <QSystemTrayIcon>
#include <QAction>
#include <QMenu>
#include <QProcess>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent)
{
  m_pacmanDatabaseSystemWatcher =
            new QFileSystemWatcher(QStringList() << ctn_PACMAN_DATABASE_DIR, this);

  initSystemTrayIcon();
}

/*
 * Let's initialize the system tray object...
 */
void MainWindow::initSystemTrayIcon()
{
  m_systemTrayIcon = new QSystemTrayIcon(this);
  m_systemTrayIcon->setObjectName("systemTrayIcon");

  refreshAppIcon();

  if (m_numberOfOutdatedPackages == 0)
  {
    m_systemTrayIcon->setToolTip(StrConstants::getApplicationName());
  }
  else if (m_numberOfOutdatedPackages > 0)
  {
    if (m_numberOfOutdatedPackages == 1)
    {
      m_systemTrayIcon->setToolTip(StrConstants::getOneNewUpdate());
    }
    else if (m_numberOfOutdatedPackages > 1)
    {
      m_systemTrayIcon->setToolTip(StrConstants::getNewUpdates().arg(m_numberOfOutdatedPackages));
    }
  }

  m_systemTrayIcon->show();

  m_actionExit = new QAction(IconHelper::getIconExit(), tr("Exit"), this);
  //m_actionExit->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Q));
  connect(m_actionExit, SIGNAL(triggered()), this, SLOT(close()));

  m_actionOctopi = new QAction(this);
  m_actionOctopi->setText("Octopi...");
  connect(m_actionOctopi, SIGNAL(triggered()), this, SLOT(runOctopi()));

  m_systemTrayIconMenu = new QMenu( this );
  m_systemTrayIconMenu->addAction(m_actionOctopi);
  m_systemTrayIconMenu->addSeparator();
  m_systemTrayIconMenu->addAction(m_actionExit);
  m_systemTrayIcon->setContextMenu(m_systemTrayIconMenu);

  connect ( m_systemTrayIcon , SIGNAL( activated( QSystemTrayIcon::ActivationReason ) ),
            this, SLOT( execSystemTrayActivated ( QSystemTrayIcon::ActivationReason ) ) );

  m_pacmanHelperTimer = new QTimer();
  m_pacmanHelperTimer->setInterval(1000 * 5);
  m_pacmanHelperTimer->start();
  connect(m_pacmanHelperTimer, SIGNAL(timeout()), this, SLOT(pacmanHelperTimerTimeout()));

  connect(m_pacmanDatabaseSystemWatcher,
          SIGNAL(directoryChanged(QString)), this, SLOT(refreshAppIcon()));
}

/*
 * Exec Octopi
 */
void MainWindow::runOctopi()
{
  QProcess proc;

  if(UnixCommand::getLinuxDistro() == ectn_MANJAROLINUX &&
     !WMHelper::isKDERunning())
  {
    proc.startDetached("octopi -style gtk -sysugrade");
  }
  else
  {
    proc.startDetached("octopi -sysupgrade");
  }
}

/*
 * Whenever this timer ticks, we need to call the PacmanHelper DBus interface to sync Pacman's dbs
 */
void MainWindow::pacmanHelperTimerTimeout()
{
  static bool firstTime=true;

  if (firstTime)
  {
    m_pacmanHelperTimer->setInterval(1000 * 60 * 15);
    firstTime=false;
  }

  PacmanHelperClient *client =
      new PacmanHelperClient("org.octopi.pacmanhelper", "/", QDBusConnection::systemBus(), 0);
  connect(client, SIGNAL(syncdbcompleted()), this, SLOT(afterPacmanHelperSyncDatabase()));
  client->syncdb();
}

/*
 * Called right after the PacmanHelper syncdb() method has finished!
 */
void MainWindow::afterPacmanHelperSyncDatabase()
{
  disconnect(m_pacmanDatabaseSystemWatcher,
          SIGNAL(directoryChanged(QString)), this, SLOT(refreshAppIcon()));

  int numberOfOutdatedPackages = m_numberOfOutdatedPackages;
  refreshAppIcon();

  if (numberOfOutdatedPackages != m_numberOfOutdatedPackages)
  {
    if (m_numberOfOutdatedPackages > 0)
    {
      if (m_numberOfOutdatedPackages == 1)
      {
        m_systemTrayIcon->setToolTip(StrConstants::getOneNewUpdate());
      }
      else if (m_numberOfOutdatedPackages > 1)
      {
        m_systemTrayIcon->setToolTip(StrConstants::getNewUpdates().arg(m_numberOfOutdatedPackages));
      }
    }
  }
  else
  {
    if (numberOfOutdatedPackages == 1)
    {
      m_systemTrayIcon->setToolTip(StrConstants::getOneNewUpdate());
    }
    else if (numberOfOutdatedPackages > 1)
    {
      m_systemTrayIcon->setToolTip(StrConstants::getNewUpdates().arg(numberOfOutdatedPackages));
    }
  }

  connect(m_pacmanDatabaseSystemWatcher,
          SIGNAL(directoryChanged(QString)), this, SLOT(refreshAppIcon()));
}

/*
 * If we have some outdated packages, let's put an angry red face icon in this app!
 */
void MainWindow::refreshAppIcon()
{
  disconnect(m_pacmanDatabaseSystemWatcher,
          SIGNAL(directoryChanged(QString)), this, SLOT(refreshAppIcon()));

  m_outdatedPackageList = Package::getOutdatedPackageList();
  m_numberOfOutdatedPackages = m_outdatedPackageList->count();

  if (m_numberOfOutdatedPackages == 0)
  {
    m_systemTrayIcon->setToolTip(StrConstants::getApplicationName());
  }
  else if (m_numberOfOutdatedPackages > 0)
  {
    if (m_numberOfOutdatedPackages == 1)
    {
      m_systemTrayIcon->setToolTip(StrConstants::getOneNewUpdate());
    }
    else if (m_numberOfOutdatedPackages > 1)
    {
      m_systemTrayIcon->setToolTip(StrConstants::getNewUpdates().arg(m_numberOfOutdatedPackages));
    }
  }

  if(m_outdatedPackageList->count() > 0)
  {
    m_icon = (IconHelper::getIconOctopiRed());
  }
  else
  {
    m_icon = (IconHelper::getIconOctopiYellow());
  }

  m_systemTrayIcon->setIcon(m_icon);
  connect(m_pacmanDatabaseSystemWatcher,
          SIGNAL(directoryChanged(QString)), this, SLOT(refreshAppIcon()));
}

/*
 * Whenever the user clicks on the systemTray icon...
 */
void MainWindow::execSystemTrayActivated(QSystemTrayIcon::ActivationReason ar)
{
  switch (ar)
  {
  case QSystemTrayIcon::DoubleClick:
  {
    runOctopi();
    break;
  }
  default: break;
  }
}