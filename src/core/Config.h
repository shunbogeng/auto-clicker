// 配置：读写 JSON，键名沿用旧版，方便老用户把配置/脚本搬过来。
#pragma once

#include <QSize>
#include <QString>

namespace ac {

struct MouseCfg {
    int interval = 1000;
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
};

struct RecordCfg {
    QString script;
    QString speedMode = "real";   // real / fixed
    int fixedInterval = 1000;
    int loops = 1;
    bool loopForever = false;
};

struct ClickCfg {
    QString type = "left";        // left/right/middle/x1/x2/key
    QString key = "space";        // type == key 时使用
    bool dbl = false;
};

// 全局只有一个热键：按下就启动/停止「当前页签」的功能，键位在设置里改。
struct HotkeyCfg {
    QString key = "f6";
    bool enabled = false;   // 默认关闭，不占键位
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
    HotkeyCfg hotkey;
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
