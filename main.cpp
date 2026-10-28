#include <QCommandLineParser>
#include "mainwindow.h"

#include <QApplication>
#include "gui_startup_smoke.h"
#include "serial_port/sh7058_identification_session.h"

int main(int argc, char *argv[])
{
    bool debug_console = false;

    QCommandLineParser cmdParser;
    cmdParser.setApplicationDescription("FastECU - software to work on and modify ECUs");
    cmdParser.addHelpOption();
    addSH7058IdentificationOptions(cmdParser);
    cmdParser.addOption({"gui-smoke-test", "Show the real GUI with an isolated profile and hardware opening disabled, then close; write NEW result JSON", "file"});
    QCommandLineOption cmdHost(QStringList() << "s" << "host",
                               "Remote host address and port, for example 127.0.0.1:33314, local:33315",
                               "host:port");
    cmdParser.addOption(cmdHost);
    QCommandLineOption cmdPassword(QStringList() << "p" << "password",
                                   "Remote host password",
                                   "password");
    cmdParser.addOption(cmdPassword);
    QCommandLineOption cmdDebug(QStringList() << "d" << "debug",
                                "Enable console debug output");
    cmdParser.addOption(cmdDebug);

    QCommandLineOption cmdTrace("j2534-trace", "Write optional J2534 JSONL trace to a NEW file (may contain sensitive ECU data)", "file");
    cmdParser.addOption(cmdTrace);

    //Locate debug option before QCommandLineParser to open console properly
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-d") ||
            !strcmp(argv[i], "--debug") ||
            !strcmp(argv[i],  "-debug"))
            debug_console = true;
    }

#ifdef _WIN32
    if (!debug_console)
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
    }
    else
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
    }
#endif

    int return_code;

    do
    {
        QApplication a(argc, argv);
        a.setApplicationVersion(FileActions::ConfigValuesStructure().software_version);

        //Parser works only after QApplication initialization
        cmdParser.process(a);

        const bool identityMode=cmdParser.isSet("sh7058-identify");
        const bool identityOptions=identityMode || cmdParser.isSet("identity-driver-sha256") ||
            cmdParser.isSet("reviewed-obdx-driver") || cmdParser.isSet("ecu-identification-authorized") || cmdParser.isSet("identity-output");
        if (identityOptions) {
            if (!identityMode || cmdParser.isSet("gui-smoke-test") || cmdParser.isSet(cmdHost) ||
                cmdParser.isSet(cmdPassword) || cmdParser.isSet(cmdTrace)) {
                qCritical("Identity worker refuses mixed GUI smoke/remote/trace or incomplete mode options"); return 2;
            }
            return runSH7058IdentificationWorker(cmdParser); // Never constructs MainWindow/background serial users.
        }
        if (cmdParser.isSet("gui-smoke-test")) {
            if (cmdParser.isSet(cmdHost) || cmdParser.isSet(cmdPassword) || cmdParser.isSet(cmdTrace)) {
                qCritical("GUI smoke refuses remote/trace options"); return 2;
            }
            return runGuiStartupSmoke(a, cmdParser.value("gui-smoke-test"));
        }

        QString addr = cmdParser.value(cmdHost);
        QString password = cmdParser.value(cmdPassword);

        if (cmdParser.isSet(cmdTrace)) {
            if (cmdParser.value(cmdTrace).isEmpty()) { qCritical("--j2534-trace requires a new output filename"); return 2; }
            qWarning("J2534 trace enabled: may contain sensitive ECU/calibration data; review before sharing. Known ISO15765 security/VIN data is omitted.");
            a.setProperty("j2534TracePath", cmdParser.value(cmdTrace));
        }

        MainWindow w(addr, password, nullptr);

        QScreen *screen = QGuiApplication::primaryScreen();
        QRect  screenGeometry = screen->geometry();
        w.move(screenGeometry.center() - w.rect().center());

        w.show();

        return_code = a.exec();
    } while (return_code == RESTART_CODE);

    return return_code;
}
