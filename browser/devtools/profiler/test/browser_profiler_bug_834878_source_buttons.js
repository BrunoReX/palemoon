/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const BASE = "http://example.com/browser/browser/devtools/profiler/test/";
const URL = BASE + "mock_profiler_bug_834878_page.html";
const SCRIPT = BASE + "mock_profiler_bug_834878_script.js";

function test() {
  waitForExplicitFinish();

  setUp(URL, function onSetUp(tab, browser, panel) {
    panel.once("profileCreated", function () {
      let data = { uri: SCRIPT, line: 5, isChrome: false };

      panel.displaySource(data, function onOpen() {
        let target = TargetFactory.forTab(tab);
        let dbg = gDevTools.getToolbox(target).getPanel("jsdebugger");
        let view = dbg.panelWin.DebuggerView;

        is(view.Sources.selectedValue, data.uri, "URI is different");
        is(view.editor.getCaretPosition().line, data.line - 1,
          "Line is different");

        // Test the case where script is already loaded.
        view.editor.setCaretPosition(1);
        gDevTools.showToolbox(target, "jsprofiler").then(function () {
          panel.displaySource(data, function onOpenAgain() {
            is(view.editor.getCaretPosition().line, data.line - 1,
              "Line is different");
            tearDown(tab);
          });
        });
      });
    });

    panel.createProfile();
  });
}
