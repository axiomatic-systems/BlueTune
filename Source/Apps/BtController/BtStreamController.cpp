/*****************************************************************
|
|   BlueTune - Stream Controller
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "Neptune.h"
#include "BlueTune.h"
#include "BtStreamController.h"

/*----------------------------------------------------------------------
|    BtStreamController::BtStreamController
+---------------------------------------------------------------------*/
BtStreamController::BtStreamController(NPT_InputStreamReference& input,
                                       BLT_Player&               player) :
    m_InputStream(input),
    m_Player(player)
{
}

/*----------------------------------------------------------------------
|    BtStreamController::Run
+---------------------------------------------------------------------*/
void
BtStreamController::Run()
{
    char       buffer[1024];
    bool       done = false;
    BLT_Result result;

    // get the command stream
    NPT_BufferedInputStream input(m_InputStream, 0);

    do {
        NPT_Size bytes_read;
        result = input.ReadLine(buffer, 
                                sizeof(buffer), 
                                &bytes_read);
        if (NPT_SUCCEEDED(result)) {
            if (NPT_StringsEqualN(buffer, "set-input ", 10)) {
                m_Player.SetInput(&buffer[10]);
            } else if (NPT_StringsEqualN(buffer, "add-node ", 9)) {
                m_Player.AddNode(&buffer[9]);
            } else if (NPT_StringsEqual(buffer, "play")) {
                m_Player.Play();
            } else if (NPT_StringsEqual(buffer, "stop")) {
                m_Player.Stop();
            } else if (NPT_StringsEqual(buffer, "pause")) {
                m_Player.Pause();
            } else if (NPT_StringsEqualN(buffer, "seek-to-timestamp", 17)) {
                //m_Player.DoSeekToTimeStamp(buffer+17);
            } else if (NPT_StringsEqualN(buffer, "exit", 4)) {
                done = BLT_TRUE;
            } else {
                ATX_Debug("ERROR: invalid command\n");
            }
        } else {
            ATX_Debug("end: %d\n", result);
        }
    } while (BLT_SUCCEEDED(result) && !done);

    // interrupt ourselves so that we can exit our message pump loop
    m_Player.Interrupt();
}

