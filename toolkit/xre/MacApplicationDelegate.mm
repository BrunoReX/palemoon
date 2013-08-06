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
 *   Thomas K. Dyas <tom.dyas@gmail.com>
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
#include "nsINativeAppSupport.h"
#include "nsAppRunner.h"
#include "nsComponentManagerUtils.h"
#include "nsIServiceManager.h"
#include "nsServiceManagerUtils.h"
#include "nsIAppStartup.h"
#include "nsIObserverService.h"
#include "nsISupportsPrimitives.h"
#include "nsObjCExceptions.h"
#include "nsIFile.h"
#include "nsDirectoryServiceDefs.h"
#include "nsICommandLineRunner.h"
#include "nsIMacDockSupport.h"
#include "nsIStandaloneNativeMenu.h"
#include "nsILocalFileMac.h"
#include "nsString.h"
#include "nsCommandLineServiceMac.h"

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

static PRBool sProcessedGetURLEvent = PR_FALSE;

@class GeckoNSApplication;

// Methods that can be called from non-Objective-C code.

// This is needed, on relaunch, to force the OS to use the "Cocoa Dock API"
// instead of the "Carbon Dock API".  For more info see bmo bug 377166.
void
EnsureUseCocoaDockAPI()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  [GeckoNSApplication sharedApplication];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void
SetupMacApplicationDelegate()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  // this is called during startup, outside an event loop, and therefore
  // needs an autorelease pool to avoid cocoa object leakage (bug 559075)
  AutoAutoreleasePool pool;

  // Ensure that ProcessPendingGetURLAppleEvents() doesn't regress bug 377166.
  [GeckoNSApplication sharedApplication];

  // This call makes it so that application:openFile: doesn't get bogus calls
  // from Cocoa doing its own parsing of the argument string. And yes, we need
  // to use a string with a boolean value in it. That's just how it works.
  [[NSUserDefaults standardUserDefaults] setObject:@"NO"
                                            forKey:@"NSTreatUnknownArgumentsAsOpen"];

  // Create the delegate. This should be around for the lifetime of the app.
  MacApplicationDelegate *delegate = [[MacApplicationDelegate alloc] init];
  [NSApp setDelegate:delegate];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

// Indirectly make the OS process any pending GetURL Apple events.  This is
// done via _DPSNextEvent() (an undocumented AppKit function called from
// [NSApplication nextEventMatchingMask:untilDate:inMode:dequeue:]).  Apple
// events are only processed if 'dequeue' is 'YES' -- so we need to call
// [NSApplication sendEvent:] on any event that gets returned.  'event' will
// never itself be an Apple event, and it may be 'nil' even when Apple events
// are processed.
void
ProcessPendingGetURLAppleEvents()
{
  AutoAutoreleasePool pool;
  PRBool keepSpinning = PR_TRUE;
  while (keepSpinning) {
    sProcessedGetURLEvent = PR_FALSE;
    NSEvent *event = [NSApp nextEventMatchingMask:NSAnyEventMask
                                        untilDate:nil
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    if (event)
      [NSApp sendEvent:event];
    keepSpinning = sProcessedGetURLEvent;
  }
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

    if (![NSApp windowsMenu]) {
      // If the application has a windows menu, it will keep it up to date and
      // prepend the window list to the Dock menu automatically.
      NSMenu* windowsMenu = [[NSMenu alloc] initWithTitle:@"Window"];
      [NSApp setWindowsMenu:windowsMenu];
      [windowsMenu release];
    }
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

// The method that NSApplication calls upon a request to reopen, such as when
// the Dock icon is clicked and no windows are open. A "visible" window may be
// miniaturized, so we can't skip nsCocoaNativeReOpen() if 'flag' is 'true'.
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

  // Add the URL to any command line we're currently setting up.
  if (CommandLineServiceMac::AddURLToCurrentCommandLine([urlString UTF8String]))
    return YES;

  nsCOMPtr<nsILocalFileMac> inFile;
  nsresult rv = NS_NewLocalFileWithCFURL((CFURLRef)url, PR_TRUE, getter_AddRefs(inFile));
  if (NS_FAILED(rv))
    return NO;

  nsCOMPtr<nsICommandLineRunner> cmdLine(do_CreateInstance("@mozilla.org/toolkit/command-line;1"));
  if (!cmdLine) {
    NS_ERROR("Couldn't create command line!");
    return NO;
  }

  nsCString filePath;
  rv = inFile->GetNativePath(filePath);
  if (NS_FAILED(rv))
    return NO;

  nsCOMPtr<nsIFile> workingDir;
  rv = NS_GetSpecialDirectory(NS_OS_CURRENT_WORKING_DIR, getter_AddRefs(workingDir));
  if (NS_FAILED(rv))
    return NO;

  const char *argv[3] = {nsnull, "-file", filePath.get()};
  rv = cmdLine->Init(3, const_cast<char**>(argv), workingDir, nsICommandLine::STATE_REMOTE_EXPLICIT);
  if (NS_FAILED(rv))
    return NO;

  if (NS_SUCCEEDED(cmdLine->Run()))
    return YES;

  return NO;

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NO);
}

