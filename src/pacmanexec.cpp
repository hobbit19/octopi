/*
* This file is part of Octopi, an open-source GUI for pacman.
* Copyright (C) 2013 Alexandre Albuquerque Arnt
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*/

#include <iostream>
#include "pacmanexec.h"
#include "strconstants.h"
#include "unixcommand.h"
#include "wmhelper.h"

/*
 * This class decouples pacman commands executing and parser code from Octopi's interface
 */

/*
 * Let's create the needed unixcommand object that will ultimately execute Pacman commands
 */
PacmanExec::PacmanExec(QObject *parent) : QObject(parent)
{
  m_unixCommand = new UnixCommand(parent);
  m_iLoveCandy = UnixCommand::isILoveCandyEnabled();
  m_debugMode = false;

  QObject::connect(m_unixCommand, SIGNAL( started() ), this, SLOT( onStarted()));

  QObject::connect(m_unixCommand, SIGNAL( finished ( int, QProcess::ExitStatus )),
                   this, SLOT( onFinished(int, QProcess::ExitStatus)));

  QObject::connect(m_unixCommand, SIGNAL( startedTerminal()), this, SLOT( onStarted()));

  QObject::connect(m_unixCommand, SIGNAL( finishedTerminal( int, QProcess::ExitStatus )),
                   this, SLOT( onFinished(int, QProcess::ExitStatus)));

  QObject::connect(m_unixCommand, SIGNAL( readyReadStandardOutput()),
                   this, SLOT( onReadOutput()));

  QObject::connect(m_unixCommand, SIGNAL( readyReadStandardError() ),
                   this, SLOT( onReadOutputError()));
}

/*
 * Let's remove UnixCommand temporary file...
 */
PacmanExec::~PacmanExec()
{
  m_unixCommand->removeTemporaryFile();
}

/*
 * Turns DEBUG MODE on or off
 */
void PacmanExec::setDebugMode(bool value)
{
  m_debugMode = value;
}

/*
 * Removes Octopi's temporary transaction file
 */
void PacmanExec::removeTemporaryFile()
{
  m_unixCommand->removeTemporaryFile();
}

/*
 * Searches for the presence of the db.lock file
 */
bool PacmanExec::isDatabaseLocked()
{
  QString lockFilePath("/var/lib/pacman/db.lck");
  QFile lockFile(lockFilePath);

  return (lockFile.exists());
}

/*
 * Removes Pacman DB lock file
 */
void PacmanExec::removeDatabaseLock()
{
  UnixCommand::execCommand("rm /var/lib/pacman/db.lck");
}

/*
 * Searches the given output for a series of verbs that a Pacman transaction may produce
 */
bool PacmanExec::searchForKeyVerbs(QString output)
{
  return (output.contains(QRegExp("checking ")) ||
          output.contains(QRegExp("loading ")) ||
          output.contains(QRegExp("installing ")) ||
          output.contains(QRegExp("upgrading ")) ||
          output.contains(QRegExp("downgrading ")) ||
          output.contains(QRegExp("resolving ")) ||
          output.contains(QRegExp("looking ")) ||
          output.contains(QRegExp("removing ")));
}

/*
 * Breaks the output generated by QProcess so we can parse the strings
 * and give a better feedback to our users (including showing percentages)
 *
 * Returns true if the given output was split
 */
