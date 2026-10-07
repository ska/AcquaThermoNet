#include "mainwindow.h"
#include "logging.h"

#include <QApplication>
#include <QThread>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QScopedPointer>
#include <QDir>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

#include "mqtt.h"
#include "common.h"
#include "configuration.h"
#include "zonemodel.h"
#include "singleinstance.h"
#include "termoregolazione.h"
#include "weather.h"
#include "serialuart.h"
#include "modbusframeprocessor.h"
#include "watchdog.h"
#include "opensslpreload.h"
#include "telegrambot.h"
#include "telegramnotifier.h"
#include "windowdetector.h"

QString getMacAddress()
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for(const QNetworkInterface &netInterface : interfaces)
    {
        // Return only the first non-loopback MAC Address
        if (!(netInterface.flags() & QNetworkInterface::IsLoopBack) && (netInterface.type() & QNetworkInterface::Ethernet))
            return netInterface.hardwareAddress();
    }
    return QString();
}

/*
 * Unix signals (SIGTERM/SIGINT): the handler only writes to a socketpair,
 * the Qt side reads it and quits the event loop, so the normal shutdown
 * (relays OFF, MQTT offline) runs. See Qt doc "Calling Qt Functions From
 * Unix Signal Handlers".
 * * */
static int sigFd[2];

static void unixSignalHandler(int)
{
    char a = 1;
    ssize_t r = ::write(sigFd[0], &a, sizeof(a));
    (void) r;
}

static bool setupUnixSignals()
{
    if( ::socketpair(AF_UNIX, SOCK_STREAM, 0, sigFd) != 0 )
        return false;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = unixSignalHandler;
    sigemptyset(&sa.sa_mask);
    /* One shot: a second signal gets the default action (forced kill) */
    sa.sa_flags = SA_RESTART | SA_RESETHAND;

    if( sigaction(SIGTERM, &sa, nullptr) != 0 )
        return false;
    if( sigaction(SIGINT, &sa, nullptr) != 0 )
        return false;
    return true;
}

