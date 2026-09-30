#include "monoclock.h"
#include <QElapsedTimer>

namespace
{
    qint64 s_testOffsetMs = 0;
}

/**
 * @brief MonoClock::nowMs
 * @return monotonic ms (QElapsedTimer reference: CLOCK_MONOTONIC on Linux)
 */
qint64 MonoClock::nowMs()
{
    QElapsedTimer t;
    t.start();
    /* +1: 0 stays free to mean "never" */
    return t.msecsSinceReference() + s_testOffsetMs + 1;
}

/**
 * @brief MonoClock::advanceForTest
 * @param ms
 */
void MonoClock::advanceForTest(qint64 ms)
{
    s_testOffsetMs += ms;
}
