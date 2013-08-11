/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Mozilla XUL Toolkit.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Stan Shebs <shebs@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

// NSApplication delegate for Mac OS X Cocoa API.

// As of 10.4 Tiger, the system can send six kinds of Apple Events to an application;
// a well-behaved XUL app should have some kind of handling for all of them.
//
// See http://developer.apple.com/documentation/Cocoa/Conceptual/ScriptableCocoaApplications/SApps_handle_AEs/chapter_11_section_3.html for details.

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>

#include "nsCOMPtr.h"
#include "nsIBaseWindow.h"
#include "nsINativeAppSupport.h"
#include "nsIWidget.h"
#include "nsIWindowMediator.h"
#include "nsAppRunner.h"
#include "nsComponentManagerUtils.h"
#include "nsCommandLineServiceMac.h"
#include "nsIServiceManager.h"
#include "nsServiceManagerUtils.h"
#include "nsIAppStartup.h"
#include "nsIObserverService.h"
#include "nsISupportsPrimitives.h"
#include "nsObjCExceptions.h"
#include "nsIFile.h"
#include "nsDirectoryServiceDefs.h"
#include "nsICommandLineRunner.h"

class AutoAutoreleasePool {
public:
  AutoAutoreleasePool()
  {
    mLocalPool = [[NSAutoreleasePool alloc] init];
  }
  ~AutoAutoreleasePool()
  {
    [mLocalPool release];
  }
private:
  NSAutoreleasePool *mLocalPool;
};

@interface MacApplicationDelegate : NSObject
{
}

@end

// Something to call from non-objective code.

// This is needed, on relaunch, to force the OS to use the "Cocoa Dock API"
// instead of the "Carbon Dock API".  For more info see bmo bug 377166.
void
EnsureUseCocoaDockAPI()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  [NSApplication sharedApplication];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void
SetupMacApplicationDelegate()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  // this is called during startup, outside an event loop, and therefore
  // needs an autorelease pool to avoid cocoa object leakage (bug 559075)
  AutoAutoreleasePool pool;

  // This call makes it so that application:openFile: doesn't get bogus calls
  // from Cocoa doing its own parsing of the argument string. And yes, we need
  // to use a string with a boolean value in it. That's just how it works.
  [[NSUserDefaults standardUserDefaults] setObject:@"NO"
                                            forKey:@"NSTreatUnknownArgumentsAsOpen"];

  // Create the delegate. This should be around for the lifetime of the app.
  MacApplicationDelegate *delegate = [[MacApplicationDelegate alloc] init];
  [[NSApplication sharedApplication] setDelegate:delegate];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

@implementation MacApplicationDelegate

- (id)init
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  if ((self = [super init])) {
    NSAppleEventManager *aeMgr = [NSAppleEventManager sharedAppleEventManager];

    [aeMgr setEventHandler:self
               andSelector:@selector(handleAppleEvent:withReplyEvent:)
             forEventClass:kInternetEventClass
                andEventID:kAEGetURL];

    [aeMgr setEventHandler:self
               andSelector:@selector(handleAppleEvent:withReplyEvent:)
             forEventClass:'WWW!'
                andEventID:'OURL'];

    [aeMgr setEventHandler:self
               andSelector:@selector(handleAppleEvent:withReplyEvent:)
             forEventClass:kCoreEventClass
                andEventID:kAEOpenDocuments];
  }
  return self;

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(nil);
}

- (void)dealloc
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NSAppleEventManager *aeMgr = [NSAppleEventManager sharedAppleEventManager];
  [aeMgr removeEventHandlerForEventClass:kInternetEventClass andEventID:kAEGetURL];
  [aeMgr removeEventHandlerForEventClass:'WWW!' andEventID:'OURL'];
  [aeMgr removeEventHandlerForEventClass:kCoreEventClass andEventID:kAEOpenDocuments];
  [super dealloc];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

// Opening the application is handled specially elsewhere,
// don't define applicationOpenUntitledFile: .

// The method that NSApplication calls upon a request to reopen, such as when
// the Dock icon is clicked and no windows are open.

// A "visible" window may be miniaturized, so we can't skip
// nsCocoaNativeReOpen() if 'flag' is 'true'.
- (BOOL)applicationShouldHandleReopen:(NSApplication*)theApp hasVisibleWindows:(BOOL)flag
{
  nsCOMPtr<nsINativeAppSupport> nas = do_CreateInstance(NS_NATIVEAPPSUPPORT_CONTRACTID);
  NS_ENSURE_TRUE(nas, NO);

  // Go to the common Carbon/Cocoa reopen method.
  nsresult rv = nas->ReOpen();
  NS_ENSURE_SUCCESS(rv, NO);

  // NO says we don't want NSApplication to do anything else for us.
  return NO;
}

