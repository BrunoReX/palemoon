/*
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Check that Dead Objects do not break the Web/Browser Consoles. See bug 883649.

const TEST_URI = "data:text/html;charset=utf8,<p>dead objects!";

function test()
{
  let hud = null;

  addTab(TEST_URI);
  browser.addEventListener("load", function onLoad() {
    browser.removeEventListener("load", onLoad, true);
    info("open the browser console");
    HUDConsoleUI.toggleBrowserConsole().then(onBrowserConsoleOpen);
  }, true);

  function onBrowserConsoleOpen(aHud)
  {
    hud = aHud;
    ok(hud, "browser console opened");

    hud.jsterm.clearOutput();
    hud.jsterm.execute("foobarzTezt = content.document", onAddVariable);
  }

  function onAddVariable()
  {
    gBrowser.removeCurrentTab();

    hud.jsterm.execute("foobarzTezt", onReadVariable);
  }

  function onReadVariable()
  {
    isnot(hud.outputNode.textContent.indexOf("[object DeadObject]"), -1,
          "dead object found");

    hud.jsterm.setInputValue("foobarzTezt");

    for (let c of ".hello") {
      EventUtils.synthesizeKey(c, {}, hud.iframeWindow);
    }

    hud.jsterm.execute(null, onReadProperty);
  }

  function onReadProperty()
  {
    isnot(hud.outputNode.textContent.indexOf("can't access dead object"), -1,
          "'cannot access dead object' message found");

    // Click the second execute output.
    let clickable = hud.outputNode.querySelectorAll(".webconsole-msg-output")[1]
                    .querySelector(".hud-clickable");
    ok(clickable, "clickable object found");
    isnot(clickable.textContent.indexOf("[object DeadObject]"), -1,
          "message text check");

    hud.jsterm.once("variablesview-fetched", onFetched);
    EventUtils.synthesizeMouse(clickable, 2, 2, {}, hud.iframeWindow);
  }

  function onFetched()
  {
    hud.jsterm.execute("delete window.foobarzTezt; 2013-26", onCalcResult);
  }

  function onCalcResult()
  {
    isnot(hud.outputNode.textContent.indexOf("1987"), -1, "result message found");

    // executeSoon() is needed to get out of the execute() event loop.
    executeSoon(finishTest);
  }
}
