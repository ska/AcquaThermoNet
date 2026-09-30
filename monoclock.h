#ifndef MONOCLOCK_H
#define MONOCLOCK_H

#include <QtGlobal>

/*
 * Monotonic time for every duration and timeout (sensor timeout, min
 * cycle, relay settle, "updated N min"). CLOCK_MONOTONIC never jumps:
 * the wall clock does, e.g. from 1970 to today when NTP syncs a board
 * without RTC battery, or backwards when set by hand.
 */
namespace MonoClock
{
    /* ms since an arbitrary point (boot), always > 0 */
    qint64 nowMs();

    /* Tests only: move the clock forward instead of waiting */
    void advanceForTest(qint64 ms);
}

#endif // MONOCLOCK_H
