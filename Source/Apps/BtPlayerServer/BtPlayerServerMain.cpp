/*****************************************************************
|
|   BlueTune - Player Web Service
|
|   (c) 2002-2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "BtPlayerServer.h"

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int /*argc*/, char** /*argv*/)
{
    // create the controller a
    BtPlayerServer* server = new BtPlayerServer();

    // create a thread to handle notifications
    NPT_Thread notification_thread(*server);
    notification_thread.Start();
    
    // loop until a termination request arrives
    server->Loop();
    
    // wait for the notification thread to end
    notification_thread.Wait();
    
    // delete the controller
    delete server;

    return 0;
}