bool PacmanExec::splitOutputStrings(QString output)
{
  bool res = true;
  QString msg = output.trimmed();
  QStringList msgs = msg.split(QRegExp("\\n"), QString::SkipEmptyParts);

  foreach (QString m, msgs)
  {
    QStringList m2 = m.split(QRegExp("\\(\\s{0,3}[0-9]{1,4}/[0-9]{1,4}\\) "), QString::SkipEmptyParts);

    if (m2.count() == 1)
    {
      //Let's try another test... if it doesn't work, we give up.
      QStringList maux = m.split(QRegExp("%"), QString::SkipEmptyParts);
      if (maux.count() > 1)
      {
        foreach (QString aux, maux)
        {
          aux = aux.trimmed();
          if (!aux.isEmpty())
          {
            if (aux.at(aux.count()-1).isDigit())
            {
              aux += "%";
            }

            if (m_debugMode) std::cout << "_split - case1: " << aux.toLatin1().data() << std::endl;
            parsePacmanProcessOutput(aux);
          }
        }
      }
      else if (maux.count() == 1)
      {
        if (!m.isEmpty())
        {
          if (m_debugMode) std::cout << "_split - case2: " << m.toLatin1().data() << std::endl;
          parsePacmanProcessOutput(m);
        }
      }
    }
    else if (m2.count() > 1)
    {
      foreach (QString m3, m2)
      {
        if (!m3.isEmpty())
        {
          if (m_debugMode) std::cout << "_split - case3: " << m3.toLatin1().data() << std::endl;
          parsePacmanProcessOutput(m3);
        }
      }
    }
    else res = false;
  }

  return res;
}

/*
 * Processes the output of the 'pacman process' so we can update percentages and messages at real time
 */
