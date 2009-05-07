/*
   Copyright 2009 Niko Sams <niko.sams@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "gdbtest.h"

#include <QtTest/QTest>
#include <QSignalSpy>
#include <QDebug>
#include <QApplication>
#include <QFileInfo>
#include <QDir>

#include <KGlobal>
#include <KSharedConfig>
#include <KDebug>

#include <shell/testcore.h>
#include <shell/shellextension.h>
#include <debugger/interfaces/stackmodel.h>
#include <debugger/breakpoint/breakpointmodel.h>
#include <interfaces/idebugcontroller.h>
#include <debugger/breakpoint/breakpoint.h>

#include "gdbcontroller.h"
#include "debugsession.h"
#include <debugger/interfaces/ibreakpointcontroller.h>
#include <gdbcommand.h>
#include <interfaces/ilaunchconfiguration.h>
#include <execute/executepluginconstants.h>

using namespace GDBDebugger;

class AutoTestShell : public KDevelop::ShellExtension
{
public:
    QString xmlFile() { return QString(); }
    QString defaultProfile() { return "kdevtest"; }
    KDevelop::AreaParams defaultArea() {
        KDevelop::AreaParams params;
        params.name = "test";
        params.title = "Test";
        return params;
    }
    QString projectFileExtension() { return QString(); }
    QString projectFileDescription() { return QString(); }
    QStringList defaultPlugins() { return QStringList(); }

    static void init() { s_instance = new AutoTestShell; }
};

void GdbTest::init()
{
    AutoTestShell::init();
    m_core = new KDevelop::TestCore();
    m_core->initialize(KDevelop::Core::NoUi);

    //remove all breakpoints - so we can set our own in the test
    KConfigGroup breakpoints = KGlobal::config()->group("breakpoints");
    breakpoints.writeEntry("number", 0);
    breakpoints.sync();
}

void GdbTest::cleanup()
{
    m_core->cleanup();
    delete m_core;
}

class TestLaunchConfiguration : public KDevelop::ILaunchConfiguration
{
public:
    TestLaunchConfiguration(KUrl executable = KUrl(QDir::currentPath()+"/unittests/debugee") ) {
        c = new KConfig();
        cfg = c->group("launch");
        cfg.writeEntry(ExecutePlugin::isExecutableEntry, true);
        cfg.writeEntry(ExecutePlugin::executableEntry, executable);
    }
    ~TestLaunchConfiguration() {
        delete c;
    }
    virtual const KConfigGroup config() const { return cfg; }
    virtual QString name() const { return QString("Test-Launch"); }
    virtual KDevelop::IProject* project() const { return 0; }
    virtual KDevelop::LaunchConfigurationType* type() const { return 0; }
private:
    KConfigGroup cfg;
    KConfig *c;
};

class TestDebugSession : public DebugSession
{
    Q_OBJECT
public:
    TestDebugSession() : DebugSession(new GDBController), m_line(0)
    {
        qRegisterMetaType<KUrl>("KUrl");
        connect(this, SIGNAL(showStepInSource(KUrl, int)), SLOT(slotShowStepInSource(KUrl, int)));
    }
    ~TestDebugSession()
    {
        delete controller();
    }
    KUrl url() { return m_url; }
    int line() { return m_line; }

private slots:
    void slotShowStepInSource(const KUrl &url, int line)
    {
        m_url = url;
        m_line = line;
    }
private:
    KUrl m_url;
    int m_line;

};

void GdbTest::testStdOut()
{
    TestDebugSession session;

    QSignalSpy outputSpy(&session, SIGNAL(applicationStandardOutputLines(QStringList)));

    TestLaunchConfiguration cfg;
    session.startProgram(&cfg, 0);
    waitForState(session, KDevelop::IDebugSession::StoppedState);

    {
        QCOMPARE(outputSpy.count(), 1);
        QList<QVariant> arguments = outputSpy.takeFirst();
        QCOMPARE(arguments.count(), 1);
        QCOMPARE(arguments.first().toStringList(), QStringList() <<"Hello, world!");
    }
}

void GdbTest::testBreakpoint()
{
    TestDebugSession session;

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel* breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();
    KDevelop::Breakpoint * b = breakpoints->addCodeBreakpoint(fileName, 28);
    QCOMPARE(session.breakpointController()->breakpointState(b), KDevelop::Breakpoint::DirtyState);

    session.startProgram(&cfg, 0);
    waitForState(session, DebugSession::PausedState);
    QCOMPARE(session.breakpointController()->breakpointState(b), KDevelop::Breakpoint::CleanState);
    session.stepInto();
    waitForState(session, DebugSession::PausedState);
    session.stepInto();
    waitForState(session, DebugSession::PausedState);
    session.run();
    waitForState(session, DebugSession::StoppedState);
}

void GdbTest::testDisableBreakpoint()
{
    TestDebugSession session;

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel *breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();
    KDevelop::Breakpoint *b;

    //add disabled breakpoint before startProgram
    b = breakpoints->addCodeBreakpoint(fileName, 29);
    b->setData(KDevelop::Breakpoint::EnableColumn, false);

    b = breakpoints->addCodeBreakpoint(fileName, 21);
    session.startProgram(&cfg, 0);
    waitForState(session, DebugSession::PausedState);

    //disable existing breakpoint
    b->setData(KDevelop::Breakpoint::EnableColumn, false);

    //add another disabled breakpoint
    b = breakpoints->addCodeBreakpoint(fileName, 31);
    QTest::qWait(300);
    b->setData(KDevelop::Breakpoint::EnableColumn, false);

    QTest::qWait(300);
    session.run();
    waitForState(session, DebugSession::StoppedState);

}

void GdbTest::testChangeLocationBreakpoint()
{
    TestDebugSession session;

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel *breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();

    KDevelop::Breakpoint *b = breakpoints->addCodeBreakpoint(fileName, 27);

    session.startProgram(&cfg, 0);
    waitForState(session, DebugSession::PausedState);
    QCOMPARE(session.line(), 27);

    QTest::qWait(100);
    b->setLine(28);
    QTest::qWait(100);
    session.run();

    QTest::qWait(100);
    waitForState(session, DebugSession::PausedState);
    QCOMPARE(session.line(), 28);
    QTest::qWait(100);
    breakpoints->setData(breakpoints->index(0, KDevelop::Breakpoint::LocationColumn), fileName+":30");
    QCOMPARE(b->line(), 29);
    QTest::qWait(100);
    session.run();
    QTest::qWait(100);
    waitForState(session, DebugSession::PausedState);
    QCOMPARE(session.line(), 29);
    session.run();

    waitForState(session, DebugSession::StoppedState);
}

void GdbTest::testDeleteBreakpoint()
{
    TestDebugSession session;

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel *breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();

    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 1); //one for the "insert here" entry
    //add breakpoint before startProgram
    KDevelop::Breakpoint *b = breakpoints->addCodeBreakpoint(fileName, 21);
    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 2);
    breakpoints->removeRow(0);
    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 1);

    b = breakpoints->addCodeBreakpoint(fileName, 22);

    session.startProgram(&cfg, 0);
    waitForState(session, DebugSession::PausedState);
    QTest::qWait(100);
    breakpoints->removeRow(0);
    QTest::qWait(100);
    session.run();

    waitForState(session, DebugSession::StoppedState);
}

void GdbTest::testPendingBreakpoint()
{
    TestDebugSession session;

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel* breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();
    breakpoints->addCodeBreakpoint(fileName, 28);

    KDevelop::Breakpoint * b = breakpoints->addCodeBreakpoint(QFileInfo(__FILE__).dir().path()+"/gdbtest.cpp", 10);
    QCOMPARE(session.breakpointController()->breakpointState(b), KDevelop::Breakpoint::DirtyState);

    session.startProgram(&cfg, 0);
    waitForState(session, DebugSession::PausedState);
    QCOMPARE(session.breakpointController()->breakpointState(b), KDevelop::Breakpoint::PendingState);
    session.run();
    waitForState(session, DebugSession::StoppedState);
}

void GdbTest::testUpdateBreakpoint()
{
    TestDebugSession session;

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel* breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();

    KDevelop::Breakpoint * b = breakpoints->addCodeBreakpoint(fileName, 28);
    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 2);

    session.startProgram(&cfg, 0);

    //insert custom command as user might do it using GDB console
    session.controller()->addCommand(new UserCommand(GDBMI::NonMI, "break "+fileName+":28"));

    waitForState(session, DebugSession::PausedState);
    QTest::qWait(100);
    session.stepInto();
    waitForState(session, DebugSession::PausedState);
    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 3);
    b = breakpoints->breakpoint(1);
    QCOMPARE(b->url(), KUrl(fileName));
    QCOMPARE(b->line(), 27);
    session.run();
    waitForState(session, DebugSession::PausedState);


    session.run();
    waitForState(session, DebugSession::StoppedState);
}

void GdbTest::testIgnoreHitsBreakpoint()
{
    TestDebugSession session;

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel* breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();

    KDevelop::Breakpoint * b = breakpoints->addCodeBreakpoint(fileName, 21);
    b->setIgnoreHits(1);

    b = breakpoints->addCodeBreakpoint(fileName, 22);

    session.startProgram(&cfg, 0);

    waitForState(session, DebugSession::PausedState);
    QTest::qWait(100);
    b->setIgnoreHits(1);
    session.run();
    waitForState(session, DebugSession::PausedState);
    session.run();
    waitForState(session, DebugSession::StoppedState);
}

void GdbTest::testConditionBreakpoint()
{
    TestDebugSession session;

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel* breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();

    KDevelop::Breakpoint * b = breakpoints->addCodeBreakpoint(fileName, 23);
    b->setCondition("i==2");

    b = breakpoints->addCodeBreakpoint(fileName, 24);

    session.startProgram(&cfg, 0);

    waitForState(session, DebugSession::PausedState);
    QTest::qWait(100);
    b->setCondition("i==0");
    QTest::qWait(100);
    session.run();
    waitForState(session, DebugSession::PausedState);
    session.run();
    waitForState(session, DebugSession::StoppedState);

}

void GdbTest::testShowStepInSource()
{
    TestDebugSession session;

    qRegisterMetaType<KUrl>("KUrl");
    QSignalSpy showStepInSourceSpy(&session, SIGNAL(showStepInSource(KUrl, int)));

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel* breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();
    breakpoints->addCodeBreakpoint(fileName, 29);
    session.startProgram(&cfg, 0);
    waitForState(session, DebugSession::PausedState);
    session.stepInto();
    waitForState(session, DebugSession::PausedState);
    session.stepInto();
    waitForState(session, DebugSession::PausedState);
    session.run();
    waitForState(session, DebugSession::StoppedState);

    {
        QCOMPARE(showStepInSourceSpy.count(), 3);
        QList<QVariant> arguments = showStepInSourceSpy.takeFirst();
        QCOMPARE(arguments.first().value<KUrl>(), KUrl::fromPath(fileName));
        QCOMPARE(arguments.at(1).toInt(), 29);

        arguments = showStepInSourceSpy.takeFirst();
        QCOMPARE(arguments.first().value<KUrl>(), KUrl::fromPath(fileName));
        QCOMPARE(arguments.at(1).toInt(), 22);

        arguments = showStepInSourceSpy.takeFirst();
        QCOMPARE(arguments.first().value<KUrl>(), KUrl::fromPath(fileName));
        QCOMPARE(arguments.at(1).toInt(), 23);
    }
}

void GdbTest::testStack()
{
    TestDebugSession session;

    TestLaunchConfiguration cfg;
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

    KDevelop::BreakpointModel* breakpoints = KDevelop::ICore::self()->debugController()
                                            ->breakpointModel();
    breakpoints->addCodeBreakpoint(fileName, 21);
    QVERIFY(session.startProgram(&cfg, 0));
    waitForState(session, DebugSession::PausedState);

    KDevelop::StackModel *model = session.stackModel();
    model->setAutoUpdate(true);
    QTest::qWait(200);

    QCOMPARE(model->rowCount(QModelIndex()), 1);
    QCOMPARE(model->columnCount(QModelIndex()), 1);

    QCOMPARE(model->data(model->index(0,0), Qt::DisplayRole).toString(), QString("#0 at foo"));

    QTest::qWait(200);
    KDevelop::FramesModel* fmodel=model->modelForThread(0);
    QCOMPARE(fmodel->rowCount(), 2);
    QCOMPARE(fmodel->columnCount(), 3);
    QCOMPARE(fmodel->framesCount(), 2);
    QCOMPARE(fmodel->data(fmodel->index(0,0), Qt::DisplayRole).toString(), QString("0"));
    QCOMPARE(fmodel->data(fmodel->index(0,1), Qt::DisplayRole).toString(), QString("foo"));
    QCOMPARE(fmodel->data(fmodel->index(0,2), Qt::DisplayRole).toString(), fileName+QString(":23"));
    QCOMPARE(fmodel->data(fmodel->index(1,0), Qt::DisplayRole).toString(), QString("1"));
    QCOMPARE(fmodel->data(fmodel->index(1,1), Qt::DisplayRole).toString(), QString("main"));
    QCOMPARE(fmodel->data(fmodel->index(1,2), Qt::DisplayRole).toString(), fileName+QString(":29"));


    session.run();
    waitForState(session, DebugSession::PausedState);
    session.run();
    waitForState(session, DebugSession::StoppedState);
}

void GdbTest::waitForState(const GDBDebugger::DebugSession &session, DebugSession::DebuggerState state)
{
    QTime stopWatch;
    stopWatch.start();
    while (session.state() != state) {
        if (stopWatch.elapsed() > 5000) qFatal("Didn't reach state");
        QTest::qWait(20);
        kDebug() << session.state() << state;
    }
}

QTEST_MAIN( GdbTest )

#include "gdbtest.moc"
#include "moc_gdbtest.cpp"
