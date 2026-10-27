#ifndef FASTECU_GUI_STARTUP_SMOKE_H
#define FASTECU_GUI_STARTUP_SMOKE_H
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTimer>
#include <QWindow>
#include <QXmlStreamReader>
#include "mainwindow.h"

// Tests the shipping executable. This mode only removes capabilities: no remote
// address, no device opening, no user input and a disposable, initially empty profile.
class GuiSmokeObserver final : public QObject {
public:
    QWidget *window = nullptr;
    int paints = 0;
    QString dialogError;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == window && event->type() == QEvent::Paint) ++paints;
        if (event->type() == QEvent::Show && qobject_cast<QDialog *>(object)) {
            dialogError = QString("Unexpected startup dialog: ") + object->metaObject()->className();
            if (auto *box = qobject_cast<QMessageBox *>(object)) dialogError += ": " + box->text();
            qCritical().noquote() << dialogError; // Fail, never auto-accept a warning to pass.
        }
        return false;
    }
};
inline int runGuiStartupSmoke(QApplication &app, const QString &output) {
    if (output.isEmpty() || QFileInfo::exists(output) || QFileInfo::exists(output + ".png")) return 2;
    QJsonObject result{{"schema", "fastecu.gui-startup.v1"}, {"ok", false},
        {"hardware_opening_disabled", true}, {"remote_enabled", false},
        {"platform", QGuiApplication::platformName()}};
    const auto finish = [&](int code, const QString &error) {
        if (!error.isEmpty()) result["error"] = error;
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 1;
        const auto bytes = QJsonDocument(result).toJson();
        if (file.write(bytes) != bytes.size() || !file.flush()) return 1;
        return code;
    };
    // Check actual extracted defaults, not just embedded fallbacks.
    for (const auto &name : {"fastecu.cfg", "protocols.cfg", "menu.cfg", "logger.cfg"}) {
        QFile file(QCoreApplication::applicationDirPath() + "/config/" + name);
        if (!file.open(QIODevice::ReadOnly)) return finish(1, QString("Packaged config missing: ") + name);
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) xml.readNext();
        if (xml.hasError()) return finish(1, QString("Packaged config XML invalid: ") + name);
    }
    QTemporaryDir profile;
    if (!profile.isValid()) return finish(1, "Unable to create isolated profile");
    app.setProperty("guiSmokeTest", true);
    app.setProperty("guiSmokeProfile", profile.path());
    GuiSmokeObserver observer;
    app.installEventFilter(&observer);
    int code = 1;
    int heartbeats = 0;
    bool closed = false;
    {
        qInfo("GUI smoke: constructing actual MainWindow");
        MainWindow window({}, {}, nullptr);
        result["mainwindow_constructed"] = true;
        result["protocol_count"] = window.property("startupProtocolCount").toInt();
        result["logger_ready"] = window.property("startupLoggerReady").toBool();
        result["logger_definition_configured"] = window.property("startupLoggerDefinitionConfigured").toBool();
        window.setEnabled(false); // No interactive actions during the smoke.
        observer.window = &window;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            ++heartbeats;
            if (heartbeats < 20) return;
            result["window_visible"] = window.isVisible();
            result["window_exposed"] = window.windowHandle() && window.windowHandle()->isExposed();
            result["native_window_id"] = QString::number(qulonglong(window.winId()));
            result["paint_events"] = observer.paints;
            result["heartbeats"] = heartbeats;
            result["unexpected_dialog"] = observer.dialogError;
            const bool ready = window.isVisible() && window.windowHandle() && window.windowHandle()->isExposed()
                && observer.paints > 0 && observer.dialogError.isEmpty()
                && result["protocol_count"].toInt() > 0 && result["logger_ready"].toBool();
            result["ready"] = ready;
            result["screenshot_saved"] = window.grab().save(output + ".png");
            timer.stop();
            window.close(); // Real closeEvent and normal destruction, not terminate/exit().
        });
        window.show();
        timer.start(100);
        code = app.exec();
        closed = !window.isVisible();
        observer.window = nullptr;
    }
    app.removeEventFilter(&observer);
    result["window_closed"] = closed;
    result["mainwindow_destroyed"] = true;
    result["event_loop_exit_code"] = code;
    result["ok"] = code == 0 && closed && result["ready"].toBool() && result["screenshot_saved"].toBool();
    qInfo("GUI smoke: MainWindow destroyed");
    return finish(result["ok"].toBool() ? 0 : 1, result["ok"].toBool() ? QString() : "GUI startup/close validation failed");
}
#endif
