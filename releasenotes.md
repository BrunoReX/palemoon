Pale Moon: Release notes
========================

### 24.5.0 (2014-04-25)

This is a security and bugfix release, to address outstanding known issues and streamline browser identity.
 Fixes/changes:

-   Fix plugin doorhanger code for removed-node confusion.
-   Remove Mozilla Corp specific details from search plugins, to clearly indicate the client is Pale Moon and to make sure searches are never counted towards other browser's searches by mistake by search providers.
-   Make sure to set both "warnOnClose" and "warnOnCloseOther" prefs to false when users choose to disable this check in the popup prompt.
-   Update branding: Remove nightly branding altogether - only have unofficial+official,  and fix the broken About dialog branding.
-   Bugfix: Clamp level of WebGL TexImage operations to 32-bits to avoid issues on x64 architectures.
-   Update Linux theme: feed icon
-   Bugfix: Add Firefox Compatibility flag to unofficial branding.
-   Workaround for several prominent websites complaining about an "outdated browser".

Security fixes:

-   [bug \#987003](https://bugzilla.mozilla.org/show_bug.cgi?id=987003) - Be more careful sandboxing javascript: URLs.
-   [bug \#952022](https://bugzilla.mozilla.org/show_bug.cgi?id=952022) - Add missing detachAsmJSModule.
-   [bug \#986843](https://bugzilla.mozilla.org/show_bug.cgi?id=986843) - Replace AutoHoldZone with AutoCompartmentRooter.
-   [bug \#989183](https://bugzilla.mozilla.org/show_bug.cgi?id=989183) - Check for nsXBLJSClass.
-   [bug \#980537](https://bugzilla.mozilla.org/show_bug.cgi?id=980537) - Only store FakeBackstagePass instances in mThisObjects.
-   [bug \#986678](https://bugzilla.mozilla.org/show_bug.cgi?id=986678) - Fix type check in TryAddTypeBarrierForWrite.
-   [bug \#966006](https://bugzilla.mozilla.org/show_bug.cgi?id=966006) - Fix security issue in DNS resolver.
-   [bug \#944353](https://bugzilla.mozilla.org/show_bug.cgi?id=944353) - Avoid spurious decoding of corrupt images.
-   [bug \#969226](https://bugzilla.mozilla.org/show_bug.cgi?id=969226) - Avoid buffer overflow in corrupt ICC profiles.
-   [bug \#991471](https://bugzilla.mozilla.org/show_bug.cgi?id=991471) - Fix offset when setting host on URL.
-   [bug \#993546](https://bugzilla.mozilla.org/show_bug.cgi?id=993546) - Don't try to malloc-free 0-size memory chunks.
-   [bug \#992968](https://bugzilla.mozilla.org/show_bug.cgi?id=992968) - Avoid OOM problems with JIT code caching

### 24.4.2 (2014-04-02)

A small bugfix release, and implementing [OCSP-stapling](https://en.wikipedia.org/wiki/OCSP_Stapling) for SSL connections.
 Fixes/changes:

-   Added OCSP-stapling.
-   Removed download status indicator from default set in status bar code to fix erroneous pop-up locations of the downloads panel.
-   Fixed errors with synchronous OCSP-stapled calls.
-   Reduced the timeout for OCSP requests to 2 seconds unless OCSP is required by the server.
-   Added proper handling of fragment loading (Bug \#s [895557](https://bugzilla.mozilla.org/show_bug.cgi?id=895557)&[987140](https://bugzilla.mozilla.org/show_bug.cgi?id=987140)). security fix
-   Updated status bar localizations: kn-IN and pt-PT.

### 24.4.1 (2014-03-19)

A small security and bugfix release.
 Fixes/changes:

-   Bugfix: the new status bar code in 24.4.0 had a bug, preventing the downloads panel/window from opening when clicking on the download status indicator. There may have been a few other, similar small usability bugs in the same code that have now been fixed.
-   Feature update: Selecting "Warn me when closing multiple tabs" in the Options window will now apply both to closing a window and closing other tabs in the tab bar.
-   [Bug \#940714](https://bugzilla.mozilla.org/show_bug.cgi?id=940714) - Add an RAII class to make synchronous raster image decoding safer.
-   [Bug \#896268](https://bugzilla.mozilla.org/show_bug.cgi?id=896268) - Use a stateless approach to synchronous image decoding. security fix
-   [Bug \#982909](https://bugzilla.mozilla.org/show_bug.cgi?id=982909) - Consistently use inner window when calling OpenJS. security fix
-   [Bug \#982957](https://bugzilla.mozilla.org/show_bug.cgi?id=982957) - Fix crash if certain sweeps run out of memory. security fix
-   [Bug \#982906](https://bugzilla.mozilla.org/show_bug.cgi?id=982906) - Remove option for security bypass in URI building. security fix
-   [Bug \#983344](https://bugzilla.mozilla.org/show_bug.cgi?id=983344) - JavaScript: Simplify typed arrays and fix GC loops. security fix
-   [Bug \#982974](https://bugzilla.mozilla.org/show_bug.cgi?id=982974) - Be paranoid about neutering ArrayBuffer objects. security fix

### 24.4.0 (2014-03-10)

This update changes the new title behavior slightly and updates a lot of things under-the-hood.
 Fixes/changes:

-   By popular request: the new page title (when using the Pale Moon App button) will now follow the operating system default alignment (in most cases), meaning it will align left on Windows Vista and Windows 7 by default instead of center. If you want to hide the title or align it differently, please see the [FAQ section on the forum](http://forum.palemoon.org/viewtopic.php?f=24&t=3766).
-   Updated status bar code to the latest "non-australis" version and license change to MPL 2.0 to bring it in line with the rest of the browser code, making it an integral part of the source tree to streamline building (also for 3rd parties).
-   Changed the way Pale Moon handles file and protocol associations. This will prevent interoperability issues if you have both Firefox and Pale Moon installed on the same system. A side effect is that Pale Moon will ask you (once) to make it the default browser again when you install this update, because of the new associations to be made.
-   Changed the search default to DuckDuckGo.
-   Added DuckDuckGo logo to about:home.
-   Changed some things in the build system, back-end code and build configuration to improve overall performance of the browser.
-   Switched to the use of a more compact browser filesystem layout, improving overall start-up speed.
-   Precompiled script cache in the application, improving overall start-up speed at the expense of some disk space.
-   Added MPS detection for non-windows operating systems (NSPR fallback method) instead of always "1".

Bugfixes ported over:

-   [bug \#968461](https://bugzilla.mozilla.org/show_bug.cgi?id=968461) - Fix imgStatusTracker.h to build with gcc 4.4.
-   [bug \#912322](https://bugzilla.mozilla.org/show_bug.cgi?id=912322) - Make sure document.getAnonymous\* is no longer available to web content.
-   [bug \#894448](https://bugzilla.mozilla.org/show_bug.cgi?id=894448) - Move IsChromeOrXBL to xpcpublic.h.

Security fixes:

-   [bug \#963198](https://bugzilla.mozilla.org/show_bug.cgi?id=963198) - Don't mix up byte-size and array-length.
-   [bug \#966311](https://bugzilla.mozilla.org/show_bug.cgi?id=966311) - Calculate frame size for stereo wave.
-   [bug \#958867](https://bugzilla.mozilla.org/show_bug.cgi?id=958867) - Consistent OwningObject handling in IDBFactory::Create methods.
-   [bug \#925747](https://bugzilla.mozilla.org/show_bug.cgi?id=925747) - Patch file extraction cleanup.
-   [bug \#942152](https://bugzilla.mozilla.org/show_bug.cgi?id=942152) - Fix error handling on NSS I/O layer.
-   [bug \#960145](https://bugzilla.mozilla.org/show_bug.cgi?id=960145) - IonMonkey: Don't ignore OSR-like values when computing phi ranges.
-   [bug \#965982](https://bugzilla.mozilla.org/show_bug.cgi?id=965982) - Clean up client threads before I/O on shutdown.
-   [bug \#950604](https://bugzilla.mozilla.org/show_bug.cgi?id=950604) - Backport of a small typed array bugfix.
-   [bug \#967341](https://bugzilla.mozilla.org/show_bug.cgi?id=967341) - Fix up URI management.
-   [bug \#963974](https://bugzilla.mozilla.org/show_bug.cgi?id=963974) - Null mCurrentCompositeTask after calling Cancel() on it.

### 24.3.2 (2014-02-11)

An update to implement TLS v1.2, implement a few new features and fix some minor bugs.
 Fixes/changes:

-   New feature: Implemented the TLS v1.1 ([RFC 4346](http://www.ietf.org/rfc/rfc4346.txt)) and TLS v1.2 ([RFC 5246](http://www.ietf.org/rfc/rfc5246.txt)) protocols for improved https security.
-   Changed the list of supported encryption ciphers and order of preference to provide you with secure, speedy connections wherever possible.
-   New feature: Added CSS background-attachment:local
-   New feature: Added dashed-line stroke support for canvas drawing (set/get/offset)
-   Adjusted geolocation timings to prevent IP bans in testing mode and to give you a slightly faster response to the request.
-   Adjusted the new window title position some more to account for edge cases.
-   Fixed the installer to use the proper class for checking if Pale Moon is already installed/running.
-   Security fix: [bug \#966021](https://bugzilla.mozilla.org/show_bug.cgi?id=966021) - Fix load\_truetype\_table in the cairo dwrite font backend.

### 24.3.1 (2014-01-31)

A minor bugfix release to address some issues with new code in 24.3.
 Fixes/changes:

-   Fine-tuned the title-bar title text position to work a little better on Windows 8 systems.
-   Fixed a problem with the classic download manager window not opening and/or downloads not starting when using the classic download manager.
-   Security fix: [Bug 945334](https://bugzilla.mozilla.org/show_bug.cgi?id=945334) - Fix runnable pointer holding.
-   Merged Linux-specific theme code into the source tree for the pm4linux project.

### 24.3.0 (2014-01-28)

A fairly significant update with feature updates, bugfixes, and security fixes.
 Changes and bugfixes:

-   New build: Atom-optimized Pale Moon
     After some thorough testing, the Atom/netbook builds are being released as final. These builds are specifically made for PCs with Intel Atom processors. More information can be found on the [Atom builds](http://www.palemoon.org/palemoon-atom.shtml) page.
-   New feature: the title has been brought back to the title bar
    When using the Application Menu (Pale Moon button), the title bar of the browser window would be blank. Considering this is wasted space, the page title will now be displayed in the title bar again (it's called a title bar for a reason, after all!). Several different styles have been implemented to cater to different OS version layouts.
-   Removal of the services tab in the Add-on Manager
     It will be visible only if someone actually has a service extension installed (similar to how language packs work)
-   Improvement of UI consistency
     Removal of illogical selective hiding of the navigation bar and toolbars when in tabs-on-top mode (Add-ons manager, permissions manager, etc.). Browser chrome will now never be hidden.
-   Bugfix: When using the classic downloads window, downloads in private windows were not shown
     If you use the classic downloads window and would open a Private Browsing (PB) window, there was no easy way to see which downloads were done in the PB window. When checking the downloads, it would open up the (non-PB) classic downloads window which does not have downloads listed from the PB session. This has been fixed, and PB windows will now open a new tab in the PB window with the downloads from that private session.
-   Bugfix: Geolocation didn't work in Pale Moon
     This was caused by the Firefox standard geolocation provider (Google Inc.) now requiring an API key to request geolocation coordinates. Only official Mozilla Firefox builds will have working geolocation from Google.
     Pale Moon has switched provider to IP-API.com to address this issue, with the required re-write of code for the different type of request. More information [on the forum](http://forum.palemoon.org/viewtopic.php?f=24&t=3658).
-   Bugfix: The "More information" link for blocked add-ons didn't work
-   Bugfix: Certain scaled fonts would have malformed letters
     On Vista and later with hardware acceleration enabled, certain letters of some font families would become malformed and difficult to read because of a Direct2D scaling issue. These fonts should now render sharp and more legibly.
-   Romanian has been added to the status bar localizations

Bugfixes ported over:

-   [Bug 903274](https://bugzilla.mozilla.org/show_bug.cgi?id=903274 "RESOLVED - this.updateDisplay is not a function at chrome://browser/content/search/search.xml:79 from every window during tpaint and tspaint") - Have the search bar binding's initialization callback bail out if the binding is destroyed.
-   [Bug 951142](https://bugzilla.mozilla.org/show_bug.cgi?id=951142 "RESOLVED - Intermittent browser_browserDrop.js | uncaught exception - TypeError: this.close is not a function at chrome://global/content/bindings/findbar.xml:1168 | Test timed out") - Check for a close method to be present on the binding before invoking it.
-   [Bug 908915](https://bugzilla.mozilla.org/show_bug.cgi?id=908915 "RESOLVED - Crash [@ js::CompartmentChecker::fail] or compartment mismatch") - Fix compartment mismatch in shell decompileThis and disassemble functions.
-   [Bug 950909](https://bugzilla.mozilla.org/show_bug.cgi?id=950909 "RESOLVED - Native aggregation is silently dropped when an XPCWrappedJS already exists for the target JSObject") - Forward native aggregation to the root XPCWrappedJS.
-   [Bug 937152](https://bugzilla.mozilla.org/show_bug.cgi?id=937152 "RESOLVED - XPCWrappedJS::mMainThread and mMainThreadOnly are always true") - Remove XPCWrappedJS::mMainThread and mMainThreadOnly.
-   [Bug 948134](https://bugzilla.mozilla.org/show_bug.cgi?id=948134 "RESOLVED - Error: "value is not defined" in PlacesDBUtils.jsm") - Fix "value is not defined" in PlacesDBUtils.jsm.
-   [Bug 822425](https://bugzilla.mozilla.org/show_bug.cgi?id=822425 "RESOLVED - Expose assertSameCompartment outside the JS engine") - Expose a few simple compartment assertions in jsfriendapi.
-   [Bug 932906](https://bugzilla.mozilla.org/show_bug.cgi?id=932906 "RESOLVED - Observers no longer called when loading overlay") - Observer no longer called when using overlay

Security fixes:

-   Update of the NSS library to 3.15.4 RTM
-   [Bug 936808](https://bugzilla.mozilla.org/show_bug.cgi?id=936808 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Serialize calls to PK11 routines in SSLServerCertVerification.
-   [Bug 945939](https://bugzilla.mozilla.org/show_bug.cgi?id=945939 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Use the pre-split value when numbering values.
-   [Bug 911864](https://bugzilla.mozilla.org/show_bug.cgi?id=911864 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Fix several XBL issues
-   [Bug 921470](https://bugzilla.mozilla.org/show_bug.cgi?id=921470 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Remove hasFallbackStub\_ check in resetMonitorStubChain.
-   [Bug 950590](https://bugzilla.mozilla.org/show_bug.cgi?id=950590 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Use nsRefPtr for gfxFontGroup's reference to the user font set, and support changing it from canvas context.
-   [Bug 937697](https://bugzilla.mozilla.org/show_bug.cgi?id=937697 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Simplify some BoundsCheckRange code.
-   [Bug 936056](https://bugzilla.mozilla.org/show_bug.cgi?id=936056 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Be consistent about the thisobj we pass to getters.
-   [Bug 953114](https://bugzilla.mozilla.org/show_bug.cgi?id=953114 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Fix GetElementIC typed array issue.
-   [Bug 937132](https://bugzilla.mozilla.org/show_bug.cgi?id=937132 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - SpiderMonkey: Check for overflows in LifoAlloc.
-   [Bug 932162](https://bugzilla.mozilla.org/show_bug.cgi?id=932162 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Dispatch IndexedDB FileInfo releases to the main thread.
-   [Bug 951366](https://bugzilla.mozilla.org/show_bug.cgi?id=951366) - Use AutoDetectInvalidation for disabled GetElement caches.
-   [Bug 950438](https://bugzilla.mozilla.org/show_bug.cgi?id=950438 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - IonMonkey: The intersection of two ranges that both contain NaN is not empty.
-   [Bug 950268](https://bugzilla.mozilla.org/show_bug.cgi?id=950268) - Fix leak in nsCertTree::GetDispInfoAtIndex.
-   [Bug 932906](https://bugzilla.mozilla.org/show_bug.cgi?id=932906) - Exempt Remote XUL from CanCreateWrapper checks.
-   [Bug 882933](https://bugzilla.mozilla.org/show_bug.cgi?id=882933 "RESOLVED - Assertion failure: script->treatAsRunOnce, at vm/Interpreter.cpp") - Copy treatAsRunOnce bit when cloning scripts, don't clone scripts unnecessarily for arrow lambdas.
-   [Bug 901348](https://bugzilla.mozilla.org/show_bug.cgi?id=901348) - Duplicate symbol errors building --with-intl-api.
-   [Bug 925896](https://bugzilla.mozilla.org/show_bug.cgi?id=925896 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Properly reference session data
-   [Bug 943803](https://bugzilla.mozilla.org/show_bug.cgi?id=943803 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Use monitor instead of mutex locking for raster images
-   [Bug 942164](https://bugzilla.mozilla.org/show_bug.cgi?id=942164) - Use weak references to imgRequest consumers
-   [Bug 947592](https://bugzilla.mozilla.org/show_bug.cgi?id=947592 "UNKNOWN - The bug is inaccessible or doesn’t exist.") - Streamline ReportLoadError.

### 24.2.2 (2013-12-11)

Mainly a security update:

-   Implementation of all remaining applicable security fixes from Firefox 26.0 that were not implemented yet in previous versions.
-   Update of the Security library (NSS) to 3.15.3.1.
-   Fix of new js zone writes/zone barrier bugs.
-   The Sync configuration allows users to input their own recovery key again. Please note that letting the browser generate its own secure recovery key is still strongly recommended, as this recovery key should be impossible to guess and of sufficient length and complexity to keep your data safely encrypted.

### 24.2.1 (2013-12-04)

A minor bugfix update:

-   Fix for some status bar localizations not working and giving an error.
-   Implementation of an optimized QuickFind routine.
-   Implementation of per-zone user data handling.
-   Security fix in the JPEG library.
-   Security fix in web workers.

### 24.2.0 (2013-11-26)

This update implements the following changes:

-   Update of the new-tab routine: When opening a new tab, focus will now only be on the address bar if you open a blank tab or the Quick Dial page, and focus will be on the page content otherwise (Pale Moon start page or custom URL).
-   Compatibility issues between QuickFind/Find-as-you-Type and HTML5 input fields in forms fixed.
-   New advanced feature: Later versions of the Firefox code will automatically place the browser window fully on a visible portion of the screen. If you prefer having the browser window positioned partially off-screen and want to prevent this automatic resizing and repositioning when starting a new session, create a new boolean preference in about:config called browser.sessionstore.exactPos and set it to true.
-   Updated the localization of the status bar code with the following locales: en-GB, es-MX, es-AR, it, pl.
-   Fix for a security issue with script event handlers.

### 24.1.2 (2013-11-19)

This update implements the following changes:

-   Update of the NSPR library to 4.10.2 RTM.
-   Update of the Security library (NSS) to 3.15.3 (alternative branch) to pick up a number of fixes.
-   Fix (finally) of the menu list of tabs when browser.allTabs.previews is set to false. It would stick the top entry, not properly highlight the selected tab, and would generally be unpleasant and stubborn when tabs were moved or closed. This should all be corrected now.
-   Additional feature: Previously, tabs would immediately resize to fill the tab bar when you would close them. Mozilla changed this a (long) while back to cater to "rapidly closing multiple tabs without moving the mouse" and to resize you have to move the mouse out of the tab bar. A good number of Firefox/Pale Moon users don't like this behavior, but the fix to make this configurable was in the end rejected by the Mozilla UX team, so I opted for my own implementation in Pale Moon. New pref: browser.tabs.resize\_immediately - set this preference to true to immediately resize other tabs when closing a tab.
     Many thanks to David for doing the required research for this feature!
-   Rework of the multi-core routine and removal of OpenMP code and the related library (Microsoft's implementation is old, limited, and won't be updated/improved; in addition it prevented some compiler optimizations that could now be used again).
-   The accessibility back-end for automatically starting "Find as you type" (an accessibility feature) has been disabled completely to prevent this setting from breaking websites with HTML5 input fields (not compatible with autostarting FAYT).

### 24.1.1 (2013-11-05)

This is a minor update to 24.1.0 to revert the change of disabling 2 specific SSL ciphers by default, since it broke more web sites than anticipated (including external elements pulled in from third-party sites using SSL). No other changes were made.

### 24.1.0 (2013-11-04)

This update fixes a number of security and user interface issues, and adds the feed icon in the address bar.
 Fixes:

-   [MFSA 2013-102](https://www.mozilla.org/security/announce/2013/mfsa2013-102.html) Use-after-free in HTML document templates.
-   [MFSA 2013-101](https://www.mozilla.org/security/announce/2013/mfsa2013-101.html) Memory corruption in workers.
-   [MFSA 2013-100](https://www.mozilla.org/security/announce/2013/mfsa2013-100.html) Miscellaneous use-after-free issues found through ASAN fuzzing.
-   [MFSA 2013-99](https://www.mozilla.org/security/announce/2013/mfsa2013-99.html) Security bypass of PDF.js checks using iframes.
-   [MFSA 2013-98](https://www.mozilla.org/security/announce/2013/mfsa2013-98.html) Use-after-free when updating offline cache.
-   [MFSA 2013-97](https://www.mozilla.org/security/announce/2013/mfsa2013-97.html) Writing to cycle collected object during image decoding.
-   [MFSA 2013-96](https://www.mozilla.org/security/announce/2013/mfsa2013-96.html) Improperly initialized memory and overflows in some JavaScript functions.
-   [MFSA 2013-95](https://www.mozilla.org/security/announce/2013/mfsa2013-95.html) Access violation with XSLT and uninitialized data.
-   [MFSA 2013-94](https://www.mozilla.org/security/announce/2013/mfsa2013-94.html) Spoofing addressbar though SELECT element.
-   [MFSA 2013-93](https://www.mozilla.org/security/announce/2013/mfsa2013-93.html) Miscellaneous memory safety hazards.
-   Security + cleanup fix: No longer store empty event handlers.
-   User interface: Fix for the classic downloads window having a blank title with no running downloads.
-   User interface: Fix of the drop-down menu "double entry" in the all-tabs list as-a-menu setup.

Changes:

-   Extensions are now set to automatically update by default. Because many users fail to do the occasional check to see if there are updates available to their extensions, the default is to automatically check and install available updates to extensions from this version forward to give the best possible browsing experience. If you prefer to check manually, make sure to change the setting accordingly in your add-on manager.
-   Two SSL ciphers that are considered weak are disabled by default (RSA-RC4-128-MD5 and RSA-RC4-128-SHA). If you are having trouble reaching certain encrypted sites that exclusively use these encryption methods, you should ask the site owners to update their SSL configuration to allow stronger encryption. As a workaround, you can enable the ciphers by installing the [Pale Moon Commander add-on](http://www.palemoon.org/commander.shtml) and changing the available ciphers there, or by setting security.ssl3.rsa\_rc4\_128\_md5 and security.ssl3.rsa\_rc4\_128\_sha to true in about:config
     (Note: this change was reverted in 24.1.1)

New Features:

-   When there is a [web feed](http://en.wikipedia.org/wiki/Web_feed) available on a website, Pale Moon will now display a feed indicator on the right side of the address bar to indicate that feeds are available. You can click this icon to subscribe to feeds.
     If you don't want this indicator, set browser.urlbar.rss to false in about:config
     Note: more technical information [on the forum](http://forum.palemoon.org/viewtopic.php?f=1&t=3333)!

### 24.0.2 (2013-09-27)

This is a small update to address a few issues with standalone images:

-   In some cases when having an image open, the User Interface would not properly redraw resulting in blank controls and tab headers.
-   In some cases, having an image open would cause 100% processor use on one core.
-   Drawing thumbnails of standalone images in the tab headers would often be slow and processor-intensive.

### 24.0.1 (2013-09-18)

This is a small update to address some small issues with the new major version:

-   Fix for unreadable address bar text when visiting a broken or mixed-mode SSL site.
-   Fix for an incorrect browser cache size default when first starting the browser. (regression)
     Note: If you have used version 24.0, then please check your Options -\> Advanced -\> Network tab, and if the cache size is set to "1024", change it back to its default "250" to prevent unnecessary use of disk space and potential slowing of the browser.
-   Fix for themes not applying to Private Browsing windows. (regression)
-   A small update to the new icon to fix some visual issues with it.
-   Reduction of visual friction and CPU usage on some operations by disabling smooth scrolling on it by default (e.g. Home/End keys).

### 24.0 (2013-09-13)

This is a new major release with many changes and improvements. A concise summary of the changes follows. There are too many changes and updates (both resulting from the code base update and from the additional Pale Moon development on top) to list all of them in detail.

Switch to a new Mozilla code base (Gecko 24.0).

Update of the Pale Moon icon/logo. Special thanks go to Roger Gómez del Casal for providing me with an interesting concept design image to use as a base for it!

Fixes for all relevant [security vulnerabilities.](https://www.mozilla.org/security/known-vulnerabilities/firefox.html)

Many changes and updates in the rendering, scripting and parsing back-end to provide significant improvements in overall browser performance (including benchmark scores).

Addition of a number of HTML5 elements, improving overall HTML5 standards compliance.

Implementation of the webaudio API (most features that are no longer draft).

Removal of Tab Groups (Panorama). If you actively used this functionality, I have also made an [add-on](http://storage.sity.nl/palemoon/support/palemoon-tabgroups-0.2.xpi) (Mozilla dev sourced) available to restore this feature to the browser. If you have issues with the supplied add-on or want to give other tab grouping methods a try, you can use alternatives found on [AMO](https://addons.mozilla.org/).

Removal of a few additional Accessibility options.

Inclusion of an updated version of the Add-on SDK and loader to solve recent issues with SDK/Jetpack add-ons.

Adjustment of the Quickdial "new tab" feature to have better layout.

Extension of the address bar shading functionality to more clearly indicate when there is a problem with a secure site (red shading on broken SSL/mixed content).

New way of handling plugins with control on a per-site basis. An extensive description can be found [on the forum](http://forum.palemoon.org/viewtopic.php?f=24&t=3031).

Restored/maintained a number of features that were removed from recent Firefox versions:

-   Graphical tab switching feature with quick search (Ctrl+Shift+Tab).
-   Removing the tab bar if there is only one tab present.
-   Options for the loading of images.
-   More recovery options in the Safe Mode startup dialog box than just nuking your profile.
-   Send Link/E-mail Link mail client integration functionality.

Unification of version numbers. x86 and x64 will from this point forward use the same version number (and icon) without an architecture designation. This will solve potential compatibility issues on new major versions, as well as the superfluous compatibility check when switching between x86 and x64 on the same profile.

Release notes for version 20
============================

### 20.3 (2013-08-13)

This update addresses performance/resource use and also tackles some security vulnerabilities.
 Changes:

-   A change to how tab histories are cached to improve the overall memory footprint and make browsing smoother, especially when using a large number of tabs with extensive active use.
-   A change to the networking pipelining back-end to use a more aggressive fallback if there are issues with pipelining requests, to minimize delays when loading pages and prevent time-outs.
-   Update of the compiler to Visual Studio 2012 Update 3, to fix a few compiler issues.
-   Removed the double entry for smooth scrolling selection in preferences (leaving just the one in the scrolling tab)

Fixes:

-   [(CVE-2013-1704)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1704) ASAN heap-use-after-free in nsINode::GetParentNode
-   [(CVE-2013-1708)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1708) Non-null crash at nsCString::CharAt
-   [(CVE-2013-1712)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1712) Code injection through internal updater
-   [(CVE-2013-1713)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1713) InstallTrigger can use the wrong principal when validating URI loads
-   [(CVE-2013-1714)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1714) Cross Domain Policy override using webworkers
-   Fix for Updater crash
-   Fix for XSS vulnerability/URI spoofing
-   Fix for newly allocated WebGL array buffers (prevent the use of uninitialized memory)
-   Several fixes for the SSL crypto library ([CVE-2013-1705](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1705) and others)
-   Fix for do\_QueryFrame support
-   x64: Fix for Yarr error
-   Update to the installer's 7zsfx module to prevent dll hijacking

Portable version:
 Important note for updating the portable version: it's recommended you make a fresh install of the portable and copy over your user profile to the new install with this update. Do not use the internal updater. Remember to check your user.ini if you have created one or changed .ini entries, and re-implement your changes in the 20.3 palemoon-portable.ini (don't just copy it over!)

-   Fix for safebrowsing
-   Enabled the XUL cache to improve startup times

### 20.2.1 (2013-07-08)

A small update to address issues with the new Aero glass theme, e.g. Tab Groups not showing.

### 20.2 (2013-07-01)

This is a maintenance update, focusing on visual improvements and security.
 Changes:

-   Implementation of some conservative additional multi-core support (mainly in graphics/media) using OpenMP. I'm taking baby steps here and will remain conservative in the use of multiple cores so stability of the browser isn't needlessly endangered.
-   Update of the navigation button icons (again). Users have clearly indicated that the inverted color icons on glass and dark themes were less desirable. I've listened, and changed the icons for glass back to the pre-20 style but with added contrast, and made a distinction for dark personas (themes) where the icons are now simply inverted white (like in Firefox).
-   Change for the color management system (CMS) so that Pale Moon now supports more types of embedded ICC profiles (including the already decade-old version 4 spec) and in the process fixing potential color issues on screens with images that embed such profiles.
-   Update of the browser padlock code. You can now choose both a "modern" look (as introduced in version 19) and a "classic" look (as introduced in version 15, when this padlock feature was first added). It also removes some phantom spacing in locations where the padlock isn't used (thanks for the pointer, Sowmoots!).

Fixes:

-   [(CVE-2013-1692)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1692) Fix for the inclusion of body data in an XMLHttpRequest HEAD request, making cross-site request forgery (CSRF) attacks via a crafted web site more difficult.
-   [(CVE-2013-1697)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1697) Fix to restrict use of DefaultValue for method calls, which allows remote attackers to execute arbitrary JavaScript code with chrome privileges.
-   [(CVE-2013-1694)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1694) Fix to properly handle the lack of a wrapper, which allows remote attackers to cause a denial of service (application crash) or possibly execute arbitrary code.
-   [(CVE-2013-1690)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1690) Crash fix ([MFSA 2013-53](http://www.mozilla.org/security/announce/2013/mfsa2013-53.html)) used to crash Tor users on certain Tor sites.
-   Fix to prevent arbitrary code execution from the profiler developer tool.
-   Fix for a crash when rapidly reloading pages.
-   Fix for cross-document selections.
-   Fixes for several crashes in JavaScript.
-   Fixes for several memory safety hazards and uncommon memory leaks.

### 20.1 (2013-05-23)

This is a security and stability update with a few additional changes and improvements.
 Changes:

-   Update of the libpixman graphics library to improve performance for SSE2 CPUs
-   Change to the "Clear download history" setting for use with the panel-based download manager (classic UI unaffected)

Fixes:

-   [(CVE-2013-1674)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1674) Fix for UAF with video and onresize event (crash fix)
-   [(CVE-2013-1675)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1675) Fix for parameters being used uninitialized
-   [(CVE-2013-1676)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1676) Fix for out-of-bounds read in SelectionIterator::GetNextSegment
-   [(CVE-2013-1679)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1679) Fix for heap use-after-free in mozilla::plugins::child::\_geturlnotify
-   [(CVE-2013-1680)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1680) Fix for heap-use-after-free in nsFrameList::FirstChild (crash fix)
-   [(CVE-2013-1681)](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-1681) Fix for heap-use-after-free in nsContentUtils::RemoveScriptBlocker (crash fix)
-   Fix for out-of-bounds read crash in PropertyProvider::GetSpacingInternal (crash fix)
-   Fix for out-of-bounds read in gfxSkipCharsIterator::SetOffsets
-   Fix for assertion failure in nsUnicharStreamLoader::WriteSegmentFun with ISO-2022-JP
-   Fix for crash with inline script in an XML doc (crash fix)
-   Fix for "ASSERTION: Out of flow frame doesn't have the expected parent" and crash  (crash fix)
-   Fix for nsScriptSecurityManager::CheckLoadURIWithPrincipal being broken
-   Fix for a problem where the IPC Channel could overwrite the stack
-   Fix for Crash in MediaDecoder::UpdatePlaybackOffset (crash fix)
-   Fix for Crash [@ nsTextFrame::HasTerminalNewline()] with splitText (crash fix)
-   Fix for FTP use-after-free crash  (crash fix)

More details can be found in the [release announcement post](http://forum.palemoon.org/viewtopic.php?f=1&t=2556) on the forum.

### 20.0.1 (2013-04-11)

This is mostly a bugfix, security and performance update, but with a number of new features as well, based on the Firefox 20 code base:
 New/updated/changed major features:

-   Per-window Private Browsing. [Learn more](https://support.mozilla.org/en-US/kb/private-browsing-browse-web-without-saving-info).
-   Panel-based download manager. See the detailed changelog for more information.
-   Ability to close hanging plugins, without the browser hanging.
-   Performance improvements related to common browser tasks.
-   Pale Moon specific Cairo performance fix for scaling/panning/zooming of HTML5 drawing surfaces.
-   Pale Moon specific fixes for performance of drawing elements (gradients, etc.).
-   HTML5 canvas now supports [blend modes.](https://hacks.mozilla.org/2012/12/firefox-development-highlights-per-window-private-browsing-canvas-globalcompositeoperation-new-values/)
-   Various HTML5 audio and video [improvements.](http://blog.pearce.org.nz/2012/12/html5-video-playbackrate-and-ogg.html)
-   Update of the Status Bar code to work with the new code base.
-   ECMAScript for XML (E4X) is kept available for add-ons. Note that this will be removed in future versions as E4X is obsolete.
-   Developer tools have been enabled by default, considering the code is practically impactless unless actually used.
-   Theming has been worked on to provide better contrast on glass/dark themes and to work around styling issues present in v19.
-   Updated fallback character sets to Windows-1252.
-   Restored legacy function key handling (uplifted from Firefox 22).
-   Fixed UNC path handling (Chemspill Firefox 20.0.1).
-   Always enable the use of personas, also in Private Browsing mode.
-   Experimental: support for H.264 videos (disabled by default)

Security fixes relevant to Pale Moon:

-   [MFSA 2013-30](https://www.mozilla.org/security/announce/2013/mfsa2013-30.html) A fairly large number of memory safety hazards (crashes/corruption/injection).
-   [MFSA 2013-31](https://www.mozilla.org/security/announce/2013/mfsa2013-31.html) Out-of-bounds write in Cairo library ([CVE-2013-0800](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-0800))
-   [MFSA 2013-34](https://www.mozilla.org/security/announce/2013/mfsa2013-34.html) Privilege escalation through Mozilla Updater ([CVE-2013-0797](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-0797))
-   [MFSA 2013-36](https://www.mozilla.org/security/announce/2013/mfsa2013-36.html) Bypass of SOW protections allows cloning of protected nodes ([CVE-2013-0795](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-0795))
-   [MFSA 2013-37](https://www.mozilla.org/security/announce/2013/mfsa2013-37.html) Bypass of tab-modal dialog origin disclosure (structural fix)
-   [MFSA 2013-38](https://www.mozilla.org/security/announce/2013/mfsa2013-38.html) Cross-site scripting (XSS) using timed history navigations ([CVE-2013-0793](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-0793))
-   [MFSA 2013-39](https://www.mozilla.org/security/announce/2013/mfsa2013-39.html) Memory corruption while rendering grayscale PNG images ([CVE-2013-0792](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-0792))
-   [MFSA 2013-40](https://www.mozilla.org/security/announce/2013/mfsa2013-40.html) Crash fix: Out-of-bounds array read in CERT\_DecodeCertPackage ([CVE-2013-0791](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2013-0791))

Detailed descriptions for these changes and fixes can be found in the [detailed changelog/announcement post](http://forum.palemoon.org/viewtopic.php?f=1&t=2392) on the forum.

* * * * *

### 20.0 -- Internal testing release, unpublished.

Release notes for version 19
============================

### 19.0.2 (2013-03-09)

A minor update prompted by a security update @Mozilla:

-   Fixes a critical security vulnerability in the browser ([MFSA 2013-29](https://www.mozilla.org/security/announce/2013/mfsa2013-29.html))
-   Slightly improves HTTP pipelining
-   Update to the integrated status bar feature (German localization updated)

### 19.0.1 (2013-02-24)

A minor update to address a few issues with the initial major release:

-   Fix for bookmarks giving an "XML parsing error" when set to "load in the sidebar"
-   Fix for a double padlock display if a secure site would not supply a favicon
-   Redone the mixed content https padlock image in 32bpp to prevent potential UI rendering issues
-   Fixed a setting so no unnecessary code walking is done for the otherwise disabled accessibility features
-   Fix (inherent) for add-ons and themes being marked as incompatible in Pale Moon x64 when they have a minimum version of 19.0

### 19.0 (2013-02-22)

This is a new major release with a lot of changes and improvements:
 Changes in this version:

Update of the underlying Firefox (gecko) code to v19. This has a number of consequences:

-   Add-ons and themes may need to be updated since the UI code has changed.
-   HTML5-implementation is more complete
-   A number of CSS statements have their prefix removed (-moz\*)
-   Javascript now uses the IonMonkey engine by default, which is a new (faster) engine
-   Improvements to the layout and rendering engines
-   If you are using a language pack, you need to update it to the new version

Update of the browser style. Main browser controls and the padlock look slightly different.

Several Pale Moon specific improvements to the rendering engine, noticeable in general use and certain benchmarks, to prevent browser stalls or high CPU usage on certain pages.

The builds no longer use PGO (Profile Guided Optimization) but are globally speed-optimized.

For details about each of these points, see the [detailed changelog post](http://forum.palemoon.org/viewtopic.php?f=1&t=2081) on the forum.

Release notes for version 15
============================

### 15.4.1 (2013-01-28)

This is a bugfix and security release. Changes in this version:

-   Updated the C runtime library included to a later version for security/stability purposes.
-   Updated the Windows SDK version to 8.0 for better Windows 8 compatibility and slight overall improvements.
-   Implemented a fix to prevent unwanted automatic opening of the plugin check window on startup on some systems.
-   Corrected the milestone marker from 15.4pre to 15.4.

In addition, this version was built on a fresh build environment to prevent possible code pollution and improve profiling efficiency.

### 15.4 (2013-01-16)

Several security and stability issues have been fixed in this update:

-   Deal with bogus Turktrust certs [MFSA 2013-20](https://www.mozilla.org/security/announce/2013/mfsa2013-20.html)
-   Several memory security hazards fixed [MFSA 2013-01](https://www.mozilla.org/security/announce/2013/mfsa2013-01.html)
-   Updated OTS library to r95 to fix potential font-related exploits
-   Security fix for libpixman stack buffer overflow
-   Fix for certain types of input lag on Twitter/Facebook & other sites with unnecessary DOM invalidations
-   Fix for HTTP pipelining re-use (improve pipelining logic)
-   Performance&stability updates to cairo and direct2d back-end
-   Improved performance for repeat gradients

### 15.3.2 (2012-12-05)

This update fixes an important issue in the JavaScript engine (MethodJIT) that would make particularly large/complex pieces of JavaScript (e.g. Mandreel) fail. (Thanks, Ryan, for catching this one!)

### 15.3.1 (2012-11-30)

This update is a bugfix and performance release with a number of security, stability and efficiency fixes:
 Bugfixes:

-   Fix for font rendering issues on Windows 8 (cairo+azure)
-   Status bar options: Russian locale fixed
-   Fix for status bar address bar linkover ghosting
-   Fix for browser hang in some WebM video content
-   Don't allow alert/confirm/prompt in onbeforeunload, onunload and onpagehide ([bug\# 391834](https://bugzilla.mozilla.org/show_bug.cgi?id=391834))

Improvements:

-   Reduce non-incremental GC occurrences (reduce lag in Javascript)
-   More efficient CPU usage for JS and Canvas
-   Pale Moon x64: Performance improvements

Security fixes:

-   Security fixes for CVE-2012-5840, CVE-2012-5839, CVE-2012-4210, CVE-2012-4207 and CVE-2012-4214.
-   Fix for methodjit assertion issue ([bug \#781859](https://bugzilla.mozilla.org/show_bug.cgi?id=781859))
-   Fix for potentially exploitable crash in XPConnect ([bug \#809674](https://bugzilla.mozilla.org/show_bug.cgi?id=809674))
-   Fix for potentially exploitable crash in layout engine ([bug \#791601](https://bugzilla.mozilla.org/show_bug.cgi?id=791601))
-   Fix for potentially exploitable crash in JS string handling ([bug \#778603](https://bugzilla.mozilla.org/show_bug.cgi?id=778603))
-   Fix for potentially exploitable crash in GIF decoder ([bug \#789046](https://bugzilla.mozilla.org/show_bug.cgi?id=789046))
-   Fix for potentially exploitable crash in image decoder ([bug \#802168](https://bugzilla.mozilla.org/show_bug.cgi?id=802168))
-   Fix for use-after-free in editor lib ([bug \#795708](https://bugzilla.mozilla.org/show_bug.cgi?id=795708))
-   Fix for potentially exploitable crash in SVG ([bug \#793848](https://bugzilla.mozilla.org/show_bug.cgi?id=793848))
-   Fix for out-of-bounds read when blurring ([bug \#783041](https://bugzilla.mozilla.org/show_bug.cgi?id=783041))
-   Fix for potentially exploitable crash in text editor ([bug \#798677](https://bugzilla.mozilla.org/show_bug.cgi?id=798677))
-   Prevent URL spoofing through prompts ([bug \#700080](https://bugzilla.mozilla.org/show_bug.cgi?id=700080))

### 15.3

This is an important update that sees a number of under-the-hood changes. The are no functional changes in this update.
 Changes:

The compiler has been changed to a newer version (Visual Studio 2012). This has a few consequences:

-   Better handling of the browser overall: things like smooth scrolling, the user interface and page loading should be noticeably smoother on the vast majority of computers.
-   Automatic vectorization: If your hardware supports it, more advanced processor instructions are used.
-   Support for multiple cores.
-   The 64-bit version of Pale Moon will no longer support Windows XP. More details in [this post](http://forum.palemoon.org/viewtopic.php?f=1&t=1647).

Incremental garbage collection for Javascript.

Bugfixes:

-   Minimize intermediate surface size in Azure to mitigate performance regression.
-   Fix SVG clip paths in Azure.
-   Fix drawing artifacts in Azure.
-   Fix fuzzy equal function.
-   Some fixes for the Windows Aero user interface that popped up with the new compiler.

A slightly more detailed description can be found in the [changelog on the forum](http://forum.palemoon.org/viewtopic.php?f=1&t=1658).

### 15.2.1

This update incorporates critical security fixes back-ported from Firefox 16.0.2 ([MFSA 2012-90](https://www.mozilla.org/security/announce/2012/mfsa2012-90.html))

### 15.2

This is an update to address a number of performance, stability and security issues, as well as some added features.
 Fixes:

-   Important performance regression fix. Both javascript and the layout engine should now have the speed and stability that is to be expected from an optimized browser.
-   Fix for the "tabs on top" menu entry not showing when tabs are already set on top, making it very difficult to switch them back to bottom.
-   Crash: Fix for a browser crash with certain types of invalid gradients. ([bug \#792903](https://bugzilla.mozilla.org/show_bug.cgi?id=792903))
-   Security: Prevent private browsing data leakage through popup windows ([bug \#795015](https://bugzilla.mozilla.org/show_bug.cgi?id=795015))
-   Security: Detect IC purging ([bug \#794025](https://bugzilla.mozilla.org/show_bug.cgi?id=794025))
-   Security: Prevent mRules from dying in DoInsertHTMLWithContext ([bug \#788950](https://bugzilla.mozilla.org/show_bug.cgi?id=788950))
-   Security: Drain the parent frame's overflow list before insert/append ([bug \#765621](https://bugzilla.mozilla.org/show_bug.cgi?id=765621))

Features:

-   Redesigned the identity panel and the way secure sites are handled in the UI
     You will now always get the favicon in the address bar, and on secure sites you will have an added padlock (indicating ssl, extended verification or a broken/insecure/mixed-content site) to the identity panel and colored shading around the URL to indicate the status. (see detailed changelog)
-   After evaluating the new address bar autocomplete algorithm, it is now switched on by default.
-   Added an option to easily switch address autocompletion on or off (see detailed changelog)
-   Partial implementation of Japanese "status bar" preferences text

More details can be found in the [detailed changelog](http://forum.palemoon.org/viewtopic.php?f=1&t=1510) on the Pale Moon forum.

### 15.1.1

This is a minor update to address some important performance (high CPU usage) and stability (browser hang) issues in Pale Moon 15.1. Specifically, some of Tete009's patches were backed out.
 Azure acceleration with his patches is still in place, but the multi-threaded box blur and cairo patches were removed to fix the CPU and browser hang issues, respectively.

### 15.1

This is a major update to the new v15.0 release, to address a fairly large number of issues with the initial version.
 Important note:

-   From this release onwards, the system requirements for your operating system have changed: If you are still running Windows XP, you are required to have Service Pack 3 installed on it, or the browser will not start.

Bugfixes:

-   Restore Windows XP Professional x64 compatibility in the installer.
-   Fix the mouse wheel smooth scrolling preferences in the preferences dialog box (did not work in v15.0)
-   Prevent memory inflation on some integrated graphics drivers in canvas games
-   Fix for private browsing mode (Firefox 15.0.1 top fix)
-   Fix for Javascript stability issues on 32-bit versions

Regression fixes:

-   Restore the favicon in the URL bar. (Behavior change: new logic)
-   Fix for top level images with transparency (white background)
-   Remove noise from top level image background
-   Undo the redesign of the Safe Mode dialog box
-   Restore Alt-Click save dialog box
-   Restore proper identity panel for domain-verified sites (blue panel)
-   Restore support for the browser.identity.ssl\_domain\_display setting
-   Restore address bar autofill preference to its desired default state (no autofill)

Added features:

-   Add control for a custom top level image background color
-   Implement Direct2D brush caching (performance win)
-   Implement multi-threaded box blur (performance win for multi-core systems)
-   Add a Profile Reset feature (from Help -\> Troubleshooting information)
-   Build with a faster floating point method

To keep these release notes concise, this is just a plain list of changes. You are encouraged to read the [extended changelog post](http://forum.palemoon.org/viewtopic.php?f=1&t=1414) on the Pale Moon forum if you have any questions or want clarification about any of the items mentioned.

### 15.0

This is a new release based on the Gecko 15.0 code base with additional branch development. It incorporates many changes under the hood that go far beyond the scope of this document.
 A few highlights, in addition to security fixes:

-   Performance improvements for the rendering engine
-   More HTML 5 implemented
-   Better handling of memory, resulting in smoother operation of the browser
-   More responsive user interface when the browser is busy
-   Prevention of memory leaks through add-ons
-   Better implementation of the Quickdial page
-   Localization of Pale Moon specific preferences and options (work in progress)
-   Reinstatement of the previous user interface, keeping it in line with version 12 (Firefox 15 has UI changes that makes the controls flat, monochrome and borderless, which isn't desired for Pale Moon)
-   The padlock has returned for secure pages! It can be found in front of the URL when you browse to a secure page, with optionally company information if supplied by the server

Some things new to the Firefox code base that are excluded or disabled by default:

-   Built-in PDF reader in javascript - use a standalone, dedicated reader
     (this is both a security and functionality consideration)
-   Additional advanced web development tools - the average user never needs these
-   Web apps on the desktop - Pale Moon is a browser, not a pseudo-OS
-   Windows Metro UI

Release notes for version 12
============================

### 12.3 r2

This is a rebuild of Pale Moon 12.3 to address some performance regression in the 32-bit version of Pale Moon 12.3. No functional changes have been implemented in this release, and updating the browser is completely optional, but recommended if you experience lesser performance in Pale Moon 12.3 than you are used to.
 This build should give overall smoother operation of the browser and better HTML5 video playback (WebM/Theora) than the original release.
 Note: once again, this is to address a regression that was only present in the 32-bit (x86) version of Pale Moon 12.3 and does not affect Pale Moon x64. Pale Moon 12.3r2 is an update for the 32-bit version only and completely optional if you are already on v12.3.

### 12.3

This is a maintenance release to address a number of potential security vulnerabilities.
 Most notably among them:

-   Use-after-free while replacing/inserting a node in a document
-   Content Security Policy inline-script bypass
-   A few additional memory safety hazards and crashes that could theoretically be exploited

Additional note: the initial binaries as-published had a missing component that caused saving of a session to malfunction and not properly restore. If you have downloaded 12.3 x86 between 12:00 and 17:00 Central European Summer Time (Z+2) on 17/7/2012, or x64 on the same day between 12:00 and 19:00, you may have a faulty browser and you should download and re-install the browser. Apologies for the inconvenience!

### 12.2.1

A minor update to fix stability issues:

-   Fix for Flash 11.3 crashing the plugin container or browser upon shutdown.
     Note: Flash 11.3 introduces "protected mode", an internal plugin sandbox, that can still cause other issues, e.g. for full-screen video playback. Please check the [forum announcement](http://forum.palemoon.org/viewtopic.php?f=1&t=922) for more information, workarounds and advice if you experience issues.
-   Fix for double entries for "recent tags" and "recently bookmarked" in the bookmarks menu on a new profile.
-   Fix for build instability issues in javascript due to the Microsoft compiler producing incorrect machine code in some cases. This fix will cause a slight performance loss on 32-bit builds to prevent crashes and scripting problems in the GUI and on web pages. Pale Moon x64 is not affected.

### 12.2

A minor update but introducing some new features.
 Most important changes:

-   Privacy issue: clear the QuickDial page when history is cleared
-   Better, neutral background color for raw image viewing
-   Smooth scrolling improved; also disabled smooth scrolling for page-by-page by default
-   Smooth scrolling can now be configured/tuned in detail in Advanced Options
-   Some changes to the build method for x64 for potentially better performance on some systems

### 12.1

A major update to the browser, implementing a number of visual and under-the-hood changes.
 Security fixes:

-   [Bug \#745580](https://bugzilla.mozilla.org/show_bug.cgi?id=745580) Thebes: handle bad results from Core Text shaping more robustly.
-   [Bug \#744541](https://bugzilla.mozilla.org/show_bug.cgi?id=744541) XPCOM i/o: Charset conversion issue.
-   [Bug \#748613](https://bugzilla.mozilla.org/show_bug.cgi?id=748613) Javascript: Scope vulnerability
-   [Bug \#747688](https://bugzilla.mozilla.org/show_bug.cgi?id=747688) Layout engine: Drop references for all destroyed frames
-   Security update of the included MSVC runtime libraries

Enhancements and fixes:

Dynamic smooth scrolling algorithm for mouse/keyboard implemented. Smooth scrolling is now also enabled by default.

Update to the status bar code to fix pop-up status not switching sides on mouse-over, as well as using a safer allocation/destruction mechanism for controls (potentially preventing memory leaks).

Fixed: cache size override on new profiles (would be set to 1GB instead of the application default of 200MB). Bug [20120512-GN](http://www.palemoon.org/bugs/20120512-GNBug.html).

Addition of a number of preferences in the Tabs category of the options dialog box:

-   A checkbox for inserting related tabs next to the current tab when opening a link;
-   A checkbox for closing the browser window when the last tab is closed;
-   A selection for new tabs: Choose from a blank page, the Pale Moon start page or the Quickdial page.

Some slight color has been re-introduced in the navigation elements to improve clarity of the UI.

Disabled an image decoding library with hazardous code. This has no impact on the browser's image decoding capabilities or performance as alternative methods for decoding are used by default.

Some changes to memory handling which potentially keep memory use better within bounds.

A change to the build environment to improve stability of Javascript. Note that this is a trade-off and may result in a slight drop in synthetic benchmarking performance of the browser compared to the previous version of Pale Moon. The impact of this on overall real-world performance of the browser is negligible.

### 12.0

A new release built on the Firefox 12 code base. This is mostly a maintenance release.
 Of special interest is that Pale Moon, unlike Mozilla Firefox in its version 12, does not move to a silent install method.
 Fixes and changes in all versions:

-   A number of security and stability fixes imported from Firefox 12
-   Update to the status bar code and a finalization of the integration of this functionality
-   Localization of the status bar preferences and messages into 3 additional common languages: German, French and Spanish. This will automatically follow your locale setting
-   Update to the HTML5 media controls
-   Some under-the-hood changes that will further improve performance of Pale Moon on some systems

Pale Moon Portable:

-   An update to the launcher and script. More information on the [Pale Moon Portable page](http://www.palemoon.org/palemoon-portable.shtml). It is recommended for everyone using the portable to make a fresh install (with a copy over of your profile files if desired) and not use an in-place upgrade.
-   A fix for backups not being properly created and stored

This version marks the start of a different release schedule for Pale Moon.
 Instead of following the rapid release schedule of Mozilla, the browser will use version 12, a properly matured build with essential functionality, as a base to make incremental updates upon.
 The reasons for this are multiple, not in the least it will allow Pale Moon to implement things that have been on the "to-do" list for a while but which have been pushed back because of lack of time "keeping up" with the (too) fast releases of new versions by Mozilla. The rapid releases often little more than maintenance updates and one or two new features (sometimes even just partially implemented), but requiring a full development/testing cycle for Pale Moon because of the lumped-together patches. Another reason is that from this point on, Firefox will receive a few user interface overhauls to go off on the "Web OS"/"Metro"/"Desktop integration" tangent that goes against Pale Moon's goals of being and remaining a web browser.

Release notes for version 11
============================

### 11.0.1

A small maintenance release to address a few annoyances in the original new release of v11.0:

-   Tone down the rather too aggressive network settings that were introduced in Firefox 11:
    1.  Maximum concurrent connections opened by Pale Moon lowered to 48 (was 256) to make sure it doesn't easily saturate the networking layer of Windows, to prevent residential NAT gateways from being overloaded and to lower strain on wireless networks
    2.  Maximum concurrent connections to a single server lowered to 6 (was 15). In tandem with pipelining this is still plenty, and it actually promotes the proper intended use of pipelining
-   Fix the installer to properly check for the minimum required operating system version: Windows XP for both x86 and x64 versions
-   Switch off OpenGL as preferred engine for WebGL again to fix compatibility problems
-   Switch off DirectWrite font rendering again to fix compatibility issues with certain graphics cards
-   Properly implement the removal/blocking of status bar add-ons when upgrading/installing

In addition, Pale Moon 11 will:

-   Save open session information less often (once a minute)
-   No longer store form data and other entered information in the browser session store when pages are viewed over SSL, to increase security - the data entered in secure pages is no longer committed to disk.

### 11.0

A new major release building on the Firefox 11.0 code base. This sees many, many bugfixes and a good number of performance improvements that would be too extensive to list in detail.

#### In summary

All versions:

-   Integration of the Status Bar functionality. You can now access the status bar configuration directly from the Tools or Options menu, and it is no longer a separate add-on<sup>[[note]](http://www.palemoon.org/knownissues.shtml#Pale_Moon_status_bar_add-on)</sup>
-   Implementation of the SPDY protocol to improve load times on websites that support it
-   Improvements to the rendering engine, and specific improvements to the hardware acceleration of font rendering and WebGL
-   Implementation of more advanced HTML5 (including IndexedDB improvements and support for more HTML5 elements) and CSS features (including CSS 3D transforms)
-   Add-on compatibility: add-ons now default to being compatible
-   Cosmetic updates to the Pale Moon icon and some included artwork
-   The (limited feature) web developer tools as added to Firefox 11.0 have mostly been disabled since they are not intended to be present in future builds of Pale Moon. Web developers are encouraged to make their own choices about which development tools they wish to use at the [Mozilla Add-ons repository (Web development section)](https://addons.mozilla.org/firefox/collections/mozilla/webdeveloper/)

Pale Moon Portable:

-   The automatic backup method has been simplified and the resulting backups will take (a lot) less space. Bookmarks and passwords are backed up, but history no longer gets backed up which saves many MB on your stick or other portable medium
-   Additional compression further reduces space requirements for the 32-bit portable

Release notes for version 9
===========================

### 9.2

This update implements security fixes from Firefox up to 10.0.2 in terms of security and stability.

-   Fix for a critical vulnerability in the libpng graphics library
-   Update to the add-ons blocklist and its handling
-   New 64-bit application icon (32-bit icon update in the next version)
-   Minor updates to cache handling settings

### 9.1

This update implements relevant fixes from Firefox 10 in terms of security and stability. Additional new functionality found in Firefox 10 has not been implemented.
 In addition, the following fixes:

-   Update to the status bar component to fix pop-up status and links, as well as a few other small issues
-   Update to the add-on compatibility assistant to no longer display the status bar core add-on as selectable
-   Update to a few default settings based on usage metrics
-   Removed some commercial search engines (e.g. Amazon) and added [DuckDuckGo](https://duckduckgo.com/)/SSL

### 9.0.1

An update incorporating the Firefox 9.0.1 code base.
 In addition, the following changes:

-   Under the hood changes and improvements to the way memory is handled by the Javascript engine
-   WebGL has been changed to use ANGLE by default instead of using native OpenGL to give better performance on a number of systems that would otherwise suffer from high CPU usage and lower frame rates
-   Change in compiler: from this point on, Visual Studio 2010 will be used for all "next gen" builds
-   Build environment changed to cater to the ever-growing XUL dll size without having to compromize on what modules to optimize. (Prevent running into the 3GB address space limit)
-   DNS prefetching disabled by default to prevent router hangups
-   Changes to timings for UI script execution and content script execution to prevent unnecessary dialog popups about unresponsive scripts
-   Some image decoding tweaks
-   Eye candy: animated preferences dialogs (resize when switching category)

### 9.0

Development version (unreleased).
 This version was not considered acceptable for release due to performance regressions.

Release notes for version 8
===========================

### 8.0

A major update building on the Firefox 8.0 code base, with improvements that were planned for the (unreleased) version 7.0.2.
 This version sees the following improvements in addition to those inherent to Firefox 8:

-   Improved cache handling: this will make the browser handle system resources more efficiently on most systems
-   Improved networking: communication with web servers should be noticeably faster and smoother
-   Fix for a rare image decoding bug (garbage, possible crashes)

It should be noted that the shift in focus of development has been towards the back-end of the browser (background resource handling and background networking), considering the rendering and scripting speed is not the bottleneck for current versions of the browser. Inherently, this may result in less of a clear difference in benchmark scores when comparing to its vulpine sibling or previous versions of Pale Moon because of rebalancing of code priority when building. Maximum benchmark scores are nice, of course, but the main goal of Pale Moon remains to be as efficient as possible when taken as a whole, including those parts that aren't measured in limited benchmark tests.

Release notes for version 7
===========================

### 7.0.1

This update fixes a very important speed regression issue for Pale Moon 7.0. It impacts mostly the page content layout engine and DOM handling, which will be on par again with what they should be for Pale Moon in this point release. Bug: [20111001-CCBug](http://www.palemoon.org/bugs/20111001-CCBug.shtml)
 Benchmarking scores will see a significant jump from 7.0 to 7.0.1 as a result, as well.
 There are no other functional or UI changes for this release.

### 7.0

A new release building on the Firefox 7.0.1 code base. This version sees a large number of performance increases, as well as lower resource use than the previous version.
 Additional changes:

-   Introduction of the Tab Groups (panorama) button in the user interface for easy access (Next to the All Tabs button)
-   Several additional performance improvements on individual browser components
-   A change of the Application Menu button color to blue shades instead of a "foxy orange"

Release notes for version 6
===========================

### 6.0.2

A security update seeing the following changes:

-   Removed trust exceptions for certain certificates ([bug 683449](https://bugzilla.mozilla.org/show_bug.cgi?id=683449))
-   Resolved an issue with gov.uk websites ([bug 669792](https://bugzilla.mozilla.org/show_bug.cgi?id=669792))
-   Revoked permissions for the root certificate for DigiNotar in the built-in certificate store ([bug 682927](https://bugzilla.mozilla.org/show_bug.cgi?id=682927))

In addition, there has been some work done on the x64 build method to prepare for performance fixes in a future release.

### 6.0

New release based on Firefox 6.0!
 Of course the necessary bugfixes and changes as part of the new code base, with a number of additional changes:

-   Add-ons will no longer automatically update by default the moment they are checked and found to have a newer release, giving the user the choice to accept or reject the update, read release notes, etc.
-   Update of the status bar add-on to v2.2, fixing compatibility issues and extending some configurability
-   Link right-click menu has "Open in new tab" on top now, like Firefox. If you are having trouble retraining yourself for this behavior, please download a menu editing Firefox add-on to customize your menus. This change was made based on user feedback
-   Added ak, ast, br, bs, en-ZA, gd, lg, mai, nso, and son language packs

Release notes for version 5
===========================

### 5.0

New release based on Firefox 5.0!
 Of course a large number of bugfixes were applied as part of development on the Mozilla trunk, compared to 4.x
 Changes/fixes:

-   Performance issues fixed on some systems
-   Instability problems fixed on some systems
-   Updated artwork for the new about box
-   Cosmetic changes: more occurrences of "palemoon" in the UI should now read "pale moon"
-   Zulu language added for the language packs

Release notes for version 4
===========================

### 4.0.7

Fixes:

-   Windows 7 jumplists fixed
-   Fix for errors in the error console when closing tabs in certain situations
-   Potential fix for printing problems (no text printed) Still an issue

### 4.0.6

Update of the source base to Firefox 4.0.1, fixing a [large number of bugs](https://bugzilla.mozilla.org/buglist.cgi?quicksearch=ALL%20status2.0%3A.1-fixed).
 Additional updates:

-   Performance fix: Switched back globally to fast floating point model, with a patch to prevent rounding errors in javascript
-   Compensated for a number of internal compiler errors causing build issues and potential browser stability problems on some systems

### 4.0.5

A number of fixes and a cosmetic update:

-   Performance fix: Javascript performance improved
-   Crash fix: Prevent crashes in optimized builds of JS due to [20110410-CCBug](http://www.palemoon.org/bugs/20110410-CCBug.shtml)
-   Updater fix: Internal updater should function again from this version onward
-   Add-ons window shows the proper add-ons page when loading it
-   Shell integration fixed for Vista and 7: The browser should no longer complain that it's not the default program when it, in fact, is. See bug [20110408-SHBug](http://www.palemoon.org/bugs/20110408-SHBug.shtml)
-   Main Pale Moon program icon updated with a higher-res version of the logo image

### 4.0.3

Some stability bugfixes and improvements:

-   Fix for a potential crash of the browser due to [20110406-PRBug](http://www.palemoon.org/bugs/20110406-PRBug.shtml)
-   Fix for the internal manual updater when a language pack is installed
-   DirectWrite now default OFF (gfx.font\_rendering.directwrite.enabled=false) to provide better compatibility with problematic (integrated) graphics processors. If it worked for you for 4.0, feel free to switch it back on (set to true)
-   Improvements to GUI/chrome speed

If you use a [language pack](http://www.palemoon.org/langpacks.shtml), you will have to update it to the latest version for it to work.

### 4.0

This is a brand new release!
 Built on Gecko 2.0, the browser has the following new features:

-   Hardware acceleration of browser windows using Direct3D
-   Direct2D rendering on operating systems and hardware that support it
-   DirectWrite font rendering on systems that support it
-   WebGL 3D graphics, either using native OpenGL (default) or DirectX (via ANGLE)
-   HTML5 support
-   New Jaegermonkey javascript engine
-   Blazingly fast DOM handling

Of course, the well known features from version 3.x are still present, as well:

-   Graphical tab previews (on a nice glass pane if you have Windows 7) with search
-   Dynamic/downloadable fonts (including WOFF)
-   Support for personas and Firefox themes
-   Support for Firefox 4 compatible add-ons
-   Support for OOPP (Out-of-process plug-ins)
-   Able to use existing Firefox profiles, bookmarks and settings (either using the migration tool or using Sync)

Since this is a new release, it may be a little less stable than the tried-and-tested 3.x releases, but no major issues have been found thus far.
