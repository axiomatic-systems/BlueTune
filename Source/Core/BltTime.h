/*****************************************************************
|
|   BlueTune - Time Definitions
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune Time Interface Header file
 */

#ifndef _BLT_TIME_H_
#define _BLT_TIME_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_UInt8 h;
    BLT_UInt8 m;
    BLT_UInt8 s;
    BLT_UInt8 f;
} BLT_TimeCode;

typedef struct {
    BLT_Int32 seconds;
    BLT_Int32 nanoseconds;
} BLT_TimeStamp;

typedef BLT_TimeStamp BLT_Time;

/*----------------------------------------------------------------------
|   macros
+---------------------------------------------------------------------*/
#define BLT_TimeStamp_Set(t, s, n) \
    do {(t).seconds = (s); (t).nanoseconds = (n);} while(0)

#define BLT_TimeStamp_IsLater(t0,t1)           \
(                                              \
    ((t0).seconds > (t1).seconds) ||    \
    (                                          \
        (t0).seconds == (t1).seconds &&        \
        (t0).nanoseconds > (t1).nanoseconds    \
    )                                          \
)

#define BLT_TimeStamp_IsLaterOrEqual(t0,t1)       \
(                                                 \
    (                                             \
        (t0).seconds == (t1).seconds &&           \
        (t0).nanoseconds == (t1).nanoseconds      \
    ) ||                                   \
    (                                             \
        ((t0).seconds > (t1).seconds) ||   \
        (                                         \
            (t0).seconds == (t1).seconds &&       \
            (t0).nanoseconds > (t1).nanoseconds   \
        )                                         \
    )                                             \
)

#define BLT_TimeStamp_Add(t0,t1,t2) do {                        \
    (t0).seconds = (t1).seconds + (t2).seconds;                 \
    (t0).nanoseconds = (t1).nanoseconds + (t2).nanoseconds;     \
    if ((t0).nanoseconds > 1000000000) {                        \
        (t0).seconds++;                                         \
        (t0).nanoseconds -= 1000000000;                         \
    }                                                           \
} while (0)

#define BLT_TimeStamp_Sub(t0,t1,t2) do {                        \
    (t0).seconds = (t1).seconds - (t2).seconds;                 \
    (t0).nanoseconds = (t1).nanoseconds - (t2).nanoseconds;     \
    if ((t0).nanoseconds < 0) {                                 \
        (t0).seconds--;                                         \
        (t0).nanoseconds += 1000000000;                         \
    }                                                           \
} while (0)

#define BLT_TimeStamp_ToInt64(t, i) do {                        \
    ATX_Int64_Set_Int32(i, t.seconds);                          \
    ATX_Int64_Mul_Int32(i, 1000000000);                         \
    ATX_Int64_Add_Int32(i, t.nanoseconds);                      \
} while (0)

/*----------------------------------------------------------------------
|   functions
+---------------------------------------------------------------------*/
BLT_TimeStamp
BLT_TimeStamp_FromSamples(ATX_Int64 sample_count,
                          ATX_Int32 sample_rate);

#endif /* _BLT_TIME_H_ */
