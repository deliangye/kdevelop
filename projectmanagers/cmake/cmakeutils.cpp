/* KDevelop CMake Support
 *
 * Copyright 2009 Andreas Pakulat <apaku@gmx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "cmakeutils.h"

#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QRegularExpression>

#include <KLocalizedString>
#include <kconfiggroup.h>

#include <project/projectmodel.h>
#include <interfaces/iproject.h>
#include <interfaces/icore.h>
#include <interfaces/iplugincontroller.h>
#include <QStandardPaths>

#include "icmakedocumentation.h"
#include "cmakebuilddirchooser.h"
#include "settings/cmakecachemodel.h"
#include "debug.h"
#include "cmakebuilderconfig.h"
#include <cmakecachereader.h>

using namespace KDevelop;

namespace Config
{
namespace Old
{
static const QString currentBuildDirKey = QStringLiteral("CurrentBuildDir");
static const QString currentCMakeBinaryKey = QStringLiteral("Current CMake Binary");
static const QString currentBuildTypeKey = QStringLiteral("CurrentBuildType");
static const QString currentInstallDirKey = QStringLiteral("CurrentInstallDir");
static const QString currentEnvironmentKey = QStringLiteral("CurrentEnvironment");
static const QString currentExtraArgumentsKey = QStringLiteral("Extra Arguments");
static const QString projectRootRelativeKey = QStringLiteral("ProjectRootRelative");
static const QString projectBuildDirs = QStringLiteral("BuildDirs");
}

static const QString buildDirIndexKey = QStringLiteral("Current Build Directory Index");
static const QString buildDirOverrideIndexKey = QStringLiteral("Temporary Build Directory Index");
static const QString buildDirCountKey = QStringLiteral("Build Directory Count");

namespace Specific
{
static const QString buildDirPathKey = QStringLiteral("Build Directory Path");
// TODO: migrate to more generic & consistent key term "CMake Executable"
static const QString cmakeExecutableKey = QStringLiteral("CMake Binary");
static const QString cmakeBuildTypeKey = QStringLiteral("Build Type");
static const QString cmakeInstallDirKey = QStringLiteral("Install Directory");
static const QString cmakeEnvironmentKey = QStringLiteral("Environment Profile");
static const QString cmakeArgumentsKey = QStringLiteral("Extra Arguments");
static const QString buildDirRuntime = QStringLiteral("Runtime");
}

static const QString groupNameBuildDir = QStringLiteral("CMake Build Directory %1");
static const QString groupName = QStringLiteral("CMake");

} // namespace Config

namespace
{

KConfigGroup baseGroup( KDevelop::IProject* project )
{
    if (!project)
        return KConfigGroup();

    return project->projectConfiguration()->group( Config::groupName );
}

KConfigGroup buildDirGroup( KDevelop::IProject* project, int buildDirIndex )
{
    return baseGroup(project).group( Config::groupNameBuildDir.arg(buildDirIndex) );
}

bool buildDirGroupExists( KDevelop::IProject* project, int buildDirIndex )
{
    return baseGroup(project).hasGroup( Config::groupNameBuildDir.arg(buildDirIndex) );
}

QString readBuildDirParameter( KDevelop::IProject* project, const QString& key, const QString& aDefault, int buildDirectory )
{
    const int buildDirIndex = buildDirectory<0 ? CMake::currentBuildDirIndex(project) : buildDirectory;
    if (buildDirIndex >= 0)
        return buildDirGroup( project, buildDirIndex ).readEntry( key, aDefault );

    else
        return aDefault;
}

void writeBuildDirParameter( KDevelop::IProject* project, const QString& key, const QString& value )
{
    int buildDirIndex = CMake::currentBuildDirIndex(project);
    if (buildDirIndex >= 0)
    {
        KConfigGroup buildDirGrp = buildDirGroup( project, buildDirIndex );
        buildDirGrp.writeEntry( key, value );
    }

    else
    {
        qCWarning(CMAKE) << "cannot write key" << key << "(" << value << ")" << "when no builddir is set!";
    }
}

void writeProjectBaseParameter( KDevelop::IProject* project, const QString& key, const QString& value )
{
    KConfigGroup baseGrp = baseGroup(project);
    baseGrp.writeEntry( key, value );
}

void setBuildDirRuntime( KDevelop::IProject* project, const QString& name)
{
    writeBuildDirParameter(project, Config::Specific::buildDirRuntime, name);
}

QString buildDirRuntime( KDevelop::IProject* project, int builddir)
{
    return readBuildDirParameter(project, Config::Specific::buildDirRuntime, QString(), builddir);
}

} // namespace

namespace CMake
{

KDevelop::Path::List resolveSystemDirs(KDevelop::IProject* project, const QStringList& dirs)
{
    const KDevelop::Path buildDir(CMake::currentBuildDir(project));
    const KDevelop::Path installDir(CMake::currentInstallDir(project));

    KDevelop::Path::List newList;
    newList.reserve(dirs.size());
    foreach(const QString& s, dirs)
    {
        KDevelop::Path dir;
        if(s.startsWith(QLatin1String("#[bin_dir]")))
        {
            dir = KDevelop::Path(buildDir, s);
        }
        else if(s.startsWith(QLatin1String("#[install_dir]")))
        {
            dir = KDevelop::Path(installDir, s);
        }
        else
        {
            dir = KDevelop::Path(s);
        }

//         qCDebug(CMAKE) << "resolved" << s << "to" << d;

        if (!newList.contains(dir))
        {
            newList.append(dir);
        }
    }
    return newList;
}

///NOTE: when you change this, update @c defaultConfigure in cmakemanagertest.cpp
bool checkForNeedingConfigure( KDevelop::IProject* project )
{
    const KDevelop::Path builddir = currentBuildDir(project);
    const bool isValid = builddir.isValid();

    if( !isValid )
    {
        CMakeBuildDirChooser bd;

        bd.setProject( project );
        const auto builddirs = CMake::allBuildDirs(project);
        bd.setAlreadyUsed( builddirs );
        bd.setShowAvailableBuildDirs(!builddirs.isEmpty());
        bd.setCMakeExecutable(currentCMakeExecutable(project));

        if( !bd.exec() )
        {
            return false;
        }

        if (bd.reuseBuilddir())
        {
            CMake::setCurrentBuildDirIndex( project, bd.alreadyUsedIndex() );
        }
        else
        {

            QString newbuilddir = bd.buildFolder().toLocalFile();
            int addedBuildDirIndex = buildDirCount( project ); // old count is the new index

            // Initialize the kconfig items with the values from the dialog, this ensures the settings
            // end up in the config file once the changes are saved
            qCDebug(CMAKE) << "adding to cmake config: new builddir index" << addedBuildDirIndex;
            qCDebug(CMAKE) << "adding to cmake config: builddir path " << bd.buildFolder();
            qCDebug(CMAKE) << "adding to cmake config: installdir " << bd.installPrefix();
            qCDebug(CMAKE) << "adding to cmake config: extra args" << bd.extraArguments();
            qCDebug(CMAKE) << "adding to cmake config: build type " << bd.buildType();
            qCDebug(CMAKE) << "adding to cmake config: cmake executable " << bd.cmakeExecutable();
            qCDebug(CMAKE) << "adding to cmake config: environment <null>";
            CMake::setBuildDirCount( project, addedBuildDirIndex + 1 );
            CMake::setCurrentBuildDirIndex( project, addedBuildDirIndex );
            CMake::setCurrentBuildDir( project, bd.buildFolder() );
            CMake::setCurrentInstallDir( project, bd.installPrefix() );
            CMake::setCurrentExtraArguments( project, bd.extraArguments() );
            CMake::setCurrentBuildType( project, bd.buildType() );
            CMake::setCurrentCMakeExecutable(project, bd.cmakeExecutable());
            CMake::setCurrentEnvironment( project, QString() );
        }

        return true;
    } else if( !QFile::exists( KDevelop::Path(builddir, QStringLiteral("CMakeCache.txt")).toLocalFile() ) ||
                //TODO: maybe we could use the builder for that?
               !(QFile::exists( KDevelop::Path(builddir, QStringLiteral("Makefile")).toLocalFile() ) ||
                    QFile::exists( KDevelop::Path(builddir, QStringLiteral("build.ninja")).toLocalFile() ) ) )
    {
        // User entered information already, but cmake hasn't actually been run yet.
        return true;
    }
    return false;
}

QHash<KDevelop::Path, QStringList> enumerateTargets(const KDevelop::Path& targetsFilePath, const QString& sourceDir, const KDevelop::Path &buildDir)
{
    const QString buildPath = buildDir.toLocalFile();
    QHash<KDevelop::Path, QStringList> targets;
    QFile targetsFile(targetsFilePath.toLocalFile());
    if (!targetsFile.open(QIODevice::ReadOnly)) {
        qCDebug(CMAKE) << "Couldn't find the Targets file in" << targetsFile.fileName();
    }

    QTextStream targetsFileStream(&targetsFile);
    const QRegularExpression rx(QStringLiteral("^(.*)/CMakeFiles/(.*).dir$"));
    while (!targetsFileStream.atEnd()) {
        const QString line = targetsFileStream.readLine();
        auto match = rx.match(line);
        if (!match.isValid())
            qCDebug(CMAKE) << "invalid match for" << line;
        const QString sourcePath = match.captured(1).replace(buildPath, sourceDir);
        targets[KDevelop::Path(sourcePath)].append(match.captured(2));
    }
    return targets;
}

KDevelop::Path projectRoot(KDevelop::IProject* project)
{
    if (!project) {
        return {};
    }

    return project->path().cd(CMake::projectRootRelative(project));
}

KDevelop::Path currentBuildDir( KDevelop::IProject* project, int builddir )
{
    return KDevelop::Path(readBuildDirParameter( project, Config::Specific::buildDirPathKey, QString(), builddir ));
}

KDevelop::Path commandsFile(KDevelop::IProject* project)
{
    auto currentBuildDir = CMake::currentBuildDir(project);
    if (currentBuildDir.isEmpty()) {
        return {};
    }

    return KDevelop::Path(currentBuildDir, QStringLiteral("compile_commands.json"));
}

KDevelop::Path targetDirectoriesFile(KDevelop::IProject* project)
{
    auto currentBuildDir = CMake::currentBuildDir(project);
    if (currentBuildDir.isEmpty()) {
        return {};
    }

    return KDevelop::Path(currentBuildDir, QStringLiteral("CMakeFiles/TargetDirectories.txt"));
}

QString currentBuildType( KDevelop::IProject* project, int builddir )
{
    return readBuildDirParameter( project, Config::Specific::cmakeBuildTypeKey, QStringLiteral("Release"), builddir );
}

QString findExecutable()
{
    auto cmake = QStandardPaths::findExecutable(QStringLiteral("cmake"));
#ifdef Q_OS_WIN
    if (cmake.isEmpty())
        cmake = QStandardPaths::findExecutable("cmake",{
            "C:\\Program Files (x86)\\CMake\\bin",
            "C:\\Program Files\\CMake\\bin",
            "C:\\Program Files (x86)\\CMake 2.8\\bin",
            "C:\\Program Files\\CMake 2.8\\bin"});
#endif
    return cmake;
}

KDevelop::Path currentCMakeExecutable(KDevelop::IProject* project, int builddir)
{
    const auto systemExecutable = findExecutable();
    auto path = readBuildDirParameter(project, Config::Specific::cmakeExecutableKey, systemExecutable, builddir);
    if (path != systemExecutable) {
        QFileInfo info(path);
        if (!info.isExecutable()) {
            path = systemExecutable;
        }
    }
    return KDevelop::Path(path);
}

KDevelop::Path currentInstallDir( KDevelop::IProject* project, int builddir )
{
    return KDevelop::Path(readBuildDirParameter( project, Config::Specific::cmakeInstallDirKey, QStringLiteral("/usr/local"), builddir ));
}

QString projectRootRelative( KDevelop::IProject* project )
{
    return baseGroup(project).readEntry( Config::Old::projectRootRelativeKey, "." );
}

bool hasProjectRootRelative(KDevelop::IProject* project)
{
    return baseGroup(project).hasKey( Config::Old::projectRootRelativeKey );
}

QString currentExtraArguments( KDevelop::IProject* project, int builddir )
{
    return readBuildDirParameter( project, Config::Specific::cmakeArgumentsKey, QString(), builddir );
}

void setCurrentInstallDir( KDevelop::IProject* project, const KDevelop::Path& path )
{
    writeBuildDirParameter( project, Config::Specific::cmakeInstallDirKey, path.toLocalFile() );
}

void setCurrentBuildType( KDevelop::IProject* project, const QString& type )
{
    writeBuildDirParameter( project, Config::Specific::cmakeBuildTypeKey, type );
}

void setCurrentCMakeExecutable(KDevelop::IProject* project, const KDevelop::Path& path)
{
    writeBuildDirParameter(project, Config::Specific::cmakeExecutableKey, path.toLocalFile());
}

void setCurrentBuildDir( KDevelop::IProject* project, const KDevelop::Path& path )
{
    writeBuildDirParameter( project, Config::Specific::buildDirPathKey, path.toLocalFile() );
}

void setProjectRootRelative( KDevelop::IProject* project, const QString& relative)
{
    writeProjectBaseParameter( project, Config::Old::projectRootRelativeKey, relative );
}

void setCurrentExtraArguments( KDevelop::IProject* project, const QString& string)
{
    writeBuildDirParameter( project, Config::Specific::cmakeArgumentsKey, string );
}

QString currentEnvironment(KDevelop::IProject* project, int builddir)
{
    return readBuildDirParameter( project, Config::Specific::cmakeEnvironmentKey, QString(), builddir );
}

int currentBuildDirIndex( KDevelop::IProject* project )
{
    KConfigGroup baseGrp = baseGroup(project);

    if ( baseGrp.hasKey( Config::buildDirOverrideIndexKey ) )
        return baseGrp.readEntry<int>( Config::buildDirOverrideIndexKey, 0 );

    else
        return baseGrp.readEntry<int>( Config::buildDirIndexKey, 0 ); // default is 0 because QString::number(0) apparently returns an empty string
}

void setCurrentBuildDirIndex( KDevelop::IProject* project, int buildDirIndex )
{
    writeProjectBaseParameter( project, Config::buildDirIndexKey, QString::number (buildDirIndex) );
}

void setCurrentEnvironment( KDevelop::IProject* project, const QString& environment )
{
    writeBuildDirParameter( project, Config::Specific::cmakeEnvironmentKey, environment );
}

void initBuildDirConfig( KDevelop::IProject* project )
{
    int buildDirIndex = currentBuildDirIndex( project );
    if (buildDirCount(project) <= buildDirIndex )
        setBuildDirCount( project, buildDirIndex + 1 );
}

int buildDirCount( KDevelop::IProject* project )
{
    return baseGroup(project).readEntry<int>( Config::buildDirCountKey, 0 );
}

void setBuildDirCount( KDevelop::IProject* project, int count )
{
    writeProjectBaseParameter( project, Config::buildDirCountKey, QString::number(count) );
}

void removeBuildDirConfig( KDevelop::IProject* project )
{
    int buildDirIndex = currentBuildDirIndex( project );
    if ( !buildDirGroupExists( project, buildDirIndex ) )
    {
        qCWarning(CMAKE) << "build directory config" << buildDirIndex << "to be removed but does not exist";
        return;
    }

    int bdCount = buildDirCount(project);
    setBuildDirCount( project, bdCount - 1 );
    removeOverrideBuildDirIndex( project );
    setCurrentBuildDirIndex( project, -1 );

    // move (rename) the upper config groups to keep the numbering
    // if there's nothing to move, just delete the group physically
    if (buildDirIndex + 1 == bdCount)
        buildDirGroup( project, buildDirIndex ).deleteGroup();

    else for (int i = buildDirIndex + 1; i < bdCount; ++i)
    {
        KConfigGroup src = buildDirGroup( project, i );
        KConfigGroup dest = buildDirGroup( project, i - 1 );
        dest.deleteGroup();
        src.copyTo(&dest);
        src.deleteGroup();
    }
}

QHash<QString, QString> readCacheValues(const KDevelop::Path& cmakeCachePath, QSet<QString> variables)
{
    QHash<QString, QString> ret;
    QFile file(cmakeCachePath.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(CMAKE) << "couldn't open CMakeCache.txt" << cmakeCachePath;
        return ret;
    }

    QTextStream in(&file);
    while (!in.atEnd() && !variables.isEmpty())
    {
        QString line = in.readLine().trimmed();
        if(!line.isEmpty() && line[0].isLetter())
        {
            CacheLine c;
            c.readLine(line);

            if(!c.isCorrect())
                continue;

            if (variables.remove(c.name())) {
                ret[c.name()] = c.value();
            }
        }
    }
    return ret;
}

void updateConfig( KDevelop::IProject* project, int buildDirIndex)
{
    if (buildDirIndex < 0)
        return;

    KConfigGroup buildDirGrp = buildDirGroup( project, buildDirIndex );
    const KDevelop::Path builddir(buildDirGrp.readEntry( Config::Specific::buildDirPathKey, QString() ));
    const KDevelop::Path cacheFilePath( builddir, QStringLiteral("CMakeCache.txt"));

    const QMap<QString, QString> keys = {
        { QStringLiteral("CMAKE_COMMAND"), Config::Specific::cmakeExecutableKey },
        { QStringLiteral("CMAKE_INSTALL_PREFIX"), Config::Specific::cmakeInstallDirKey },
        { QStringLiteral("CMAKE_BUILD_TYPE"), Config::Specific::cmakeBuildTypeKey }
    };

    const QHash<QString, QString> cacheValues = readCacheValues(cacheFilePath, keys.keys().toSet());
    for(auto it = cacheValues.constBegin(), itEnd = cacheValues.constEnd(); it!=itEnd; ++it) {
        const QString key = keys.value(it.key());
        Q_ASSERT(!key.isEmpty());

        // Use cache only when the config value is not set. Without this check we will always
        // overwrite values provided by the user in config dialog.
        if (buildDirGrp.readEntry(key).isEmpty() && !it.value().isEmpty())
        {
            buildDirGrp.writeEntry( key, it.value() );
        }
    }
}

void attemptMigrate( KDevelop::IProject* project )
{
    if ( !baseGroup(project).hasKey( Config::Old::projectBuildDirs ) )
    {
        qCDebug(CMAKE) << "CMake settings migration: already done, exiting";
        return;
    }

    KConfigGroup baseGrp = baseGroup(project);

    KDevelop::Path buildDir( baseGrp.readEntry( Config::Old::currentBuildDirKey, QString() ) );
    int buildDirIndex = -1;
    const QStringList existingBuildDirs = baseGrp.readEntry( Config::Old::projectBuildDirs, QStringList() );
    {
        // also, find current build directory in this list (we need an index, not path)
        QString currentBuildDirCanonicalPath = QDir( buildDir.toLocalFile() ).canonicalPath();

        for( int i = 0; i < existingBuildDirs.count(); ++i )
        {
            const QString& nextBuildDir = existingBuildDirs.at(i);
            if( QDir(nextBuildDir).canonicalPath() == currentBuildDirCanonicalPath )
            {
                buildDirIndex = i;
            }
        }
    }
    int buildDirsCount = existingBuildDirs.count();

    qCDebug(CMAKE) << "CMake settings migration: existing build directories" << existingBuildDirs;
    qCDebug(CMAKE) << "CMake settings migration: build directory count" << buildDirsCount;
    qCDebug(CMAKE) << "CMake settings migration: current build directory" << buildDir << "(index" << buildDirIndex << ")";

    baseGrp.writeEntry( Config::buildDirCountKey, buildDirsCount );
    baseGrp.writeEntry( Config::buildDirIndexKey, buildDirIndex );

    for (int i = 0; i < buildDirsCount; ++i)
    {
        qCDebug(CMAKE) << "CMake settings migration: writing group" << i << ": path" << existingBuildDirs.at(i);

        KConfigGroup buildDirGrp = buildDirGroup( project, i );
        buildDirGrp.writeEntry( Config::Specific::buildDirPathKey, existingBuildDirs.at(i) );
    }

    baseGrp.deleteEntry( Config::Old::currentBuildDirKey );
    baseGrp.deleteEntry( Config::Old::currentCMakeBinaryKey );
    baseGrp.deleteEntry( Config::Old::currentBuildTypeKey );
    baseGrp.deleteEntry( Config::Old::currentInstallDirKey );
    baseGrp.deleteEntry( Config::Old::currentEnvironmentKey );
    baseGrp.deleteEntry( Config::Old::currentExtraArgumentsKey );
    baseGrp.deleteEntry( Config::Old::projectBuildDirs );
}

void setOverrideBuildDirIndex( KDevelop::IProject* project, int overrideBuildDirIndex )
{
    writeProjectBaseParameter( project, Config::buildDirOverrideIndexKey, QString::number(overrideBuildDirIndex) );
}

void removeOverrideBuildDirIndex( KDevelop::IProject* project, bool writeToMainIndex )
{
    KConfigGroup baseGrp = baseGroup(project);

    if( !baseGrp.hasKey(Config::buildDirOverrideIndexKey) )
        return;
    if( writeToMainIndex )
        baseGrp.writeEntry( Config::buildDirIndexKey, baseGrp.readEntry(Config::buildDirOverrideIndexKey) );

    baseGrp.deleteEntry(Config::buildDirOverrideIndexKey);
}

ICMakeDocumentation* cmakeDocumentation()
{
    return KDevelop::ICore::self()->pluginController()->extensionForPlugin<ICMakeDocumentation>(QStringLiteral("org.kdevelop.ICMakeDocumentation"));
}

QStringList allBuildDirs(KDevelop::IProject* project)
{
    QStringList result;
    int bdCount = buildDirCount(project);
    for (int i = 0; i < bdCount; ++i)
        result += buildDirGroup( project, i ).readEntry( Config::Specific::buildDirPathKey );
    return result;
}

QString executeProcess(const QString& execName, const QStringList& args)
{
    Q_ASSERT(!execName.isEmpty());
    qCDebug(CMAKE) << "Executing:" << execName << "::" << args;

    QProcess p;
    QTemporaryDir tmp(QStringLiteral("kdevcmakemanager"));
    p.setWorkingDirectory( tmp.path() );
    p.start(execName, args, QIODevice::ReadOnly);

    if(!p.waitForFinished())
    {
        qCDebug(CMAKE) << "failed to execute:" << execName;
    }

    QByteArray b = p.readAllStandardOutput();
    QString t;
    t.prepend(b.trimmed());
    return t;
}

QStringList supportedGenerators()
{
    QStringList generatorNames;

    bool hasNinja = ICore::self() && ICore::self()->pluginController()->pluginForExtension(QStringLiteral("org.kdevelop.IProjectBuilder"), QStringLiteral("KDevNinjaBuilder"));
    if (hasNinja)
        generatorNames << QStringLiteral("Ninja");

#ifdef Q_OS_WIN
    // Visual Studio solution is the standard generator under windows, but we don't want to use
    // the VS IDE, so we need nmake makefiles
    generatorNames << "NMake Makefiles";
#endif
    generatorNames << QStringLiteral("Unix Makefiles");

    return generatorNames;
}

QString defaultGenerator()
{
    const QStringList generatorNames = supportedGenerators();

    QString defGen = generatorNames.value(CMakeBuilderSettings::self()->generator());
    if (defGen.isEmpty())
    {
        qCWarning(CMAKE) << "Couldn't find builder with index " << CMakeBuilderSettings::self()->generator()
                   << ", defaulting to 0";
        CMakeBuilderSettings::self()->setGenerator(0);
        defGen = generatorNames.at(0);
    }
    return defGen;
}

}

