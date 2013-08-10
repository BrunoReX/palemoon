/* vim:set ts=2 sw=2 sts=2 et: */
/*
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

function test() {
  var tab1 = addTab(TAB1_URL, function() {
    gBrowser.selectedTab = tab1;

    ok(!DebuggerUI.getDebugger(),
      "Shouldn't have a debugger pane for this tab yet.");

    let pane = DebuggerUI.toggleDebugger();
    ok(pane, "toggleDebugger() should return a pane.");

    let preferredSfw = Services.prefs.getIntPref("devtools.debugger.ui.stackframes-width");
    let preferredBpw = Services.prefs.getIntPref("devtools.debugger.ui.variables-width");
    let someWidth1, someWidth2;

    do {
      someWidth1 = parseInt(Math.random() * 200) + 100;
      someWidth2 = parseInt(Math.random() * 200) + 100;
    } while (someWidth1 == preferredSfw ||
             someWidth2 == preferredBpw)

    info("Preferred stackframes width: " + preferredSfw);
    info("Preferred variables width: " + preferredBpw);
    info("Generated stackframes width: " + someWidth1);
    info("Generated variables width: " + someWidth2);

    is(DebuggerUI.getDebugger(), pane,
      "getDebugger() should return the same pane as toggleDebugger().");

    let content = pane.contentWindow;
    let stackframes;
    let variables;

    wait_for_connect_and_resume(function() {
      ok(content.Prefs.stackframesWidth,
        "The debugger preferences should have a saved stackframesWidth value.");
      ok(content.Prefs.variablesWidth,
        "The debugger preferences should have a saved variablesWidth value.");

      stackframes = content.document.getElementById("stackframes+breakpoints");
      variables = content.document.getElementById("variables+expressions");

      is(content.Prefs.stackframesWidth, stackframes.getAttribute("width"),
        "The stackframes pane width should be the same as the preferred value.");
      is(content.Prefs.variablesWidth, variables.getAttribute("width"),
        "The variables pane width should be the same as the preferred value.");

      stackframes.setAttribute("width", someWidth1);
      variables.setAttribute("width", someWidth2);

      removeTab(tab1);
    });

    window.addEventListener("Debugger:Shutdown", function dbgShutdown() {
      window.removeEventListener("Debugger:Shutdown", dbgShutdown, true);

      is(content.Prefs.stackframesWidth, stackframes.getAttribute("width"),
        "The stackframes pane width should have been saved by now.");
      is(content.Prefs.variablesWidth, variables.getAttribute("width"),
        "The variables pane width should have been saved by now.");

      finish();

    }, true);
  });
}
