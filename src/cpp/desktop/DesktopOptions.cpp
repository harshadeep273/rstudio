/*
 * DesktopOptions.cpp
 *
 * Copyright (C) 2009-18 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include "DesktopOptions.hpp"

#include <QtGui>
#include <QApplication>
#include <QDesktopWidget>

#include <core/Random.hpp>
#include <core/system/System.hpp>
#include <core/system/Environment.hpp>

#include "DesktopInfo.hpp"
#include "DesktopUtils.hpp"

using namespace rstudio::core;

namespace rstudio {
namespace desktop {

#ifdef _WIN32
// Defined in DesktopRVersion.cpp
QString binDirToHomeDir(QString binDir);
#endif

QString scratchPath;

Options& options()
{
   static Options singleton;
   return singleton;
}

void Options::initFromCommandLine(const QStringList& arguments)
{
   for (int i=1; i<arguments.size(); i++)
   {
      const QString &arg = arguments.at(i);
      if (arg == QString::fromUtf8(kRunDiagnosticsOption))
         runDiagnostics_ = true;
   }

   // synchronize zoom level with desktop frame
   desktopInfo().setZoomLevel(zoomLevel());
}

void Options::restoreMainWindowBounds(QMainWindow* win)
{
   // NOTE: win->saveGeometry and win->restoreGeometry are unreliable in
   // Qt 5.11.1 (they do not successfully restore the window size if the
   // display bounds have changed) so we explicitly save and restore the
   // bounds as a rectangle
   QString key = QStringLiteral("mainwindow/bounds");
   if (settings_.contains(key))
   {
      QRect bounds = settings_.value(key).toRect();
      win->setGeometry(bounds);
   }
   else
   {
      QSize size = QSize(1200, 900).boundedTo(
            QApplication::desktop()->availableGeometry().size());
      if (size.width() > 800 && size.height() > 500)
      {
         // Only use default size if it seems sane; otherwise let Qt set it
         win->resize(size);
      }
   }
}

void Options::saveMainWindowBounds(QMainWindow* win)
{
   // NOTE: win->saveGeometry and win->restoreGeometry are unreliable in
   // Qt 5.11.1 (they do not successfully restore the window size if the
   // display bounds have changed) so we explicitly save and restore the
   // bounds as a rectangle
   QVariant bounds = win->geometry();
   settings_.setValue(QStringLiteral("mainwindow/bounds"), bounds);
}

QString Options::portNumber() const
{
   // lookup / generate on demand
   if (portNumber_.length() == 0)
   {
      // Use a random-ish port number to avoid collisions between different
      // instances of rdesktop-launched rsessions
      int base = std::abs(core::random::uniformRandomInteger<int>());
      portNumber_ = QString::number((base % 40000) + 8080);

      // recalculate the local peer and set RS_LOCAL_PEER so that
      // rsession and it's children can use it
#ifdef _WIN32
      QString localPeer = QString::fromUtf8("\\\\.\\pipe\\") +
                          portNumber_ + QString::fromUtf8("-rsession");
      localPeer_ = localPeer.toUtf8().constData();
      core::system::setenv("RS_LOCAL_PEER", localPeer_);
#endif
   }

   return portNumber_;
}

QString Options::newPortNumber()
{
   portNumber_.clear();
   return portNumber();
}

std::string Options::localPeer() const
{
   return localPeer_;
}

QString Options::desktopRenderingEngine() const
{
   return settings_.value(QStringLiteral("desktop.renderingEngine")).toString();
}

void Options::setDesktopRenderingEngine(QString engine)
{
   settings_.setValue(QStringLiteral("desktop.renderingEngine"), engine);
}

namespace {
QString findFirstMatchingFont(const QStringList& fonts,
                              QString defaultFont,
                              bool fixedWidthOnly)
{
   for (int i = 0; i < fonts.size(); i++)
   {
      QFont font(fonts.at(i));
      if (font.exactMatch())
         if (!fixedWidthOnly || isFixedWidthFont(QFont(fonts.at(i))))
            return fonts.at(i);
   }
   return defaultFont;
}
} // anonymous namespace

QString Options::proportionalFont() const
{
   static QString detectedFont;

   QString font =
         settings_.value(QString::fromUtf8("font.proportional")).toString();
   if (!font.isEmpty())
   {
      return font;
   }

   if (!detectedFont.isEmpty())
      return detectedFont;

   QStringList fontList;
#if defined(_WIN32)
   fontList <<
           QString::fromUtf8("Segoe UI") << QString::fromUtf8("Verdana") <<  // Windows
           QString::fromUtf8("Lucida Sans") << QString::fromUtf8("DejaVu Sans") <<  // Linux
           QString::fromUtf8("Lucida Grande") <<          // Mac
           QString::fromUtf8("Helvetica");
#elif defined(__APPLE__)
   fontList <<
           QString::fromUtf8("Lucida Grande") <<          // Mac
           QString::fromUtf8("Lucida Sans") << QString::fromUtf8("DejaVu Sans") <<  // Linux
           QString::fromUtf8("Segoe UI") << QString::fromUtf8("Verdana") <<  // Windows
           QString::fromUtf8("Helvetica");
#else
   fontList <<
           QString::fromUtf8("Lucida Sans") << QString::fromUtf8("DejaVu Sans") <<  // Linux
           QString::fromUtf8("Lucida Grande") <<          // Mac
           QString::fromUtf8("Segoe UI") << QString::fromUtf8("Verdana") <<  // Windows
           QString::fromUtf8("Helvetica");
#endif

   QString sansSerif = QStringLiteral("sans-serif");
   QString selectedFont = findFirstMatchingFont(fontList, sansSerif, false);

   // NOTE: browsers will refuse to render a default font if the name is in
   // quotes; e.g. "sans-serif" is a signal to look for a font called sans-serif
   // rather than use the default sans-serif font!
   if (selectedFont == sansSerif)
      return sansSerif;
   else
      return QStringLiteral("\"%1\"").arg(selectedFont);
}

void Options::setFixedWidthFont(QString font)
{
   if (font.isEmpty())
      settings_.remove(QString::fromUtf8("font.fixedWidth"));
   else
      settings_.setValue(QString::fromUtf8("font.fixedWidth"),
                         font);
}

QString Options::fixedWidthFont() const
{
   static QString detectedFont;

   QString font =
         settings_.value(QString::fromUtf8("font.fixedWidth")).toString();
   if (!font.isEmpty())
   {
      return QString::fromUtf8("\"") + font + QString::fromUtf8("\"");
   }

   if (!detectedFont.isEmpty())
      return detectedFont;

   QStringList fontList;
   fontList <<
#if defined(Q_OS_MAC)
           QString::fromUtf8("Monaco")
#elif defined (Q_OS_LINUX)
           QString::fromUtf8("Ubuntu Mono") << QString::fromUtf8("Droid Sans Mono") << QString::fromUtf8("DejaVu Sans Mono") << QString::fromUtf8("Monospace")
#else
           QString::fromUtf8("Lucida Console") << QString::fromUtf8("Consolas") // Windows;
#endif
           ;

   // NOTE: browsers will refuse to render a default font if the name is in
   // quotes; e.g. "monospace" is a signal to look for a font called monospace
   // rather than use the default monospace font!
   QString monospace = QStringLiteral("monospace");
   QString matchingFont = findFirstMatchingFont(fontList, monospace, true);
   if (matchingFont == monospace)
      return monospace;
   else
      return QStringLiteral("\"%1\"").arg(matchingFont);
}


double Options::zoomLevel() const
{
   QVariant zoom = settings_.value(QString::fromUtf8("view.zoomLevel"), 1.0);
   return zoom.toDouble();
}

void Options::setZoomLevel(double zoomLevel)
{
   desktopInfo().setZoomLevel(zoomLevel);
   settings_.setValue(QString::fromUtf8("view.zoomLevel"), zoomLevel);
}

bool Options::enableAccessibility() const
{
   QVariant accessibility = settings_.value(QString::fromUtf8("view.accessibility"), false);
   return accessibility.toBool();
}

void Options::setEnableAccessibility(bool enable)
{
   settings_.setValue(QString::fromUtf8("view.accessibility"), enable);
}

bool Options::clipboardMonitoring() const
{
   QVariant monitoring = settings_.value(QString::fromUtf8("clipboard.monitoring"), true);
   return monitoring.toBool();
}

void Options::setClipboardMonitoring(bool monitoring)
{
   settings_.setValue(QString::fromUtf8("clipboard.monitoring"), monitoring);
}

bool Options::ignoreGpuBlacklist() const
{
   QVariant ignore = settings_.value(QStringLiteral("general.ignoreGpuBlacklist"), false);
   return ignore.toBool();
}

void Options::setIgnoreGpuBlacklist(bool ignore)
{
   settings_.setValue(QStringLiteral("general.ignoreGpuBlacklist"), ignore);
}

bool Options::disableGpuDriverBugWorkarounds() const
{
   QVariant disable = settings_.value(QStringLiteral("general.disableGpuDriverBugWorkarounds"), false);
   return disable.toBool();
}

void Options::setDisableGpuDriverBugWorkarounds(bool disable)
{
   settings_.setValue(QStringLiteral("general.disableGpuDriverBugWorkarounds"), disable);
}

#ifdef _WIN32
QString Options::rBinDir() const
{
   QString value = settings_.value(QString::fromUtf8("RBinDir")).toString();
   return value.isNull() ? QString() : value;
}

void Options::setRBinDir(QString path)
{
   settings_.setValue(QString::fromUtf8("RBinDir"), path);
}

#endif

FilePath Options::scriptsPath() const
{
   return scriptsPath_;
}

void Options::setScriptsPath(const FilePath& scriptsPath)
{
   scriptsPath_ = scriptsPath;
}

FilePath Options::executablePath() const
{
   if (executablePath_.empty())
   {
      Error error = core::system::executablePath(QApplication::arguments().at(0).toUtf8(),
                                                 &executablePath_);
      if (error)
         LOG_ERROR(error);
   }
   return executablePath_;
}

FilePath Options::supportingFilePath() const
{
   if (supportingFilePath_.empty())
   {
      // default to install path
      core::system::installPath("..",
                                QApplication::arguments().at(0).toUtf8(),
                                &supportingFilePath_);

      // adapt for OSX resource bundles
#ifdef __APPLE__
         if (supportingFilePath_.complete("Info.plist").exists())
            supportingFilePath_ = supportingFilePath_.complete("Resources");
#endif
   }
   return supportingFilePath_;
}

FilePath Options::resourcesPath() const
{
   if (resourcesPath_.empty())
   {
      if (scriptsPath().complete("resources").exists())
      {
         // developer configuration: the 'resources' folder is
         // a sibling of the RStudio executable
         resourcesPath_ = scriptsPath().complete("resources");
      }
      else
      {
         // release configuration: the 'resources' folder is
         // part of the supporting files folder
         resourcesPath_ = supportingFilePath().complete("resources");
      }
   }

   return resourcesPath_;
}

FilePath Options::wwwDocsPath() const
{
   FilePath supportingFilePath = desktop::options().supportingFilePath();
   FilePath wwwDocsPath = supportingFilePath.complete("www/docs");
   if (!wwwDocsPath.exists())
      wwwDocsPath = supportingFilePath.complete("../gwt/www/docs");
#ifdef __APPLE__
   if (!wwwDocsPath.exists())
      wwwDocsPath = supportingFilePath.complete("../../../../../gwt/www/docs");
#endif
   return wwwDocsPath;
}

#ifdef _WIN32

FilePath Options::urlopenerPath() const
{
   FilePath parentDir = scriptsPath();

   // detect dev configuration
   if (parentDir.filename() == "desktop")
      parentDir = parentDir.complete("urlopener");

   return parentDir.complete("urlopener.exe");
}

FilePath Options::rsinversePath() const
{
   FilePath parentDir = scriptsPath();

   // detect dev configuration
   if (parentDir.filename() == "desktop")
      parentDir = parentDir.complete("synctex/rsinverse");

   return parentDir.complete("rsinverse.exe");
}

#endif

QStringList Options::ignoredUpdateVersions() const
{
   return settings_.value(QString::fromUtf8("ignoredUpdateVersions"), QStringList()).toStringList();
}

void Options::setIgnoredUpdateVersions(const QStringList& ignoredVersions)
{
   settings_.setValue(QString::fromUtf8("ignoredUpdateVersions"), ignoredVersions);
}

core::FilePath Options::scratchTempDir(core::FilePath defaultPath)
{
   core::FilePath dir(scratchPath.toUtf8().constData());

   if (!dir.empty() && dir.exists())
   {
      dir = dir.childPath("tmp");
      core::Error error = dir.ensureDirectory();
      if (!error)
         return dir;
   }
   return defaultPath;
}

void Options::cleanUpScratchTempDir()
{
   core::FilePath temp = scratchTempDir(core::FilePath());
   if (!temp.empty())
      temp.removeIfExists();
}

} // namespace desktop
} // namespace rstudio
