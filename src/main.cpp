#include "core/App.h"
#include "core/Log.h"
#include "platform/Platform.h"
#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("AutoClicker");
    app.setOrganizationName("AutoClicker");
    app.setApplicationVersion("2.0.0");

    ac::App& core = ac::App::instance();
    if (!core.init()) return 0;   // 已有实例在跑时，init() 会把前台切过去然后返回 false

    ac::MainWindow win;
    win.show();
    win.raise();
    win.activateWindow();

    QObject::connect(&app, &QApplication::aboutToQuit, [&core] {
        core.stopAll(true);
        ac::pl::shutdown();
    });

    return app.exec();
}
