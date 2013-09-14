/* vim:set ts=2 sw=2 sts=2 et: */
/* ***** BEGIN LICENSE BLOCK *****
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * Contributor(s):
 *  Mihai Șucan <mihai.sucan@gmail.com>
 *
 * ***** END LICENSE BLOCK ***** */

const TEST_URI = "http://example.com/browser/browser/devtools/webconsole/test/test-network.html";

function consoleOpened(aHud) {
  hud = aHud;

  for (let i = 0; i < 200; i++) {
    content.console.log("test message " + i);
  }

  hud.setFilterState("network", false);
  hud.setFilterState("networkinfo", false);

  hud.ui.filterBox.value = "test message";
  hud.ui.adjustVisibilityOnSearchStringChange();

  waitForMessages({
    webconsole: hud,
    messages: [{
      name: "console messages displayed",
      text: "test message 199",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_LOG,
    }],
  }).then(() => {
    waitForMessages({
      webconsole: hud,
      messages: [{
        text: "test-network.html",
        category: CATEGORY_NETWORK,
        severity: SEVERITY_LOG,
        successFn: testScroll,
        failureFn: finishTest,
      }],
    }).then(testScroll);
    content.location.reload();
  });
}

function testScroll() {
  let msgNode = hud.outputNode.querySelector(".webconsole-msg-network");
  ok(msgNode.classList.contains("hud-filtered-by-type"),
    "network message is filtered by type");
  ok(msgNode.classList.contains("hud-filtered-by-string"),
    "network message is filtered by string");

  let scrollBox = hud.outputNode.scrollBoxObject.element;
  ok(scrollBox.scrollTop > 0, "scroll location is not at the top");

  // Make sure the Web Console output is scrolled as near as possible to the
  // bottom.
  let nodeHeight = hud.outputNode.querySelector(".hud-log").clientHeight;
  ok(scrollBox.scrollTop >= scrollBox.scrollHeight - scrollBox.clientHeight -
     nodeHeight * 2, "scroll location is correct");

  hud.setFilterState("network", true);
  hud.setFilterState("networkinfo", true);

  executeSoon(finishTest);
}

function test() {
  const PREF = "devtools.webconsole.persistlog";
  Services.prefs.setBoolPref(PREF, true);
  registerCleanupFunction(() => Services.prefs.clearUserPref(PREF));

  addTab(TEST_URI);
  browser.addEventListener("load", function onLoad() {
    browser.removeEventListener("load", onLoad, true);
    openConsole(null, consoleOpened);
  }, true);
}
