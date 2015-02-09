#include "mainwindow_p.hpp"
#include "app.hpp"
#include "misc/trayicon.hpp"
#include "dialog/mbox.hpp"
#include "quick/appobject.hpp"

//DECLARE_LOG_CONTEXT(Main)

#ifdef Q_OS_MAC
void qt_mac_set_dock_menu(QMenu *menu);
#include <Carbon/Carbon.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent, Qt::Window), d(new Data)
{
    d->p = this;
    d->view = new MainQuickView(this);
    d->desktop = cApp.desktop();
    d->pref.initialize();
    d->pref.load();
    d->undo.setActive(false);
    d->dontShowMsg = true;

    AppObject::setEngine(&d->e);
    AppObject::setHistory(&d->history);
    AppObject::setPlaylist(&d->playlist);
    AppObject::setDownloader(&d->downloader);
    AppObject::setTheme(&d->theme);
    AppObject::setWindow(this);

    d->playlist.setDownloader(&d->downloader);
    d->e.setHistory(&d->history);
    d->e.setYouTube(&d->youtube);
    d->e.setYle(&d->yle);
    d->e.run();

    d->initWindow();
    d->initDesktop();
    d->initContextMenu();
    d->initItems();
    d->initTray();

    d->plugEngine();
    d->plugMenu();

    d->restoreState();

    d->dontShowMsg = false;
    d->undo.setActive(true);
    QTimer::singleShot(1, this, [=] () { d->applyPref(); cApp.runCommands(); });
}

MainWindow::~MainWindow() {
    cApp.setMprisActivated(false);
    d->view->engine()->clearComponentCache();
    exit();
    delete d->view;
    delete d;
}

auto MainWindow::openFromFileManager(const Mrl &mrl) -> void
{
    if (mrl.isDir())
        d->openDir(mrl.toLocalFile());
    else {
        if (mrl.isLocalFile() && _IsSuffixOf(PlaylistExt, mrl.suffix()))
            d->playlist.open(mrl, QString());
        else {
            const auto mode = d->pref.open_media_from_file_manager();
            d->openWith(mode, QList<Mrl>() << mrl);
        }
    }
}

auto MainWindow::screen() const -> QScreen*
{
    return d->view->screen();
}

auto MainWindow::engine() const -> PlayEngine*
{
    return &d->e;
}

auto MainWindow::playlist() const -> PlaylistModel*
{
    return &d->playlist;
}

auto MainWindow::exit() -> void
{
    static bool done = false;
    if (!done) {
        cApp.setScreensaverDisabled(false);
        d->commitData();
        cApp.quit();
        done = true;
    }
}

auto MainWindow::play() -> void
{
    if (d->stateChanging)
        return;
    if (d->e.mrl().isImage())
        d->menu(u"play"_q)[u"next"_q]->trigger();
    else {
        const auto state = d->e.state();
        switch (state) {
        case PlayEngine::Playing:
            break;
        case PlayEngine::Paused:
            d->e.unpause();
            break;
        default:
            d->load(d->e.mrl());
            break;
        }
    }
}

auto MainWindow::togglePlayPause() -> void
{
    if (d->stateChanging)
        return;
    if (d->e.mrl().isImage())
        d->menu(u"play"_q)[u"next"_q]->trigger();
    else {
        const auto state = d->e.state();
        switch (state) {
        case PlayEngine::Playing:
            d->e.pause();
            break;
        default:
            play();
            break;
        }
    }
}

auto MainWindow::isSceneGraphInitialized() const -> bool
{
    return d->sgInit;
}