// The method that NSApplication calls when documents are requested to be opened.
// It will be called once for each selected document.

- (BOOL)application:(NSApplication*)theApplication openFile:(NSString*)filename
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  NSURL *url = [NSURL fileURLWithPath:filename];
  if (!url)
    return NO;

  NSString *urlString = [url absoluteString];
  if (!urlString)
    return NO;

  nsMacCommandLine& cmdLine = nsMacCommandLine::GetMacCommandLine();

  // Add the URL to any command line we're currently setting up.
  if (cmdLine.AddURLToCurrentCommandLine([urlString UTF8String]))
    return YES;

  // We don't actually care about Mac filetypes in this context, just pass a placeholder.
  
  cmdLine.HandleOpenOneDoc((CFURLRef)url, 'abcd');

  return YES;

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NO);
}

// The method that NSApplication calls when documents are requested to be printed
// from the Finder (under the "File" menu).
// It will be called once for each selected document.

- (BOOL)application:(NSApplication*)theApplication printFile:(NSString*)filename
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  // Take advantage of the existing "command line" code for Macs.
  nsMacCommandLine& cmdLine = nsMacCommandLine::GetMacCommandLine();
  // We don't actually care about Mac filetypes in this context, just pass a placeholder.
  cmdLine.HandlePrintOneDoc((CFURLRef)[NSURL URLWithString:filename], 'abcd');

  return YES;

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NO);
}

// Drill down from nsIXULWindow and get an NSWindow. We get passed nsISupports
// because that's what nsISimpleEnumerator returns.

static NSWindow* GetCocoaWindowForXULWindow(nsISupports *aXULWindow)
{
  nsresult rv;
  nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(aXULWindow, &rv);
  NS_ENSURE_SUCCESS(rv, nil);
  nsCOMPtr<nsIWidget> widget;
  rv = baseWindow->GetMainWidget(getter_AddRefs(widget));
  NS_ENSURE_SUCCESS(rv, nil);
  // If it fails, we return nil anyway, no biggie
  return (NSWindow *)widget->GetNativeData(NS_NATIVE_WINDOW);
}

// Create the menu that shows up in the Dock.

- (NSMenu*)applicationDockMenu:(NSApplication*)sender
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  // Why we're not just using Cocoa to enumerate our windows:
  // The Dock thinks we're a Carbon app, probably because we don't have a
  // blessed Window menu, so we get none of the automatic handling for dock
  // menus that Cocoa apps get. Add in Cocoa being a bit braindead when you hide
  // the app, and we end up having to get our list of windows via XPCOM. Ugh.

  // Get the window mediator to do all our lookups.
  nsresult rv;
  nsCOMPtr<nsIWindowMediator> wm = do_GetService(NS_WINDOWMEDIATOR_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, nil);

  // Get the frontmost window
  nsCOMPtr<nsISimpleEnumerator> orderedWindowList;
  rv = wm->GetZOrderXULWindowEnumerator(nsnull, PR_TRUE,
                                        getter_AddRefs(orderedWindowList));
  NS_ENSURE_SUCCESS(rv, nil);
  PRBool anyWindows = false;
  rv = orderedWindowList->HasMoreElements(&anyWindows);
  NS_ENSURE_SUCCESS(rv, nil);
  nsCOMPtr<nsISupports> frontWindow;
  rv = orderedWindowList->GetNext(getter_AddRefs(frontWindow));
  NS_ENSURE_SUCCESS(rv, nil);

  // Get our list of windows and prepare to iterate. We use this list, ordered
  // by window creation date, instead of the z-ordered list because that's what
  // native apps do.
  nsCOMPtr<nsISimpleEnumerator> windowList;
  rv = wm->GetXULWindowEnumerator(nsnull, getter_AddRefs(windowList));
  NS_ENSURE_SUCCESS(rv, nil);

  // Iterate through our list of windows to create our menu
  NSMenu *menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
  PRBool more;
  while (NS_SUCCEEDED(windowList->HasMoreElements(&more)) && more) {
    // Get our native window
    nsCOMPtr<nsISupports> xulWindow;
    rv = windowList->GetNext(getter_AddRefs(xulWindow));
    NS_ENSURE_SUCCESS(rv, nil);
    NSWindow *cocoaWindow = GetCocoaWindowForXULWindow(xulWindow);
    if (!cocoaWindow) continue;
    
    NSString *windowTitle = [cocoaWindow title];
    if (!windowTitle) continue;
    
    // Now, create a menu item, and add it to the menu
    NSMenuItem *menuItem = [[NSMenuItem alloc]
                              initWithTitle:windowTitle
                                     action:@selector(dockMenuItemSelected:)
                              keyEquivalent:@""];
    [menuItem setTarget:self];
    [menuItem setRepresentedObject:cocoaWindow];

    // If this is the foreground window, put a checkmark next to it
    if (SameCOMIdentity(xulWindow, frontWindow))
      [menuItem setState:NSOnState];

    [menu addItem:menuItem];
    [menuItem release];
  }
  return menu;

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

