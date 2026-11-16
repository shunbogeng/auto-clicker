#include "core/Config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace ac {

namespace {

const char* kFileName = "config.json";

// 程序所在目录优先（便携），不可写时退回用户数据目录
QString pickDir() {
    QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty() && QFileInfo(appDir).isWritable()) return appDir;
    QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (data.isEmpty()) data = QDir::homePath() + "/.auto-clicker";
    QDir().mkpath(data);
    return data;
}

}  // namespace

QString Config::writableDir() {
    static QString dir = pickDir();
    return dir;
}

QString Config::templatesDir() {
    const QString d = writableDir() + "/templates";
    QDir().mkpath(d);
    return d;
}

QString Config::scriptsDir() {
    const QString d = writableDir() + "/scripts";
    QDir().mkpath(d);
    return d;
}

QString Config::configPath() { return writableDir() + "/" + kFileName; }

void Config::reset() { *this = Config(); }

Config Config::load() {
    Config c;
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return c;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return c;

    const QJsonObject o = doc.object();
    auto obj = [&](const char* k) { return o.value(k).toObject(); };

    QJsonObject m = obj("mouse");
    c.mouse.interval = m.value("interval").toInt(c.mouse.interval);

    QJsonObject i = obj("image");
    c.image.tmpl = i.value("template").toString(c.image.tmpl);
    c.image.confidence = i.value("confidence").toDouble(c.image.confidence);
    c.image.interval = i.value("interval").toInt(c.image.interval);
    c.image.offx = i.value("offset_x").toInt(c.image.offx);
    c.image.offy = i.value("offset_y").toInt(c.image.offy);
    c.image.multi = i.value("multi").toBool(c.image.multi);
    c.image.limitEnabled = i.value("limit_enabled").toBool(c.image.limitEnabled);
    c.image.limitCount = i.value("limit_count").toInt(c.image.limitCount);
    c.image.onMiss = i.value("on_miss").toString(c.image.onMiss);

    QJsonObject w = obj("watch");
    c.watch.tmpl = w.value("template").toString(c.watch.tmpl);
    c.watch.confidence = w.value("confidence").toDouble(c.watch.confidence);
    c.watch.interval = w.value("interval").toInt(c.watch.interval);
    c.watch.offx = w.value("offset_x").toInt(c.watch.offx);
    c.watch.offy = w.value("offset_y").toInt(c.watch.offy);
    c.watch.limitEnabled = w.value("limit_enabled").toBool(c.watch.limitEnabled);
    c.watch.limitCount = w.value("limit_count").toInt(c.watch.limitCount);
    c.watch.onMiss = w.value("on_miss").toString(c.watch.onMiss);
    c.watch.process = w.value("process").toString(c.watch.process);
    c.watch.title = w.value("title").toString(c.watch.title);

    QJsonObject r = obj("record");
    c.record.script = r.value("script").toString(c.record.script);
    c.record.speedMode = r.value("speed_mode").toString(c.record.speedMode);
    c.record.fixedInterval = r.value("fixed_interval").toInt(c.record.fixedInterval);
    c.record.loops = r.value("loops").toInt(c.record.loops);
    c.record.loopForever = r.value("loop_forever").toBool(c.record.loopForever);

    QJsonObject k = obj("click");
    c.click.type = k.value("type").toString(c.click.type);
    c.click.key = k.value("key").toString(c.click.key);
    c.click.dbl = k.value("dbl").toBool(c.click.dbl);

    // 老配置里的 emergency 段直接忽略（急停键已移除），旧热键不再迁移。
    QJsonObject h = obj("hotkey");
    c.hotkey.key = h.value("key").toString(c.hotkey.key);
    c.hotkey.enabled = h.value("enabled").toBool(c.hotkey.enabled);

    QJsonObject u = obj("ui");
    c.ui.w = u.value("w").toInt(c.ui.w);
    c.ui.h = u.value("h").toInt(c.ui.h);
    c.ui.tab = u.value("tab").toInt(c.ui.tab);
    return c;
}

bool Config::save() const {
    QJsonObject root;
    root["mouse"] = QJsonObject{{"interval", mouse.interval}};
    root["image"] = QJsonObject{{"template", image.tmpl},
                                {"confidence", image.confidence},
                                {"interval", image.interval},
                                {"offset_x", image.offx},
                                {"offset_y", image.offy},
                                {"multi", image.multi},
                                {"limit_enabled", image.limitEnabled},
                                {"limit_count", image.limitCount},
                                {"on_miss", image.onMiss}};
    root["watch"] = QJsonObject{{"template", watch.tmpl},
                                {"confidence", watch.confidence},
                                {"interval", watch.interval},
                                {"offset_x", watch.offx},
                                {"offset_y", watch.offy},
                                {"limit_enabled", watch.limitEnabled},
                                {"limit_count", watch.limitCount},
                                {"on_miss", watch.onMiss},
                                {"process", watch.process},
                                {"title", watch.title}};
    root["record"] = QJsonObject{{"script", record.script},
                                 {"speed_mode", record.speedMode},
                                 {"fixed_interval", record.fixedInterval},
                                 {"loops", record.loops},
                                 {"loop_forever", record.loopForever}};
    root["click"] = QJsonObject{{"type", click.type}, {"key", click.key}, {"dbl", click.dbl}};
    root["hotkey"] = QJsonObject{{"key", hotkey.key}, {"enabled", hotkey.enabled}};
    root["ui"] = QJsonObject{{"w", ui.w}, {"h", ui.h}, {"tab", ui.tab}};

    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

}  // namespace ac