auto MainWindow::setFullScreen(bool full) -> void
{
    d->dontPause = true;
    if (full != d->fullScreen) {
        d->fullScreen = full;
        d->updateWindowPosState();
#ifdef Q_OS_MAC
        if (!d->pref.lion_style_fullscreen()) {
            static Qt::WindowFlags flags = windowFlags();
            static QRect geometry;
            if (full) {
                auto desktop = cApp.desktop();
                const int screen = desktop->screenNumber(this);
                if (screen >= 0) {
                    flags = windowFlags();
                    geometry = this->geometry();
                    setWindowFlags(flags | Qt::FramelessWindowHint);
                    SetSystemUIMode(kUIModeAllHidden, kUIOptionAutoShowMenuBar);
                    show();
                    setGeometry(QRect(QPoint(0, 0),
                                      desktop->screenGeometry(this).size()));
                }
            } else {
                setWindowFlags(flags);
                setGeometry(geometry);
                SetSystemUIMode(kUIModeNormal, 0);
            }
            d->checkWindowState(d->winState);
            d->updateTitle();
            d->updateStaysOnTop();
        } else
#endif
        {
            constexpr auto minfull = Qt::WindowMinimized | Qt::WindowFullScreen;
            setWindowState(full ? Qt::WindowFullScreen
                                : (d->prevWinState & ~minfull));
        }
        d->setCursorVisible(!d->fullScreen);
        emit fullscreenChanged(d->fullScreen);
    }
    d->dontPause = false;
}

auto MainWindow::isFullScreen() const -> bool
{
    return d->fullScreen || QWidget::isFullScreen();
}

auto MainWindow::resetMoving() -> void
{
    if (d->moving) {
        d->moving = false;
        d->winStartPos = d->mouseStartPos = QPoint();
    }
}

using MsBh = MouseBehavior;

auto MainWindow::onMouseMoveEvent(QMouseEvent *event) -> void
{
    QWidget::mouseMoveEvent(event);
    d->cancelToHideCursor();
    const bool full = isFullScreen();
    const auto gpos = event->globalPos();
    if (full)
        resetMoving();
    else if (d->moving)
            move(d->winStartPos + (gpos - d->mouseStartPos));
    d->readyToHideCursor();
    d->e.sendMouseMove(event->pos());
}

auto MainWindow::onMouseDoubleClickEvent(QMouseEvent *event) -> void
{
    QWidget::mouseDoubleClickEvent(event);
    if (event->buttons() & Qt::LeftButton) {
        const auto act = d->menu.action(d->actionId(MsBh::DoubleClick, event));
        if (!act)
            return;
#ifdef Q_OS_MAC
        if (action == d->menu(u"window"_q)[u"full"_q])
            QTimer::singleShot(300, action, SLOT(trigger()));
        else
#endif
        d->trigger(act);
    }
}

auto MainWindow::onMouseReleaseEvent(QMouseEvent *event) -> void
{
    QWidget::mouseReleaseEvent(event);
    const auto rect = geometry();
    if (d->pressedButton == event->button()
            && rect.contains(event->localPos().toPoint() + rect.topLeft())) {
        const auto mb = MouseBehaviorInfo::fromData(d->pressedButton);
        if (mb != MsBh::DoubleClick)
            d->trigger(d->menu.action(d->actionId(mb, event)));
    }
}

auto MainWindow::onMousePressEvent(QMouseEvent *event) -> void
{
    QWidget::mousePressEvent(event);
    d->pressedButton = Qt::NoButton;
    bool showContextMenu = false;
    switch (event->button()) {
    case Qt::LeftButton:
        if (isFullScreen())
            break;
        d->moving = true;
        d->mouseStartPos = event->globalPos();
        d->winStartPos = pos();
        break;
    case Qt::MiddleButton:    case Qt::ExtraButton1:
    case Qt::ExtraButton2:    case Qt::ExtraButton3:
    case Qt::ExtraButton4:    case Qt::ExtraButton5:
    case Qt::ExtraButton6:    case Qt::ExtraButton7:
    case Qt::ExtraButton8:    case Qt::ExtraButton9:
        d->pressedButton = event->button();
        break;
    case Qt::RightButton:
        showContextMenu = true;
        break;
    default:
        break;
    }
    if (showContextMenu)
        d->contextMenu.exec(QCursor::pos());
    else
        d->contextMenu.hide();
    d->e.sendMouseClick(event->pos());
}

auto MainWindow::onWheelEvent(QWheelEvent *ev) -> void
{
    QWidget::wheelEvent(ev);
    const auto delta = ev->delta();
    if (delta) {
        const bool up = d->pref.invert_wheel() ? delta < 0 : delta > 0;
        const auto id = d->actionId(up ? MsBh::ScrollUp : MsBh::ScrollDown, ev);
        d->trigger(d->menu.action(id));
        ev->accept();
    }
}