// One of our dock menu items was selected
- (void)dockMenuItemSelected:(id)sender
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  // Our represented object is an NSWindow
  [[sender representedObject] makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

// If we don't handle applicationShouldTerminate:, a call to [NSApp terminate:]
// (from the browser or from the OS) can result in an unclean shutdown.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
  nsCOMPtr<nsIObserverService> obsServ =
           do_GetService("@mozilla.org/observer-service;1");
  if (!obsServ)
    return NSTerminateNow;

  nsCOMPtr<nsISupportsPRBool> cancelQuit =
           do_CreateInstance(NS_SUPPORTS_PRBOOL_CONTRACTID);
  if (!cancelQuit)
    return NSTerminateNow;

  cancelQuit->SetData(PR_FALSE);
  obsServ->NotifyObservers(cancelQuit, "quit-application-requested", nsnull);

  PRBool abortQuit;
  cancelQuit->GetData(&abortQuit);
  if (abortQuit)
    return NSTerminateCancel;

  nsCOMPtr<nsIAppStartup> appService =
           do_GetService("@mozilla.org/toolkit/app-startup;1");
  if (appService)
    appService->Quit(nsIAppStartup::eForceQuit);

  return NSTerminateNow;
}

- (void)handleAppleEvent:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)replyEvent
{
  if (!event)
    return;

  AutoAutoreleasePool pool;

  if (([event eventClass] == kInternetEventClass && [event eventID] == kAEGetURL) ||
      ([event eventClass] == 'WWW!' && [event eventID] == 'OURL')) {
    NSString* urlString = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];

    // don't open chrome URLs
    NSString* schemeString = [[NSURL URLWithString:urlString] scheme];
    if (!schemeString ||
        [schemeString compare:@"chrome"
                      options:NSCaseInsensitiveSearch
                        range:NSMakeRange(0, [schemeString length])] == NSOrderedSame) {
      return;
    }

    // Add the URL to any command line we're currently setting up.
    nsMacCommandLine& macCmdLine = nsMacCommandLine::GetMacCommandLine();
    if (macCmdLine.AddURLToCurrentCommandLine([urlString UTF8String]))
      return;

    nsCOMPtr<nsICommandLineRunner> cmdLine(do_CreateInstance("@mozilla.org/toolkit/command-line;1"));
    if (!cmdLine) {
      NS_ERROR("Couldn't create command line!");
      return;
    }
    nsCOMPtr<nsIFile> workingDir;
    nsresult rv = NS_GetSpecialDirectory(NS_OS_CURRENT_WORKING_DIR, getter_AddRefs(workingDir));
    if (NS_FAILED(rv))
      return;
    const char *argv[3] = {nsnull, "-url", [urlString UTF8String]};
    rv = cmdLine->Init(3, const_cast<char**>(argv), workingDir, nsICommandLine::STATE_REMOTE_EXPLICIT);
    if (NS_FAILED(rv))
      return;
    rv = cmdLine->Run();
  }
  else if ([event eventClass] == kCoreEventClass && [event eventID] == kAEOpenDocuments) {
    NSAppleEventDescriptor* fileListDescriptor = [event paramDescriptorForKeyword:keyDirectObject];
    if (!fileListDescriptor)
      return;

    // Descriptor list indexing is one-based...
    int numberOfFiles = [fileListDescriptor numberOfItems];
    for (int i = 1; i <= numberOfFiles; i++) {
      NSString* urlString = [[fileListDescriptor descriptorAtIndex:i] stringValue];
      if (!urlString)
        continue;

      // We need a path, not a URL
      NSURL* url = [NSURL URLWithString:urlString];
      if (!url)
        continue;

      [self application:NSApp openFile:[url path]];
    }
  }
}

@end