int main(int argc, char *argv[])
{
    int rv;
    QString mac = getMacAddress().remove(':');

    /*
     * Start app
     * * */
    /* Before QApplication: works without a display */
    for(int i = 1; i < argc; i++)
    {
        if(!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version"))
        {
            printf("%s\n", SW_BANNER);
            return 0;
        }
    }

    Logging::install();
    QApplication a(argc, argv);
    QCoreApplication::setApplicationName(SW_NAME);
    QCoreApplication::setApplicationVersion(SW_VER);
    QFileInfo fi(argv[0]);
    qCInfo(lcApp).noquote() << SW_BANNER;
    qCInfo(lcApp).noquote() << fi.fileName() << " Thread ID: "        << QThread::currentThreadId();

    /* One instance per target, checked before touching any hardware.
     * Must live until exit: the lock lasts as long as this object. */
    SingleInstance instance(SW_NAME);
    if( !instance.tryLock() )
        return EXIT_ALREADY_RUNNING;

    if( setupUnixSignals() )
    {
        QSocketNotifier *sigNotifier = new QSocketNotifier(sigFd[1], QSocketNotifier::Read, &a);
        QObject::connect(sigNotifier, &QSocketNotifier::activated, &a, [sigNotifier] {
            sigNotifier->setEnabled(false);
            char c;
            ssize_t r = ::read(sigFd[1], &c, sizeof(c));
            (void) r;
            qCInfo(lcApp) << "Termination signal received, shutting down";
            QCoreApplication::quit();
        });
    } else {
        qCWarning(lcApp) << "Unix signal handler setup failed";
    }

    /*
     * Configuration and zones
     * * */
    const QDir appDir(QDir::currentPath());
    Configuration::ensureSettings(appDir.filePath("setting.ini"), appDir.filePath("setting.default.ini"));
    Configuration conf(appDir.filePath("setting.ini"), appDir.filePath("state.ini"));

    QString logPath;
    qint64  logMaxBytes;
    int     logFiles;
    conf.loadLog(logPath, logMaxBytes, logFiles);
    Logging::enableFile(logPath, logMaxBytes, logFiles);
    OpenSslPreload::load();

    /* How the previous run ended; "not clean" until the shutdown below */
    const int lastExit = conf.lastExitState();
    conf.setCleanExit(false);

    ZoneModel *zones = new ZoneModel(&conf, ZoneModel::SAVE_DELAY_MS, &a);

    /*
     * Mqtt
     * * */
    mqtt_brk_t mqtt_info;
    conf.loadMqttInfo(mqtt_info);
    Mqtt *mq = new Mqtt(mqtt_info, conf.uniqueId(mac), zones, &a);

    /*
     * Weather
     * * */
    const RegulationConfig regulation = conf.loadRegulation();
    const WeatherConfig weatherConfig = conf.loadWeather();
    Weather wh(weatherConfig);
    wh.startPoll();
    /* HA outdoor sensors: unavailable when the frost protection would
     * count the outdoor data as unknown */
    mq->setWeatherConfig(weatherConfig.enabled(), regulation.frost.outdoorMaxAgeMin * 60,
                         weatherConfig.provider == WeatherConfig::MetNo ? "met.no" : "wttr.in");
    QObject::connect( &wh, &Weather::newWeatherInfo, mq, &Mqtt::setWeather );

    /*
     * Open window guessed from the temperature (log and Telegram for now)
     * * */
    WindowWatch *windows = new WindowWatch(zones, conf.loadWindow(), &a);

    /*
     * WatchDog: refreshed by the main event loop, a hang reboots the board
     * * */
    WatchDog *wdt = new WatchDog(&a);
    wdt->start();

    /*
     * ModBus (serial is event driven, runs in this thread)
     * * */
    QString serialPort;
    qint32  serialBaud;
    conf.loadSerial(serialPort, serialBaud);
    SerialUart *serial = new SerialUart(serialPort, serialBaud, &a);
    serial->open();

    ModBusFrameProcessor *frameProcessor = new ModBusFrameProcessor(&a);
    QObject::connect(frameProcessor,    &ModBusFrameProcessor::frameToSend,     serial,         &SerialUart::sendFrame);
    QObject::connect(serial,            &SerialUart::frameReceived,             frameProcessor, &ModBusFrameProcessor::frameIncoming);

    /*
     * Termoregolazione
     * * */
    Termoregolazione *tr = new Termoregolazione(zones, frameProcessor, regulation, &conf, &a);
    QString relayLogPath;
    int     relayLogKeep;
    conf.loadRelayLog(relayLogPath, relayLogKeep);
    QScopedPointer<RelayLog> relayLog;
    if(!relayLogPath.isEmpty())
    {
        relayLog.reset(new RelayLog(relayLogPath, relayLogKeep));
        tr->setRelayLog(relayLog.data());
    }
    tr->allRelaysOff();     /* safe state until first valid sensor data */
    /* Commands are dropped while the port is closed: resync when it opens */
    QObject::connect(serial, &SerialUart::portOpenChanged, tr, [tr](bool open) {
        if(open)
            tr->forceRefreshAllZones();
    });
    frameProcessor->startPoll(10);

    /*
     * MainWindow
     * * */
    MainWindow w(zones, mq);
#if DEVICE == DESKTOP
    w.show();
#else
    w.setWindowFlags( w.windowFlags() | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint );
    w.showMaximized();
#endif

    QObject::connect( &wh,              &Weather::newWeatherInfo,               &w, &MainWindow::weatherInfoIsChanged );
    QObject::connect( &wh, &Weather::newWeatherInfo, tr, [tr](weather_t wi) { tr->setOutdoorTemperature(wi.temp); });
    QObject::connect( frameProcessor,   &ModBusFrameProcessor::onlineChanged,   &w, &MainWindow::onModbusOnlineChanged );
    QObject::connect( serial,           &SerialUart::portOpenChanged,           &w, &MainWindow::onSerialPortChanged );
    w.onSerialPortChanged(serial->isOpen());

    /*
     * Telegram: alarms and status, independent of MQTT/Home Assistant
     * * */
    const TelegramConfig tgConf = conf.loadTelegram();
    TelegramBot *tgBot = nullptr;
    TelegramNotifier *tg = nullptr;
    if(tgConf.enabled)
    {
        tgBot = new TelegramBot(tgConf, &a);
        tg = new TelegramNotifier(zones, tgConf, relayLog.data(), &a);
        QObject::connect( tg,             &TelegramNotifier::broadcast,           tgBot, &TelegramBot::sendToAll );
        QObject::connect( tg,             &TelegramNotifier::reply,               tgBot, &TelegramBot::send );
        QObject::connect( tgBot,          &TelegramBot::commandReceived,          tg,    &TelegramNotifier::onCommand );
        QObject::connect( frameProcessor, &ModBusFrameProcessor::onlineChanged,   tg,    &TelegramNotifier::setModbusOnline );
        QObject::connect( serial,         &SerialUart::portOpenChanged,           tg,    &TelegramNotifier::setSerialOpen );
        QObject::connect( &wh,            &Weather::newWeatherInfo,               tg,    &TelegramNotifier::setWeather );
        QObject::connect( mq,             &Mqtt::gatewayStateChanged,             tg,    &TelegramNotifier::setGatewayState );
        QObject::connect( windows,        &WindowWatch::windowOpened,             tg,    &TelegramNotifier::onWindowOpened );
        QObject::connect( windows,        &WindowWatch::windowClosed,             tg,    &TelegramNotifier::onWindowClosed );
        QObject::connect( mq, &Mqtt::clientStateChanged, tg, [tg](quint8 state) {
            tg->setMqttConnected(state == QMqttClient::Connected);
        });
        tg->setSerialOpen(serial->isOpen());
        tg->announceStart(lastExit);
        tgBot->start();
    }

    w.show();
    rv = a.exec();

    /*
     * End of the game
     * * */

    /* Leave the plant in a safe state: relays OFF, confirmed by reading
     * the board back; MQTT offline and disconnected. Both need the event
     * loop, so keep processing events (max 5s). */
    tr->beginShutdown();
    mq->shutdown();
    zones->flushPendingSaves();
    if(tg)
        tg->announceStop();
    bool relaysOff = false;
    QElapsedTimer flushTimer;
    flushTimer.start();
    while( flushTimer.elapsed() < 5000 )
    {
        QCoreApplication::processEvents();
        QThread::msleep(10);
        if( !relaysOff && serial->isOpen() && serial->isIdle() )
            relaysOff = tr->verifyRelaysOff();
        if( (relaysOff || !serial->isOpen()) && mq->isDisconnected() && (!tgBot || tgBot->isIdle()) )
            break;
    }
    if( relaysOff )
        qCInfo(lcApp) << "All relays confirmed OFF";
    else
        qCWarning(lcApp) << "Relays OFF not confirmed on exit";

    /* Disarm only now: the shutdown loop above keeps refreshing it */
    wdt->stop();
    conf.setCleanExit(true);

    qCDebug(lcApp) << "End Application";

    return rv;
}