auto MainWindow::dragEnterEvent(QDragEnterEvent *event) -> void
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

auto MainWindow::dropEvent(QDropEvent *event) -> void
{
    const auto urls = event->mimeData()->urls();
    if (!event->mimeData()->hasUrls() || urls.isEmpty())
        return;
    Playlist playlist;
    QStringList subList;

    auto addPlaylist = [&] (const QUrl &url, const QString &suffix) -> bool {
        if (_IsSuffixOf(PlaylistExt, suffix)) {
            Playlist list;
            list.load(url);
            playlist += list;
        } else if (_IsSuffixOf(VideoExt, suffix)
                   || _IsSuffixOf(AudioExt, suffix))
            playlist.append(url);
        else
            return false;
        return true;
    };

    for (auto &url : urls) {
        if (url.isLocalFile()) {
            const QFileInfo fileInfo(url.toLocalFile());
            if (!fileInfo.exists())
                continue;
            auto path = fileInfo.absoluteFilePath();
            if (fileInfo.isFile()) {
                const auto suffix = fileInfo.suffix();
                if (!addPlaylist(url, suffix) && _IsSuffixOf(SubtitleExt, suffix))
                    subList << path;
            } else if (fileInfo.isDir()) {
                if (!fileInfo.fileName().compare("VIDEO_TS"_a, Qt::CaseInsensitive)
                        && !QDir(path).entryList(QStringList(u"*.ifo"_q), QDir::Files).isEmpty()) {
                    d->as.dvd_device = path;
                    d->openMrl(Mrl::fromDisc(u"dvdnav"_q, d->as.dvd_device, -1, true));
                } else
                    d->openDir(path);
                return;
            }
        } else
            addPlaylist(url, QFileInfo(url.path()).suffix().toLower());
    }
    if (!playlist.isEmpty()) {
        d->openWith(d->pref.open_media_by_drag_and_drop(), playlist);
    } else if (!subList.isEmpty())
        d->e.addSubtitleFiles(subList, d->pref.sub_enc());
}

auto MainWindow::resizeEvent(QResizeEvent *event) -> void
{
    QWidget::resizeEvent(event);
    if (!d->fullScreen)
        d->updateWindowSizeState();
}

auto MainWindow::onKeyPressEvent(QKeyEvent *event) -> void
{
    QWidget::keyPressEvent(event);
    d->trigger(RootMenu::instance().action(event));
    event->accept();
}

auto MainWindow::showEvent(QShowEvent *event) -> void
{
    QWidget::showEvent(event);
    d->doVisibleAction(true);
}

auto MainWindow::hideEvent(QHideEvent *event) -> void
{
    QWidget::hideEvent(event);
    d->doVisibleAction(false);
}

auto MainWindow::changeEvent(QEvent *ev) -> void
{
    QWidget::changeEvent(ev);
    if (ev->type() == QEvent::WindowStateChange) {
        auto event = static_cast<QWindowStateChangeEvent*>(ev);
        d->checkWindowState(event->oldState());
    }
}

auto MainWindow::closeEvent(QCloseEvent *event) -> void
{
    setFullScreen(false);
    QWidget::closeEvent(event);
#ifndef Q_OS_MAC
    if (d->tray && d->pref.enable_system_tray()
            && d->pref.hide_rather_close()) {
        hide();
        if (d->as.ask_system_tray) {
            MBox mbox(this);
            mbox.setIcon(MBox::Icon::Information);
            mbox.setTitle(tr("System Tray Icon"));
            mbox.setText(tr("bomi will be running in "
                "the system tray when the window closed."));
            mbox.setInformativeText(
                tr("You can change this behavior in the preferences. "
                    "If you want to exit bomi, please use 'Exit' menu."));
            mbox.addButton(BBox::Ok);
            mbox.checkBox()->setText(tr("Do not display this message again"));
            mbox.exec();
            d->as.ask_system_tray = !mbox.checkBox()->isChecked();
        }
        event->ignore();
    } else {
        event->accept();
        exit();
    }
#else
    event->accept();
#endif
}

auto MainWindow::moveEvent(QMoveEvent *event) -> void
{
    QWidget::moveEvent(event);
    if (!d->fullScreen)
        d->updateWindowPosState();
}