void PacmanExec::parsePacmanProcessOutput(QString output)
{
  if (m_commandExecuting == ectn_RUN_IN_TERMINAL ||
      m_commandExecuting == ectn_RUN_SYSTEM_UPGRADE_IN_TERMINAL) return;

  bool continueTesting = false;
  QString perc;
  QString msg = output;
  QString progressRun;
  QString progressEnd;

  msg.remove(QRegExp(".+\\[Y/n\\].+"));
  //Let's remove color codes from strings...
  msg.remove("\033[0;1m");
  msg.remove("\033[0m");
  msg.remove("[1;33m");
  msg.remove("[00;31m");
  msg.remove("\033[1;34m");
  msg.remove("\033[0;1m");

  msg.remove("c");
  msg.remove("C");
  msg.remove("");
  msg.remove("[m[0;37m");
  msg.remove("o");
  msg.remove("[m");
  msg.remove(";37m");
  msg.remove("[c");
  msg.remove("[mo");

  if (msg.contains("exists in filesystem") ||
      (msg.contains(":: waiting for 1 process to finish repacking")) ||
      (msg.contains(":: download complete in"))) return;

  else if (msg.contains("download complete: "))
  {
    prepareTextToPrint(msg + "<br>");
    return;
  }

  if (m_debugMode) std::cout << "_treat: " << msg.toLatin1().data() << std::endl;

  if (m_iLoveCandy)
  {
    progressRun = "m]";
    progressEnd = "100%";
  }
  else
  {
    progressRun = "-]";
    progressEnd = "#]";
  }

  //If it is a percentage, we are talking about curl output...
  if(msg.indexOf(progressEnd) != -1)
  {
    perc = "100%";
    emit percentage(100);
    continueTesting = true;
  }

  if (msg.indexOf(progressRun) != -1 || continueTesting)
  {
    if (!continueTesting){
      perc = msg.right(4).trimmed();
      if (m_debugMode) std::cout << "percentage is: " << perc.toLatin1().data() << std::endl;
    }

    continueTesting = false;

    int aux = msg.indexOf("[");
    if (aux > 0 && !msg.at(aux-1).isSpace()) return;

    QString target;
    if (m_commandExecuting == ectn_INSTALL ||
        m_commandExecuting == ectn_SYSTEM_UPGRADE ||
        m_commandExecuting == ectn_SYNC_DATABASE ||
        m_commandExecuting == ectn_REMOVE ||
        m_commandExecuting == ectn_REMOVE_INSTALL)
    {
      int ini = msg.indexOf(QRegExp("\\(\\s{0,3}[0-9]{1,4}/[0-9]{1,4}\\) "));
      if (ini == 0)
      {
        int rp = msg.indexOf(")");
        msg = msg.remove(0, rp+2);

        if (searchForKeyVerbs(msg))
        {
          int end = msg.indexOf("[");
          msg = msg.remove(end, msg.size()-end).trimmed() + " ";
          prepareTextToPrint(msg);
        }
        else
        {
          if (m_debugMode) std::cout << "test1: " << target.toLatin1().data() << std::endl;
          int pos = msg.indexOf(" ");
          if (pos >=0)
          {
            target = msg.left(pos);
            target = target.trimmed() + " ";
            if (m_debugMode) std::cout << "target: " << target.toLatin1().data() << std::endl;

            if(!target.isEmpty())
            {
              prepareTextToPrint("<b><font color=\"#b4ab58\">" + target + "</font></b>"); //#C9BE62
            }
          }
          else
          {
            prepareTextToPrint("<b><font color=\"#b4ab58\">" + msg + "</font></b>"); //#C9BE62
          }
        }
      }
      else if (ini == -1)
      {
        if (searchForKeyVerbs(msg))
        {
          if (m_debugMode) std::cout << "test2: " << msg.toLatin1().data() << std::endl;

          int end = msg.indexOf("[");
          msg = msg.remove(end, msg.size()-end);
          msg = msg.trimmed() + " ";
          prepareTextToPrint(msg);
        }
        else
        {
          int pos = msg.indexOf(" ");
          if (pos >=0)
          {
            target = msg.left(pos);
            target = target.trimmed() + " ";
            if (m_debugMode) std::cout << "target: " << target.toLatin1().data() << std::endl;

            if(!target.isEmpty() && !m_textPrinted.contains(target))
            {
              if (target.indexOf(QRegExp("[a-z]+")) != -1)
              {
                if(m_commandExecuting == ectn_SYNC_DATABASE && !target.contains("/"))
                {
                  prepareTextToPrint("<b><font color=\"#FF8040\">" +
                                      StrConstants::getSyncing() + " " + target + "</font></b>");
                }
                else if (m_commandExecuting != ectn_SYNC_DATABASE)
                {
                  prepareTextToPrint("<b><font color=\"#b4ab58\">" +
                                      target + "</font></b>"); //#C9BE62
                }
              }
            }
          }
          else
          {
            prepareTextToPrint("<b><font color=\"blue\">" + msg + "</font></b>");
          }
        }
      }
    }

    //Here we print the transaction percentage updating
    if(!perc.isEmpty() && perc.indexOf("%") > 0)
    {
      int ipercentage = perc.left(perc.size()-1).toInt();
      emit percentage(ipercentage);
    }
  }
  //It's another error, so we have to output it
  else
  {
    //Let's supress some annoying string bugs...
    msg.remove(QRegExp("\\(process.+"));
    msg.remove(QRegExp("Using the fallback.+"));
    msg.remove(QRegExp("Gkr-Message:.+"));
    msg.remove(QRegExp("kdesu.+"));
    msg.remove(QRegExp("kbuildsycoca.+"));
    msg.remove(QRegExp("Connecting to deprecated signal.+"));
    msg.remove(QRegExp("QVariant.+"));
    msg.remove(QRegExp("gksu-run.+"));
    msg.remove(QRegExp("GConf Error:.+"));
    msg.remove(QRegExp(":: Do you want.+"));
    msg.remove(QRegExp("org\\.kde\\."));
    msg.remove(QRegExp("QCommandLineParser"));
    msg.remove(QRegExp("QCoreApplication.+"));
    msg.remove(QRegExp("Fontconfig warning.+"));
    msg.remove(QRegExp("reading configurations from.+"));
    msg.remove(QRegExp(".+annot load library.+"));
    msg = msg.trimmed();

    if (m_debugMode) std::cout << "debug: " << msg.toLatin1().data() << std::endl;

    QString order;
    int ini = msg.indexOf(QRegExp("\\(\\s{0,3}[0-9]{1,4}/[0-9]{1,4}\\) "));
    if (ini == 0)
    {
      int rp = msg.indexOf(")");
      order = msg.left(rp+2);
      msg = msg.remove(0, rp+2);
    }

    if (!msg.isEmpty())
    {
      if (msg.contains(QRegExp("removing ")) && !m_textPrinted.contains(msg + " "))
      {
        //Does this package exist or is it a proccessOutput buggy string???
        QString pkgName = msg.mid(9).trimmed();

        if (pkgName.indexOf("...") != -1 || UnixCommand::isPackageInstalled(pkgName))
        {
          prepareTextToPrint("<b><font color=\"#E55451\">" + msg + "</font></b>"); //RED
        }
      }
      else
      {
        QString altMsg = msg;

        if (msg.contains(":: Updating") && m_commandExecuting == ectn_SYNC_DATABASE)
          prepareTextToPrint("<br><b>" + StrConstants::getSyncDatabases() + " (pkgfile -u)</b><br>");

        else if (msg.contains("download complete: "))
          prepareTextToPrint(altMsg);

        else if (msg.indexOf(":: Synchronizing package databases...") == -1 &&
            msg.indexOf(":: Starting full system upgrade...") == -1)
        {
          if (m_debugMode) std::cout << "Print in black: " << msg.toLatin1().data() << std::endl;

          if (m_commandExecuting == ectn_SYNC_DATABASE &&
              msg.indexOf("is up to date"))
          {
            emit percentage(100);

            int blank = msg.indexOf(" ");
            QString repo = msg.left(blank);

            if (repo.contains("error", Qt::CaseInsensitive) ||
                repo.contains("gconf", Qt::CaseInsensitive) ||
                repo.contains("failed", Qt::CaseInsensitive) ||
                repo.contains("fontconfig", Qt::CaseInsensitive) ||
                repo.contains("reading", Qt::CaseInsensitive)) return;

            altMsg = repo + " " + StrConstants::getIsUpToDate();
          }

          prepareTextToPrint(altMsg); //BLACK
        }
      }
    }
  }
}

