#include "Timer.h"

// -----------------------------------------------------------------------------

static LARGE_INTEGER liPerfFreq = []
{
    LARGE_INTEGER li;

    ::QueryPerformanceFrequency(&li);
    return li;
}();

// -----------------------------------------------------------------------------

ULONGLONG GetCurrentTimeMs()
{
    LARGE_INTEGER liNow;

    ::QueryPerformanceCounter(&liNow);
    return (liNow.QuadPart * 1000ULL) / liPerfFreq.QuadPart;
}
