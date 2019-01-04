/****************************************************************************************
 * Copyright (c) 2008 Aaron Seigo <aseigo@kde.org>                                      *
 * Copyright (c) 2009 Leo Franchi <lfranchi@kde.org>                                    *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) any later           *
 * version.                                                                             *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#include "../AmarokContextPackageStructure.h"

#include <iostream>

#include <QApplication>
#include <QAction>
#include <QCommandLineParser>
#include <QDBusInterface>
#include <QDir>
#include <QLocale>
#include <QStandardPaths>
#include <QTextStream>

#include <KAboutData>
#include <KLocalizedString>
#include <KShell>
#include <KProcess>
#include <KConfigGroup>
#include <KPackage/PackageLoader>
#include <KPackage/Package>


static const char description[] = "Install, list, remove Amarok applets";
static const char version[] = "0.2";


void output(const QString &msg)
{
    std::cout << msg.toLocal8Bit().constData() << std::endl;
}

void runKbuildsycoca()
{
    QDBusInterface dbus("org.kde.kded", "/kbuildsycoca", "org.kde.kbuildsycoca");
    dbus.call(QDBus::NoBlock, "recreate");
}

QStringList packages()
{
    auto loader = KPackage::PackageLoader::self();
    auto structure = new AmarokContextPackageStructure;
    loader->addKnownPackageStructure(QStringLiteral("Amarok/ContextApplet"), structure);
    auto applets = loader->findPackages(QStringLiteral("Amarok/ContextApplet"),
                                        QString(),
                                        [] (const KPluginMetaData &data)
                                        { return data.serviceTypes().contains(QStringLiteral("Amarok/ContextApplet")); });

    QStringList result;

    for (const auto &applet : applets)
        result << applet.pluginId();

    return result;
}

void listPackages()
{
    QStringList list = packages();
    list.sort();
    foreach(const QString& package, list) {
        output(package);
    }
}

int main(int argc, char **argv)
{
    KAboutData aboutData("amarokpkg", i18n("Amarok Applet Manager"),
                         version, i18n(description), KAboutLicense::GPL,
                         i18n("(C) 2008, Aaron Seigo, (C) 2009, Leo Franchi"));
    aboutData.addAuthor( i18n("Aaron Seigo"),
                         i18n("Original author"),
                        "aseigo@kde.org" );
    aboutData.addAuthor( i18n( "Leo Franchi" ),
                         i18n( "Developer" ) ,
                         "lfranchi@kde.org"  );

    QApplication app(argc, argv);
    app.setApplicationName("amarokpkg");
    app.setOrganizationDomain("kde.org");
    app.setApplicationDisplayName(i18n("Amarok Applet Manager"));
    app.setApplicationVersion(version);

    /**
     * TODO: DO WE NEED THIS ?
     */
    KAboutData::setApplicationData(aboutData);

    QCommandLineParser parser;

    parser.addOption(QCommandLineOption(QStringList() << "g" << "global",
                                        i18n("For install or remove, operates on applets installed for all users.")));
    parser.addOption(QCommandLineOption(QStringList() << "s" << "i" << "install <path>",
                                        i18nc("Do not translate <path>", "Install the applet at <path>")));
    parser.addOption(QCommandLineOption(QStringList() << "u" << "upgrade <path>",
                                        i18nc("Do not translate <path>", "Upgrade the applet at <path>")));
    parser.addOption(QCommandLineOption(QStringList() << "l" << "list",
                                        i18n("Most installed applets")));
    parser.addOption(QCommandLineOption(QStringList() << "r" << "remove <name>",
                                        i18nc("Do not translate <name>", "Remove the applet named <name>")));
    parser.addOption(QCommandLineOption(QStringList() << "p" << "packageroot <path>",
                                        i18n("Absolute path to the package root. If not supplied, then the standard data directories for this KDE session will be searched instead.")));

    parser.addVersionOption();
    parser.addHelpOption();
    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    QString packageRoot = "kpackage/amarok";
    KPackage::Package *installer = 0;

    if (parser.isSet("list")) {
        listPackages();
    } else {
        // install, remove or upgrade
        if (!installer)
            installer = new KPackage::Package(new AmarokContextPackageStructure);

        if (parser.isSet("packageroot")) {
            packageRoot = parser.value("packageroot");
        } else if (parser.isSet("global")) {
            packageRoot = QStandardPaths::locate(QStandardPaths::GenericDataLocation, packageRoot);
        } else {
            //@FIXME Maybe we need to check whether the folders exist or not.
            packageRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + packageRoot;
        }

        QString package;
        QString packageFile;
        if (parser.isSet("remove")) {
            package = parser.value("remove");
        } else if (parser.isSet("upgrade")) {
            package = parser.value("upgrade");
        } else if (parser.isSet("install")) {
            package = parser.value("install");
        }
        if (!QDir::isAbsolutePath(package)) {
            packageFile = QDir(QDir::currentPath() + QLatin1Char('/') + package).absolutePath();
        } else {
            packageFile = package;
        }

        if (parser.isSet("remove") || parser.isSet("upgrade")) {
            installer->setPath(packageFile);
            KPluginMetaData metadata = installer->metadata();

            QString pluginId;
            if (metadata.name().isEmpty()) {
                // plugin name given in command line
                pluginId = package;
            } else {
                // Parameter was a plasma package, get plugin name from the package
                pluginId = metadata.pluginId();
            }

            QStringList installed = packages();
            if (installed.contains(pluginId)) {
                if (installer->uninstall(pluginId, packageRoot)) {
                    output(i18n("Successfully removed %1", pluginId));
                } else if (!parser.isSet("upgrade")) {
                    output(i18n("Removal of %1 failed.", pluginId));
                    delete installer;
                    return 1;
                }
            } else {
                output(i18n("Plugin %1 is not installed.", pluginId));
            }
        }
        if (parser.isSet("install") || parser.isSet("upgrade")) {
            if (installer->install(packageFile, packageRoot)) {
                output(i18n("Successfully installed %1", packageFile));
                runKbuildsycoca();
            } else {
                output(i18n("Installation of %1 failed.", packageFile));
                delete installer;
                return 1;
            }
        }

        if (package.isEmpty()) {
            QTextStream out(stdout);
            out << i18nc("No option was given, this is the error message telling the user he needs at least one, do not translate install, remove, upgrade nor list", "One of install, remove, upgrade or list is required.");
        } else {
            runKbuildsycoca();
        }
    }
    delete installer;
    return 0;
}