/*
 * Prepares a string parsed from pacman output to be printed by the UI
 */
void PacmanExec::prepareTextToPrint(QString str, TreatString ts, TreatURLLinks tl)
{
  if (m_debugMode) std::cout << "_print: " << str.toLatin1().data() << std::endl;

  if (ts == ectn_DONT_TREAT_STRING)
  {
    emit textToPrintExt(str);
    return;
  }

  //If the str waiting to being print is from curl status OR any other unwanted string...
  if ((str.contains(QRegExp("\\(\\d")) &&
       (!str.contains("target", Qt::CaseInsensitive)) &&
       (!str.contains("package", Qt::CaseInsensitive))) ||
      (str.contains(QRegExp("\\d\\)")) &&
       (!str.contains("target", Qt::CaseInsensitive)) &&
       (!str.contains("package", Qt::CaseInsensitive))) ||

      str.indexOf("Enter a selection", Qt::CaseInsensitive) == 0 ||
      str.indexOf("Proceed with", Qt::CaseInsensitive) == 0 ||
      str.indexOf("%") != -1 ||
      str.indexOf("[") != -1 ||
      str.indexOf("]") != -1 ||
      str.indexOf("---") != -1)
  {
    return;
  }

  //If the str waiting to being print has not yet been printed...
  if(m_textPrinted.contains(str))
  {
    return;
  }

  QString newStr = str;

  if(newStr.contains(QRegExp("<font color")))
  {
    newStr += "<br>";
  }
  else
  {
    if(newStr.contains("removing ") ||
       newStr.contains("could not ") ||
       newStr.contains("error:", Qt::CaseInsensitive) ||
       newStr.contains("failed") ||
       newStr.contains("is not synced") ||
       newStr.contains("could not be found") ||
       newStr.contains(StrConstants::getCommandFinishedWithErrors()))
    {
      newStr = "<b><font color=\"#E55451\">" + newStr + "&nbsp;</font></b>"; //RED
    }
    else if(newStr.contains("checking ") ||
            newStr.contains("is synced") ||
            newStr.contains("-- reinstalling") ||
            newStr.contains("installing ") ||
            newStr.contains("upgrading ") ||
            newStr.contains("loading ") ||
            newStr.contains("resolving ") ||
            newStr.contains("looking "))
    {
      newStr = "<b><font color=\"#4BC413\">" + newStr + "</font></b>"; //GREEN
    }
    else if (newStr.contains("warning", Qt::CaseInsensitive) || (newStr.contains("downgrading")))
    {
      newStr = "<b><font color=\"#FF8040\">" + newStr + "</font></b>"; //ORANGE
    }
    else if (!newStr.contains("::"))
    {
      newStr += "<br>";
    }
  }

  if (newStr.contains("::"))
  {
    newStr = "<br><B>" + newStr + "</B><br><br>";
  }

  if (!newStr.contains(QRegExp("<br"))) //It was an else!
  {
    newStr += "<br>";
  }

  if (tl == ectn_TREAT_URL_LINK)
    newStr = Package::makeURLClickable(newStr);

  m_textPrinted.append(str);

  emit textToPrintExt(newStr);
}

