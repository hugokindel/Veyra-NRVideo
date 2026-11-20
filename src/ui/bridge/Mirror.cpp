// Mirror output window controls. The window belongs to the GUI thread; the
// engine presents the final frame into it (engine/MirrorOutput.h).
#include "../QmlPlayerBridge_p.h"
namespace veyra::ui {
namespace {
std::optional<MirrorWindow::Placement> parsePlacement(const QString& text){
    const auto parts=text.split(QLatin1Char(','));
    if(parts.size()!=5)return std::nullopt;
    bool ok=true;long v[4]{};
    for(int i=0;i<4&&ok;++i)v[i]=parts[i].toLong(&ok);
    if(!ok||v[2]<=v[0]||v[3]<=v[1])return std::nullopt;
    MirrorWindow::Placement p;p.windowed={v[0],v[1],v[2],v[3]};p.fullscreen=parts[4]==QLatin1String("1");
    return p;
}
QString formatPlacement(const MirrorWindow::Placement& p){
    return QStringLiteral("%1,%2,%3,%4,%5").arg(p.windowed.left).arg(p.windowed.top)
        .arg(p.windowed.right).arg(p.windowed.bottom).arg(p.fullscreen?1:0);
}
}
bool QmlPlayerBridge::mirrorWindowOpen()const{return impl_->mirrorWindow&&impl_->mirrorWindow->visible();}
QVariantMap QmlPlayerBridge::mirrorWindowState()const{return impl_->mirrorWindowState;}
void QmlPlayerBridge::setMirrorWindowOpen(bool open){
    auto& i=*impl_;
    if(open){
        if(!i.mirrorWindow){
            i.mirrorWindow=std::make_unique<MirrorWindow>();
            // Queued: the window procedure that reports the close must return first.
            i.mirrorWindow->onUserClosed=[this]{QMetaObject::invokeMethod(this,[this]{setMirrorWindowOpen(false);},Qt::QueuedConnection);};
            i.mirrorWindow->onFullscreenChanged=[this]{QMetaObject::invokeMethod(this,[this]{pollMirrorWindow();},Qt::QueuedConnection);};
        }
        HWND owner=i.videoWindow?GetAncestor(i.videoWindow,GA_ROOT):nullptr;
        if(!owner)if(auto* window=QGuiApplication::focusWindow())owner=reinterpret_cast<HWND>(window->winId());
        const bool wasOpen=mirrorWindowOpen();
        if(!i.mirrorWindow->show(owner,parsePlacement(i.prefString("mirrorWindowPlacement")))){
            emit notice(tr("镜像窗口无法创建，请查看日志"),true);pollMirrorWindow();return;
        }
        i.engine.setMirrorWindow(i.mirrorWindow->hwnd());
        if(!wasOpen)emit notice(tr("镜像窗口已打开：拖到另一块屏幕，双击或按 F11 全屏"),false);
    }else{
        // The engine lets go of the swap chain on its next frame; the window is only hidden.
        i.engine.setMirrorWindow(nullptr);
        if(i.mirrorWindow&&i.mirrorWindow->hwnd()){
            i.prefs.insert(QStringLiteral("mirrorWindowPlacement"),formatPlacement(i.mirrorWindow->placement()));
            if(!i.savePrefs())veyra::log::warn("ui-prefs","mirror window placement not saved");
            i.mirrorWindow->hide();
        }
    }
    pollMirrorWindow();
}
void QmlPlayerBridge::toggleMirrorFullscreen(){
    if(impl_->mirrorWindow&&impl_->mirrorWindow->visible())impl_->mirrorWindow->toggleFullscreen();
    pollMirrorWindow();
}
void QmlPlayerBridge::pollMirrorWindow(){
    auto& i=*impl_;
    const auto s=i.engine.mirrorStatus();
    const bool open=mirrorWindowOpen();
    const char* state="off";
    switch(s.state){
    case engine::MirrorState::Waiting:state="waiting";break;
    case engine::MirrorState::Active:state="active";break;
    case engine::MirrorState::Unsupported:state="unsupported";break;
    case engine::MirrorState::Failed:state="failed";break;
    default:break;
    }
    // A window that is open but not yet reached by a presenting session waits.
    if(open&&s.state==engine::MirrorState::Off)state="waiting";
    QVariantMap next{{"open",open},{"state",QString::fromLatin1(state)},
        {"fullscreen",open&&i.mirrorWindow->fullscreen()},
        {"width",int(s.width)},{"height",int(s.height)},
        {"presented",QVariant::fromValue(qulonglong(s.presented))},{"skipped",QVariant::fromValue(qulonglong(s.skipped))}};
    // Counters change every frame; publish them only with a state change or once a second.
    const bool changed=next.value("open")!=i.mirrorWindowState.value("open")||next.value("state")!=i.mirrorWindowState.value("state")||
        next.value("fullscreen")!=i.mirrorWindowState.value("fullscreen")||next.value("width")!=i.mirrorWindowState.value("width")||
        next.value("height")!=i.mirrorWindowState.value("height");
    static QElapsedTimer counters;
    if(!changed&&counters.isValid()&&counters.elapsed()<1000)return;
    counters.start();
    if(next==i.mirrorWindowState)return;
    const auto previous=i.mirrorWindowState.value("state").toString();
    i.mirrorWindowState=next;emit mirrorWindowChanged();
    if(open&&previous!=next.value("state").toString()){
        if(s.state==engine::MirrorState::Unsupported)emit notice(tr("XeSS 补帧自行管理呈现，镜像窗口暂不可用；换用其他补帧或关闭补帧后显示"),true);
        else if(s.state==engine::MirrorState::Failed)emit notice(tr("镜像窗口画面创建失败，正在重试"),true);
    }
}
void QmlPlayerBridge::releaseMirrorWindow(){
    auto& i=*impl_;
    if(!i.mirrorWindow)return;
    i.engine.setMirrorWindow(nullptr);
    if(i.mirrorWindow->hwnd()){
        i.prefs.insert(QStringLiteral("mirrorWindowPlacement"),formatPlacement(i.mirrorWindow->placement()));
        i.savePrefs();
    }
    // DXGI needs the window to outlive the swap chain. Wait (bounded) for the
    // engine to drop it, servicing messages sent to this thread meanwhile.
    QElapsedTimer waited;waited.start();
    while(!i.engine.mirrorReleased()&&waited.elapsed()<1000){
        MSG msg{};PeekMessageW(&msg,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);
        Sleep(5);
    }
    if(i.engine.mirrorReleased())i.mirrorWindow->destroy();
    else{veyra::log::warn("mirror-ui","engine still holds the mirror swap chain at exit; window left to process teardown");i.mirrorWindow->abandon();}
    i.mirrorWindow.reset();
}
}
