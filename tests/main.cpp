#include <QCoreApplication>
#include <QTest>
#include <cstdio>

int runChronoTests(int argc, char *argv[]);
int runConfigTests(int argc, char *argv[]);
int runExerciseTests(int argc, char *argv[]);
int runFrostTests(int argc, char *argv[]);
int runLoggingTests(int argc, char *argv[]);
int runModbusTests(int argc, char *argv[]);
int runMonoClockTests(int argc, char *argv[]);
int runMqttParseTests(int argc, char *argv[]);
int runNetInfoTests(int argc, char *argv[]);
int runRegulationTests(int argc, char *argv[]);
int runRelayLogTests(int argc, char *argv[]);
int runTelegramTests(int argc, char *argv[]);
int runWeatherTests(int argc, char *argv[]);
int runZoneModelTests(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    int failed = 0;
    failed += runMonoClockTests(argc, argv);
    failed += runModbusTests(argc, argv);
    failed += runMqttParseTests(argc, argv);
    failed += runNetInfoTests(argc, argv);
    failed += runConfigTests(argc, argv);
    failed += runChronoTests(argc, argv);
    failed += runZoneModelTests(argc, argv);
    failed += runRegulationTests(argc, argv);
    failed += runExerciseTests(argc, argv);
    failed += runFrostTests(argc, argv);
    failed += runRelayLogTests(argc, argv);
    failed += runTelegramTests(argc, argv);
    failed += runWeatherTests(argc, argv);
    /* Last: installs the file log handler for the rest of the process */
    failed += runLoggingTests(argc, argv);

    fprintf(stderr, "\n%s: %d failed test function(s)\n", failed ? "FAIL" : "OK", failed);
    return failed ? 1 : 0;
}