/*
 * Whenever QProcess starts the pacman command...
 */
void PacmanExec::onStarted()
{
  //First we output the name of action we are starting to execute!
  if (m_commandExecuting == ectn_MIRROR_CHECK)
  {
    prepareTextToPrint("<b>" + StrConstants::getSyncMirror() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_SYNC_DATABASE)
  {
    prepareTextToPrint("<b>" + StrConstants::getSyncDatabases() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_SYSTEM_UPGRADE || m_commandExecuting == ectn_RUN_SYSTEM_UPGRADE_IN_TERMINAL)
  {
    prepareTextToPrint("<b>" + StrConstants::getSystemUpgrade() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_REMOVE)
  {
    prepareTextToPrint("<b>" + StrConstants::getRemovingPackages() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_INSTALL)
  {
    prepareTextToPrint("<b>" + StrConstants::getInstallingPackages() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_REMOVE_INSTALL)
  {
    prepareTextToPrint("<b>" + StrConstants::getRemovingAndInstallingPackages() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_RUN_IN_TERMINAL)
  {
    prepareTextToPrint("<b>" + StrConstants::getRunningCommandInTerminal() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }

  QString output = m_unixCommand->readAllStandardOutput();
  output = output.trimmed();

  if (!output.isEmpty())
  {
    prepareTextToPrint(output);
  }

  emit started();
}

/*
 * Whenever QProcess' read output is retrieved...
 */
void PacmanExec::onReadOutput()
{
  if (m_commandExecuting == ectn_MIRROR_CHECK)
  {
    QString output = m_unixCommand->readAllStandardOutput();

    output.remove("[01;33m");
    output.remove("\033[01;37m");
    output.remove("\033[00m");
    output.remove("\033[00;32m");
    output.remove("[00;31m");
    output.replace("[", "'");
    output.replace("]", "'");
    output.remove("\n");

    if (output.contains("Checking", Qt::CaseInsensitive))
    {
      output += "<br>";
    }

    prepareTextToPrint(output, ectn_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);

    emit readOutput();
    return;
  }

  if (WMHelper::getSUCommand().contains("kdesu"))
  {
    QString output = m_unixCommand->readAllStandardOutput();

    if (m_commandExecuting == ectn_SYNC_DATABASE &&
        output.contains("Usage: /usr/bin/kdesu [options] command"))
    {
      emit readOutput();
      return;
    }

    output = output.remove("Fontconfig warning: \"/etc/fonts/conf.d/50-user.conf\", line 14:");
    output = output.remove("reading configurations from ~/.fonts.conf is deprecated. please move it to /home/arnt/.config/fontconfig/fonts.conf manually");

    if (!output.trimmed().isEmpty())
    {
      splitOutputStrings(output);
    }
  }
  else if (WMHelper::getSUCommand().contains("gksu"))
  {
    QString output = m_unixCommand->readAllStandardOutput();
    output = output.trimmed();

    if(!output.isEmpty() &&
       output.indexOf(":: Synchronizing package databases...") == -1 &&
       output.indexOf(":: Starting full system upgrade...") == -1)
    {
      prepareTextToPrint(output);
    }
  }

  emit readOutput();
}

/*
 * Whenever QProcess' read error output is retrieved...
 */
void PacmanExec::onReadOutputError()
{
  if (m_commandExecuting == ectn_MIRROR_CHECK)
  {
    QString output = m_unixCommand->readAllStandardError();

    output.remove("[01;33m");
    output.remove("\033[01;37m");
    output.remove("\033[00m");
    output.remove("\033[00;32m");
    output.remove("[00;31m");
    output.remove("\n");

    if (output.contains("Checking"), Qt::CaseInsensitive)
      output += "<br>";

    prepareTextToPrint(output, ectn_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);

    emit readOutputError();
    return;
  }

  QString msg = m_unixCommand->readAllStandardError();
  msg = msg.remove("Fontconfig warning: \"/etc/fonts/conf.d/50-user.conf\", line 14:");
  msg = msg.remove("reading configurations from ~/.fonts.conf is deprecated. please move it to /home/arnt/.config/fontconfig/fonts.conf manually");

  if (!msg.trimmed().isEmpty())
  {
    splitOutputStrings(msg);
  }

  emit readOutputError();
}

/*
 * Whenever QProcess finishes the pacman command...
 */
void PacmanExec::onFinished(int exitCode, QProcess::ExitStatus es)
{
  if (m_commandExecuting == ectn_REMOVE_KCP_PKG)
  {
    if (UnixCommand::getLinuxDistro() == ectn_KAOS &&
        UnixCommand::hasTheExecutable("kcp") &&
        !UnixCommand::isRootRunning())

      UnixCommand::execCommandAsNormalUser("kcp -u");
  }

  emit finished(exitCode, es);
}

// --------------------- DO METHODS ------------------------------------

/*
 * Calls mirro-check to check mirrors and returns output to UI
 */
void PacmanExec::doMirrorCheck()
{
  m_commandExecuting = ectn_MIRROR_CHECK;
  m_unixCommand->executeCommandAsNormalUser(ctn_MIRROR_CHECK_APP);
}

/*
 * Calls pacman to install given packages and returns output to UI
 */
void PacmanExec::doInstall(const QString &listOfPackages)
{
  QString command = "pacman -S --noconfirm " + listOfPackages;

  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -S " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_INSTALL;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to install given packages inside a terminal
 */
void PacmanExec::doInstallInTerminal(const QString &listOfPackages)
{
  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -S " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to install given LOCAL packages and returns output to UI
 */
void PacmanExec::doInstallLocal(const QString &listOfPackages)
{
  QString command = "pacman -U --force --noconfirm " + listOfPackages;

  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -U --force " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_INSTALL;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to install given LOCAL packages inside a terminal
 */
void PacmanExec::doInstallLocalInTerminal(const QString &listOfPackages)
{
  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -U --force " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to remove given packages and returns output to UI
 */
void PacmanExec::doRemove(const QString &listOfPackages)
{
  QString command = "pacman -R --noconfirm " + listOfPackages;

  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -R " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_REMOVE;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to remove given packages inside a terminal
 */
void PacmanExec::doRemoveInTerminal(const QString &listOfPackages)
{
  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -R " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to remove and install given packages and returns output to UI
 */
void PacmanExec::doRemoveAndInstall(const QString &listOfPackagestoRemove, const QString &listOfPackagestoInstall)
{
  QString command = "pacman -R --noconfirm " + listOfPackagestoRemove +
      "; pacman -S --noconfirm " + listOfPackagestoInstall;

  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -R " + listOfPackagestoRemove + ";");
  m_lastCommandList.append("pacman -S " + listOfPackagestoInstall + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_REMOVE_INSTALL;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to remove and install given packages inside a terminal
 */
void PacmanExec::doRemoveAndInstallInTerminal(const QString &listOfPackagestoRemove, const QString &listOfPackagestoInstall)
{
  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -R " + listOfPackagestoRemove + ";");
  m_lastCommandList.append("pacman -S " + listOfPackagestoInstall + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to upgrade the entire system and returns output to UI
 */
void PacmanExec::doSystemUpgrade()
{
  QString command = "pacman -Su --noconfirm";

  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -Su;");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_SYSTEM_UPGRADE;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to upgrade the entire system inside a terminal
 */
void PacmanExec::doSystemUpgradeInTerminal()
{
  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -Su;");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_SYSTEM_UPGRADE_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to sync databases and returns output to UI
 */
void PacmanExec::doSyncDatabase()
{
  QString command;

  if (UnixCommand::isRootRunning())
    command = "pacman -Sy";
  else
    command = "pacman -Syy";

  if (UnixCommand::hasTheExecutable("pkgfile") && !UnixCommand::isRootRunning())
    command += "; pkgfile -u";

  m_commandExecuting = ectn_SYNC_DATABASE;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls AUR tool to upgrade given packages inside a terminal
 */
void PacmanExec::doAURUpgrade(const QString &listOfPackages)
{
  m_lastCommandList.clear();

  if (StrConstants::getForeignRepositoryToolName() == "pacaur")
  {
    m_lastCommandList.append(StrConstants::getForeignRepositoryToolName() + " -Sa " + listOfPackages + ";");
  }
  else if (StrConstants::getForeignRepositoryToolName() == "yaourt")
  {
    m_lastCommandList.append(StrConstants::getForeignRepositoryToolName() + " -S " + listOfPackages + ";");
  }

  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminalAsNormalUser(m_lastCommandList);
}

/*
 * Calls AUR tool to install given packages inside a terminal
 */
void PacmanExec::doAURInstall(const QString &listOfPackages)
{
  m_lastCommandList.clear();

  if (UnixCommand::getLinuxDistro() == ectn_KAOS)
    m_lastCommandList.append(StrConstants::getForeignRepositoryToolName() + " -i " + listOfPackages + ";");
  else if (StrConstants::getForeignRepositoryToolName() == "pacaur")
    m_lastCommandList.append(StrConstants::getForeignRepositoryToolName() + " -Sa " + listOfPackages + ";");
  else if (StrConstants::getForeignRepositoryToolName() == "yaourt")
    m_lastCommandList.append(StrConstants::getForeignRepositoryToolName() + " -S " + listOfPackages + ";");
  else if (StrConstants::getForeignRepositoryToolName() == "chaser")
    m_lastCommandList.append(StrConstants::getForeignRepositoryToolName() + " install " + listOfPackages + ";");

  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminalAsNormalUser(m_lastCommandList);
}

/*
 * Calls AUR tool to remove given packages inside a terminal
 */
void PacmanExec::doAURRemove(const QString &listOfPackages)
{
  m_lastCommandList.clear();

  if (StrConstants::getForeignRepositoryToolName() == "chaser" ||
      StrConstants::getForeignRepositoryToolName() == "kcp")
  {
    m_lastCommandList.append("pacman -R " + listOfPackages + ";");
  }
  else
  {
    m_lastCommandList.append(StrConstants::getForeignRepositoryToolName() +
                             " -R " + listOfPackages + ";");
  }

  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  if (StrConstants::getForeignRepositoryToolName() == "kcp")
    m_commandExecuting = ectn_REMOVE_KCP_PKG;
  else
    m_commandExecuting = ectn_RUN_IN_TERMINAL;

  if (StrConstants::getForeignRepositoryToolName() != "yaourt" && StrConstants::getForeignRepositoryToolName() != "pacaur")
    m_unixCommand->runCommandInTerminal(m_lastCommandList);
  else
    m_unixCommand->runCommandInTerminalAsNormalUser(m_lastCommandList);
}

/*
 * Runs latest command inside a terminal (probably due to some previous error)
 */
void PacmanExec::runLastestCommandInTerminal()
{
  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}
