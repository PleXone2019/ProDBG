#include "MainWindow.h"
#include "CodeView/CodeView.h"
#include "RecentExecutables.h"

#include "AmigaUAE/AmigaUAE.h"
#include "Backend/BackendRequests.h"
#include "Backend/BackendSession.h"
#include "BreakpointModel.h"
#include "CodeViews.h"
#include "Config/AmigaUAEConfig.h"
#include "MemoryView/MemoryView.h"
#include "RegisterView/RegisterView.h"

#include <QDebug>
#include <QFileDialog>
#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include <QtCore/QSettings>
#include <QtWidgets/QDockWidget>

namespace prodbg {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MainWindow::MainWindow()
    : m_memoryView(new MemoryView(this))
    , m_registerView(new RegisterView(this))
    , m_statusbar(new QStatusBar(this))
    , m_backend(nullptr)
//, m_currentSession(nullptr)
//, m_amigaUae(nullptr)
{
    qRegisterMetaType<uint16_t>("uint16_t");
    qRegisterMetaType<uint32_t>("uint32_t");
    qRegisterMetaType<uint64_t>("uint64_t");
    qRegisterMetaType<IBackendRequests::ProgramCounterChange>("IBackendRequests::ProgramCounterChange");

    m_recentExecutables = new RecentExecutables;
    m_amigaUae = new AmigaUAE(this);

    m_ui.setupUi(this);

    m_breakpoints = new BreakpointModel;

    m_codeViews = new CodeViews(m_breakpoints, this);
    // m_codeViews->openFile(QStringLiteral("src/prodbg/main.cpp"), true);
    // m_codeViews->openFile(QStringLiteral("src/prodbg/main.cpp"), m_breakpoints);
    // m_codeViews->openFile(QStringLiteral("src/prodbg/main.cpp"), m_breakpoints);

    setCentralWidget(m_codeViews);

    setWindowTitle(QStringLiteral("ProDBG"));

    // Setup docking for MemoryView

    {
        QDockWidget* dock = new QDockWidget(QStringLiteral("MemoryView"), this);
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        dock->setObjectName(QStringLiteral("MemoryViewDock"));
        dock->setWidget(m_memoryView);
        addDockWidget(Qt::RightDockWidgetArea, dock);
    }

    // Setup docking for RegisterView

    {
        QDockWidget* dock = new QDockWidget(QStringLiteral("RegisterView"), this);
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        dock->setObjectName(QStringLiteral("RegisterViewDock"));
        dock->setWidget(m_registerView);
        addDockWidget(Qt::BottomDockWidgetArea, dock);
    }

    m_statusbar->showMessage(tr("Ready."));

    setStatusBar(m_statusbar);

    startDummyBackend();

    initActions();
    readSettings();

    initRecentFileActions();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MainWindow::~MainWindow()
{
    delete m_recentExecutables;

    closeCurrentBackend();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::initActions()
{
    connect(m_ui.actionStart, &QAction::triggered, this, &MainWindow::startDebug);
    connect(m_ui.actionReloadCurrentFile, &QAction::triggered, this, &MainWindow::reloadCurrentFile);
    connect(m_ui.actionStep_In, &QAction::triggered, this, &MainWindow::stepIn);
    connect(m_ui.actionAmiga_UAE, &QAction::triggered, this, &MainWindow::amigaUAEConfig);
    connect(m_ui.actionDebugAmigaExe, &QAction::triggered, this, &MainWindow::debugAmigaExe);
    connect(m_ui.actionToggleBreakpoint, &QAction::triggered, this, &MainWindow::toggleBreakpoint);
    connect(m_ui.actionOpen, &QAction::triggered, this, &MainWindow::openSourceFile);
    connect(m_ui.actionBreak, &QAction::triggered, this, &MainWindow::breakDebug);
    connect(m_ui.actionStop, &QAction::triggered, this, &MainWindow::stop);
    connect(m_ui.actionToggleSourceAsm, &QAction::triggered, m_codeViews, &CodeViews::toggleSourceAsm);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static QAction* findMenu(QMenu* menu, const QString& menuName)
{
    QList<QAction*> menus = menu->actions(); 

    for (auto& menu : menus) {
        if (menu->text() == menuName) {
            return menu;
        }
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::initRecentFileActions()
{
    QMenu* menu = m_ui.menuRecentExecutables;

    //QAction* recentFiles = m_ui.menuFile->actionRecentExecutables;

    for (int i = 0; i < RecentExecutables::MaxFiles_Count; ++i) {
        QAction* recentFileAction = new QAction(this);
        recentFileAction->setVisible(false);
        connect(recentFileAction, &QAction::triggered, this, &MainWindow::openRecentExe);
        m_recentFileActions.append(recentFileAction);
        menu->addAction(recentFileAction); 
    }

    m_recentExecutables->updateActionList(m_recentFileActions);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::openSourceFile()
{
    QString path = QFileDialog::getOpenFileName(nullptr, QStringLiteral("Open Source File"));

    if (path.isEmpty()) {
        return;
    }

    m_codeViews->openFile(path, true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::start()
{
    printf("MainWindow::start\n");

    const QVector<BreakpointModel::FileLineBreakpoint>& fileLineBreakpoints = m_breakpoints->getFileLineBreakpoints();
    const QVector<uint64_t>& addressBreakpoints = m_breakpoints->getAddressBreakpoints();

    // add the breakpoints before we start

    for (auto& bp : fileLineBreakpoints) {
        m_backendRequests->beginAddFileLineBreakpoint(bp.filename, bp.line);
    }

    for (auto& bp : addressBreakpoints) {
        m_backendRequests->beginAddAddressBreakpoint(bp);
    }

    startBackend();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::breakDebug()
{
    breakBackend();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::amigaUAEConfig()
{
    AmigaUAEConfig cfg(this);
    cfg.setModal(true);
    cfg.exec();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::uaeStarted()
{
    startAmigaUAEBackend();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::debugAmigaExe()
{
    if (!m_amigaUae->openFile()) {
        return;
    }

    m_lastAmigaExe = m_amigaUae->m_localExeToRun;

    m_recentExecutables->setFile(m_recentFileActions, m_lastAmigaExe, BackendType_AmigaUAE); 

    internalStartAmigaExe();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::openRecentExe()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action) {
        QString filename = action->data().toString();
        m_lastAmigaExe = filename;
        m_amigaUae->m_localExeToRun = filename;

        m_recentExecutables->putFileOnTop(m_recentFileActions, filename);

        internalStartAmigaExe();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::startDebug()
{
    if (m_currentBackend == Amiga && !m_lastAmigaExe.isEmpty()) {
        internalStartAmigaExe();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::reloadCurrentFile()
{
    m_codeViews->reloadCurrentFile();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::internalStartAmigaExe()
{
    m_amigaUae->m_localExeToRun = m_lastAmigaExe;

    if (!m_amigaUae->validateSettings()) {
        return;
    }

    m_lastAmigaExe = m_amigaUae->m_localExeToRun;

    if (!m_amigaUae->m_skipUAELaunch) {
        connect(m_amigaUae->m_uaeProcess, &QProcess::started, this, &MainWindow::uaeStarted);
        m_amigaUae->launchUAE();
    } else {
        startAmigaUAEBackend();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::setupBackendConnections()
{
    connect(this, &MainWindow::stepInBackend, m_backend, &BackendSession::stepIn);
    connect(this, &MainWindow::stepOverBackend, m_backend, &BackendSession::stepOver);
    connect(this, &MainWindow::startBackend, m_backend, &BackendSession::start);
    connect(this, &MainWindow::breakBackend, m_backend, &BackendSession::breakDebug);

    connect(m_backend, &BackendSession::statusUpdate, this, &MainWindow::statusUpdate);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::startDummyBackend()
{
    BackendSession* backend = BackendSession::createBackendSession(QStringLiteral("Dummy Backend"));

    if (!backend) {
        qDebug() << "Unable to create Dummy Backend";
        return;
    }

    setupBackend(backend);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::closeCurrentBackend()
{
    if (m_backendThread) {
        m_backendThread->quit();
        m_backendThread->wait();
    }

    delete m_backend;
    delete m_backendThread;
    delete m_backendRequests;

    // m_registerView->setBackendInterface(nullptr);
    // m_codeViews->setBackendInterface(nullptr);
    // m_memoryView->setBackendInterface(nullptr);

    m_backend = nullptr;
    m_backendThread = nullptr;
    m_backendRequests = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::startAmigaUAEBackend()
{
    closeCurrentBackend();

    BackendSession* backend = BackendSession::createBackendSession(QStringLiteral("Amiga UAE Debugger"));

    if (!backend) {
        printf("Unable to create Amiga UAE Debugger backend\n");
        return;
    }

    setupBackend(backend);

    m_backendRequests->sendCustomString(m_amigaUae->m_setFileId, m_amigaUae->m_fileToRun);
    m_backendRequests->sendCustomString(m_amigaUae->m_setHddPathId, m_amigaUae->m_dh0Path);

    // TODO: This is really temporary
    QTimer::singleShot(2000, this, &MainWindow::start);

    m_currentBackend = Amiga;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::setupBackend(BackendSession* backend)
{
    m_backend = backend;

    m_backendThread = new QThread;

    m_backend->moveToThread(m_backendThread);
    m_backendRequests = new BackendRequests(m_backend);

    m_registerView->setBackendInterface(m_backendRequests);
    m_codeViews->setBackendInterface(m_backendRequests);
    m_memoryView->setBackendInterface(m_backendRequests);

    m_backendThread->start();

    setupBackendConnections();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::stop()
{
    if (m_currentBackend == Amiga) {
        m_amigaUae->killProcess();
    }

    stopBackend();

    m_statusbar->showMessage(QStringLiteral("Ready."));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::stepIn()
{
    stepInBackend();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::stepOver()
{
    stepOverBackend();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::toggleBreakpoint()
{
    m_codeViews->toggleBreakpoint();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::statusUpdate(const QString& status)
{
    m_statusbar->showMessage(status);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::closeEvent(QCloseEvent* event)
{
    writeSettings();
    event->accept();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::writeSettings()
{
    QSettings settings(QStringLiteral("TBL"), QStringLiteral("ProDBG"));

    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.setValue(QStringLiteral("size"), size());
    settings.setValue(QStringLiteral("pos"), pos());
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("windowState"), saveState());
    settings.endGroup();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::readSettings()
{
    QSettings settings(QStringLiteral("TBL"), QStringLiteral("ProDBG"));

    settings.beginGroup(QStringLiteral("MainWindow"));
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("windowState")).toByteArray());
    resize(settings.value(QStringLiteral("size"), QSize(800, 600)).toSize());
    move(settings.value(QStringLiteral("pos"), QPoint(100, 100)).toPoint());
    settings.endGroup();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
