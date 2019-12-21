//
//  main.m
//  CocoaPlayer
//
//  Created by Gilles on 9/6/08.
//  Copyright Gilles Boccon-Gibod 2008-2012. All rights reserved.
//

#import <UIKit/UIKit.h>
#include "Neptune.h"
#include "BtPlayerServer.h"

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end

@interface AppDelegate ()
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // Override point for customization after application launch.
    (void)application;
    (void)launchOptions;
    return YES;
}

- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)connectingSceneSession options:(UISceneConnectionOptions *)options {
    (void)application;
    (void)connectingSceneSession;
    (void)options;
    return [[UISceneConfiguration alloc] initWithName:@"Default Configuration" sessionRole:connectingSceneSession.role];
}

- (void)application:(UIApplication *)application didDiscardSceneSessions:(NSSet<UISceneSession *> *)sceneSessions {
    (void)application;
    (void)sceneSessions;
}

@end


class PlayerServerThread : public NPT_Thread
{
public:
    PlayerServerThread(unsigned int port) : NPT_Thread(true), m_Port(port) {}
    
    // NPT_Runnable methods
    virtual void Run() {
        NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    
        // create the server
        NPT_String bundle_root = [[[NSBundle mainBundle] bundlePath] UTF8String];
        BtPlayerServer* server = new BtPlayerServer(bundle_root+"/WebRoot");

        // loop until a termination request arrives
        server->Loop();
        
        // delete the controller
        delete server;
        
        [pool release];
    }
    
    unsigned int m_Port;
};

int main(int argc, char *argv[])
{
    (new PlayerServerThread(9080))->Start();

    return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
}