// The method that NSApplication calls when documents are requested to be printed
// from the Finder (under the "File" menu).
// It will be called once for each selected document.
- (BOOL)application:(NSApplication*)theApplication printFile:(NSString*)filename
{
  return NO;
}

// Create the menu that shows up in the Dock.
- (NSMenu*)applicationDockMenu:(NSApplication*)sender
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  // Create the NSMenu that will contain the dock menu items.
  NSMenu *menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
  [menu setAutoenablesItems:NO];

  // Add application-specific dock menu items. On error, do not insert the
  // dock menu items.
  nsresult rv;
  nsCOMPtr<nsIMacDockSupport> dockSupport = do_GetService("@mozilla.org/widget/macdocksupport;1", &rv);
  if (NS_FAILED(rv) || !dockSupport)
    return menu;

  nsCOMPtr<nsIStandaloneNativeMenu> dockMenu;
  rv = dockSupport->GetDockMenu(getter_AddRefs(dockMenu));
  if (NS_FAILED(rv) || !dockMenu)
    return menu;

  // Determine if the dock menu items should be displayed. This also gives
  // the menu the opportunity to update itself before display.
  PRBool shouldShowItems;
  rv = dockMenu->MenuWillOpen(&shouldShowItems);
  if (NS_FAILED(rv) || !shouldShowItems)
    return menu;

  // Obtain a copy of the native menu.
  NSMenu * nativeDockMenu;
  rv = dockMenu->GetNativeMenu(reinterpret_cast<void **>(&nativeDockMenu));
  if (NS_FAILED(rv) || !nativeDockMenu)
    return menu;

  // Loop through the application-specific dock menu and insert its
  // contents into the dock menu that we are building for Cocoa.
  int numDockMenuItems = [nativeDockMenu numberOfItems];
  if (numDockMenuItems > 0) {
    if ([menu numberOfItems] > 0)
      [menu addItem:[NSMenuItem separatorItem]];

    for (int i = 0; i < numDockMenuItems; i++) {
      NSMenuItem * itemCopy = [[nativeDockMenu itemAtIndex:i] copy];
      [menu addItem:itemCopy];
      [itemCopy release];
    }
  }

  return menu;

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
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

  PRBool isGetURLEvent =
    ([event eventClass] == kInternetEventClass && [event eventID] == kAEGetURL);
  if (isGetURLEvent)
    sProcessedGetURLEvent = PR_TRUE;

  if (isGetURLEvent ||
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
    if (CommandLineServiceMac::AddURLToCurrentCommandLine([urlString UTF8String]))
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
    NSInteger numberOfFiles = [fileListDescriptor numberOfItems];
    for (NSInteger i = 1; i <= numberOfFiles; i++) {
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
