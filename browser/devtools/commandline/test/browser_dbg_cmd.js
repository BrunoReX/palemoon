function test() {
  const TEST_URI = "http://example.com/browser/browser/devtools/commandline/" +
                   "test/browser_dbg_cmd.html";

  DeveloperToolbarTest.test(TEST_URI, function() {
    testDbgCmd();
  });
}

function testDbgCmd() {
  DeveloperToolbarTest.exec({
    typed: "dbg open",
    blankOutput: true
  });

  let pane = DebuggerUI.findDebugger();
  ok(pane, "Debugger was opened.");
  let frame = pane._frame;

  frame.addEventListener("Debugger:Connected", function dbgConnected(aEvent) {
    frame.removeEventListener("Debugger:Connected", dbgConnected, true);

    // Wait for the initial resume...
    aEvent.target.ownerDocument.defaultView.gClient
        .addOneTimeListener("resumed", function() {

      info("Starting tests.");

      let contentDoc = content.window.document;
      let output = contentDoc.querySelector("input[type=text]");
      let btnDoit = contentDoc.querySelector("input[type=button]");

      cmd("dbg interrupt", function() {
        ok(true, "debugger is paused");
        pane.contentWindow.gClient.addOneTimeListener("resumed", function() {
          ok(true, "debugger continued");
          pane.contentWindow.gClient.addOneTimeListener("paused", function() {
            cmd("dbg step in", function() {
              cmd("dbg step in", function() {
                cmd("dbg step in", function() {
                  is(output.value, "step in", "debugger stepped in");
                  cmd("dbg step over", function() {
                    is(output.value, "step over", "debugger stepped over");
                    cmd("dbg step out", function() {
                      is(output.value, "step out", "debugger stepped out");
                      cmd("dbg continue", function() {
                        cmd("dbg continue", function() {
                          is(output.value, "dbg continue", "debugger continued");
                          DeveloperToolbarTest.exec({
                            typed: "dbg close",
                            blankOutput: true
                          });

                          let dbg = DebuggerUI.findDebugger();
                          ok(!dbg, "Debugger was closed.");
                          finish();
                        });
                      });
                    });
                  });
                });
              });
            });
          });
          EventUtils.sendMouseEvent({type:"click"}, btnDoit);
        });
        DeveloperToolbarTest.exec({
          typed: "dbg continue",
          blankOutput: true
        });
      });
    });

    function cmd(aTyped, aCallback) {
      pane.contentWindow.gClient.addOneTimeListener("paused", aCallback);
      DeveloperToolbarTest.exec({
        typed: aTyped,
        blankOutput: true
      });
    }
  });
}
