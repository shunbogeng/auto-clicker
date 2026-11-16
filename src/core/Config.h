// 配置：读写 JSON，键名沿用旧版，方便老用户把配置/脚本搬过来。
#pragma once

#include <QSize>
#include <QString>

namespace ac {

struct MouseCfg {
    int interval = 1000;
    QString hotkey = "f6";
    bool hotkeyEnabled = false;   // 第一次打开默认关闭，避免占键位
};

struct ImageCfg {
    QString tmpl;
    double confidence = 0.85;
    int interval = 1000;
    int offx = 0;
    int offy = 0;
    bool multi = false;
    bool limitEnabled = false;
    int limitCount = 1;
    QString onMiss = "wait";      // wait / stop
    QString hotkey = "f7";
    bool hotkeyEnabled = false;
};

struct WatchCfg {
    QString tmpl;
    double confidence = 0.85;
    int interval = 1000;
    int offx = 0;
    int offy = 0;
    bool limitEnabled = false;
    int limitCount = 1;
    QString onMiss = "wait";
    QString process;
    QString title;
    QString hotkey = "f8";
    bool hotkeyEnabled = false;
};

struct RecordCfg {
    QString script;
    QString speedMode = "real";   // real / fixed
    int fixedInterval = 1000;
    int loops = 1;
    bool loopForever = false;
    QString hotkey = "f9";
    bool hotkeyEnabled = false;
};

struct ClickCfg {
    QString type = "left";        // left/right/middle/x1/x2/key
    QString key = "space";        // type == key 时使用
    bool dbl = false;
};

struct EmergencyCfg {
    QString hotkey = "f12";
    bool enabled = true;
};

struct UiCfg {
    int w = 0;
    int h = 0;
    int tab = 0;
};

struct Config {
    MouseCfg mouse;
    ImageCfg image;
    WatchCfg watch;
    RecordCfg record;
    ClickCfg click;
    EmergencyCfg emergency;
    UiCfg ui;

    static Config load();
    bool save() const;
    void reset();

    static QString writableDir();
    static QString templatesDir();
    static QString scriptsDir();
    static QString configPath();
};

}  // namespace ac
