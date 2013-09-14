/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is a test for the Open URL context menu item
// that is shown for network requests
const TEST_URI = "http://example.com/browser/browser/devtools/webconsole/test/test-console.html"
const COMMAND_NAME = "consoleCmd_openURL";
const CONTEXT_MENU_ID = "#menu_openURL";

let HUD = null, outputNode = null, contextMenu = null;

function test() {
  addTab(TEST_URI);
  browser.addEventListener("load", function onLoad() {
    browser.removeEventListener("load", onLoad, true);
    openConsole(null, consoleOpened);
  }, true);
}

function consoleOpened(aHud) {
  HUD = aHud;
  outputNode = aHud.outputNode;
  contextMenu = HUD.iframeWindow.document.getElementById("output-contextmenu");

  registerCleanupFunction(() => {
    HUD = outputNode = contextMenu = null;
  });

  HUD.jsterm.clearOutput();

  content.console.log("bug 764572");

  waitForMessages({
    webconsole: HUD,
    messages: [{
      text: "bug 764572",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_LOG,
    }],
  }).then(onConsoleMessage);
}

function onConsoleMessage(aResults) {
  outputNode.focus();
  outputNode.selectedItem = [...aResults[0].matched][0];

  // Check if the command is disabled non-network messages.
  goUpdateCommand(COMMAND_NAME);
  let controller = top.document.commandDispatcher
                   .getControllerForCommand(COMMAND_NAME);

  let isDisabled = !controller || !controller.isCommandEnabled(COMMAND_NAME);
  ok(isDisabled, COMMAND_NAME + " should be disabled.");

  waitForContextMenu(contextMenu, outputNode.selectedItem, () => {
    let isHidden = contextMenu.querySelector(CONTEXT_MENU_ID).hidden;
    ok(isHidden, CONTEXT_MENU_ID + " should be hidden.");
  }, testOnNetActivity);
}

function testOnNetActivity() {
  HUD.jsterm.clearOutput();

  // Reload the url to show net activity in console.
  content.location.reload();

  waitForMessages({
    webconsole: HUD,
    messages: [{
      text: "test-console.html",
      category: CATEGORY_NETWORK,
      severity: SEVERITY_LOG,
    }],
  }).then(onNetworkMessage);
}

function onNetworkMessage(aResults) {
  outputNode.focus();
  outputNode.selectedItem = [...aResults[0].matched][0];

  let currentTab = gBrowser.selectedTab;
  let newTab = null;

  gBrowser.tabContainer.addEventListener("TabOpen", function onOpen(aEvent) {
    gBrowser.tabContainer.removeEventListener("TabOpen", onOpen, true);
    newTab = aEvent.target;
    newTab.linkedBrowser.addEventListener("load", onTabLoaded, true);
  }, true);

  function onTabLoaded() {
    newTab.linkedBrowser.removeEventListener("load", onTabLoaded, true);
    gBrowser.removeTab(newTab);
    gBrowser.selectedTab = currentTab;
    executeSoon(testOnNetActivity_contextmenu);
  }

  // Check if the command is enabled for a network message.
  goUpdateCommand(COMMAND_NAME);
  let controller = top.document.commandDispatcher
                   .getControllerForCommand(COMMAND_NAME);
  ok(controller.isCommandEnabled(COMMAND_NAME),
     COMMAND_NAME + " should be enabled.");

  // Try to open the URL.
  goDoCommand(COMMAND_NAME);
}

function testOnNetActivity_contextmenu() {
  let target = outputNode.querySelector(".webconsole-msg-network");

  outputNode.focus();
  outputNode.selectedItem = target;

  waitForContextMenu(contextMenu, target, () => {
    let isShown = !contextMenu.querySelector(CONTEXT_MENU_ID).hidden;
    ok(isShown, CONTEXT_MENU_ID + " should be shown.");
  }, finishTest);
}
