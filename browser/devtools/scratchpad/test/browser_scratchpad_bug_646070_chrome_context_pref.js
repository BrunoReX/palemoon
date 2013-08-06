/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Reference to the Scratchpad chrome window object.
let gScratchpadWindow;

let gOldPref;
let DEVTOOLS_CHROME_ENABLED = "devtools.chrome.enabled";

function test()
{
  waitForExplicitFinish();

  gOldPref = Services.prefs.getBoolPref(DEVTOOLS_CHROME_ENABLED);
  Services.prefs.setBoolPref(DEVTOOLS_CHROME_ENABLED, true);

  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function() {
    gBrowser.selectedBrowser.removeEventListener("load", arguments.callee, true);

    ok(Scratchpad, "Scratchpad variable exists");

    gScratchpadWindow = Scratchpad.openScratchpad();
    gScratchpadWindow.addEventListener("load", runTests, false);
  }, true);

  content.location = "data:text/html,Scratchpad test for bug 646070 - chrome context preference";
}

function runTests()
{
  gScratchpadWindow.removeEventListener("load", arguments.callee, false);

  let sp = gScratchpadWindow.Scratchpad;
  ok(sp, "Scratchpad object exists in new window");

  let chromeContextMenu = gScratchpadWindow.document.
                          getElementById("sp-menu-browser");
  ok(chromeContextMenu, "Chrome context menuitem element exists");
  ok(!chromeContextMenu.hasAttribute("hidden"),
     "Chrome context menuitem is visible");

  let errorConsoleCommand = gScratchpadWindow.document.
                            getElementById("sp-cmd-errorConsole");
  ok(errorConsoleCommand, "Error console command element exists");
  ok(!errorConsoleCommand.hasAttribute("disabled"),
     "Error console command is enabled");

  let errorConsoleMenu = gScratchpadWindow.document.
                         getElementById("sp-menu-errorConsole");
  ok(errorConsoleMenu, "Error console menu element exists");
  ok(!errorConsoleMenu.hasAttribute("hidden"),
     "Error console menuitem is visible");

  let chromeContextCommand = gScratchpadWindow.document.
                            getElementById("sp-cmd-browserContext");
  ok(chromeContextCommand, "Chrome context command element exists");
  ok(!chromeContextCommand.hasAttribute("disabled"),
     "Chrome context command is disabled");

  Services.prefs.setBoolPref(DEVTOOLS_CHROME_ENABLED, gOldPref);

  gScratchpadWindow.close();
  gScratchpadWindow = null;
  gBrowser.removeCurrentTab();
  finish();
}
