//
//  main.m
//  CocoaPlayer
//
//  Created by Gilles on 9/6/08.
//  Copyright Gilles Boccon-Gibod 2008. All rights reserved.
//

#import <UIKit/UIKit.h>
#include "Neptune.h"
#include "BtPlayerServer.h"

class PlayerServerThread : public NPT_Thread
{
public:
    PlayerServerThread() : NPT_Thread(true) {}
    
    // NPT_Runnable methods
    virtual void Run() {
        // create the controller
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
    }
};

int main(int argc, char *argv[])
{
    (new PlayerServerThread())->Start();
    
    return UIApplicationMain(argc, argv, nil, nil);
}
