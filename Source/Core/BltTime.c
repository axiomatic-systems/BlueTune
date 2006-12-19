/*****************************************************************
|
|   BlueTune - Time Library
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltTime.h"

/*----------------------------------------------------------------------
|   functions
+---------------------------------------------------------------------*/
BLT_TimeStamp 
BLT_TimeStamp_FromSamples(ATX_Int64 sample_count,
                          ATX_Int32 sample_rate)
{
    ATX_Int64     duration;
    ATX_Int64     seconds;
    BLT_TimeStamp time_stamp;

    duration = sample_count;
    ATX_Int64_Mul_Int32(duration, 1000000);
    ATX_Int64_Div_Int32(duration, sample_rate);
    seconds = duration;
    ATX_Int64_Div_Int32(seconds, 1000000);
    time_stamp.seconds = ATX_Int64_Get_Int32(seconds);
    ATX_Int64_Mul_Int32(seconds, 1000000);
    ATX_Int64_Sub_Int64(duration, seconds);
    time_stamp.nanoseconds = ATX_Int64_Get_Int32(duration)*1000;

    return time_stamp;
}
