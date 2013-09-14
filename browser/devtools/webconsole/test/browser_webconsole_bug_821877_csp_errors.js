// Tests that CSP errors from nsDocument::InitCSP are logged to the Web Console

/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const TEST_URI = "https://example.com/browser/browser/devtools/webconsole/test/test-bug-821877-csperrors.html";
const CSP_DEPRECATED_HEADER_MSG = "The X-Content-Security-Policy and X-Content-Security-Report-Only headers will be deprecated in the future. Please use the Content-Security-Policy and Content-Security-Report-Only headers with CSP spec compliant syntax instead.";

function test()
{
  addTab(TEST_URI);
  browser.addEventListener("load", function onLoad(aEvent) {
    browser.removeEventListener(aEvent.type, onLoad, true);
    openConsole(null, function testCSPErrorLogged (hud) {
      waitForMessages({
        webconsole: hud,
        messages: [
          {
            name: "Deprecated CSP header error displayed successfully",
            text: CSP_DEPRECATED_HEADER_MSG,
            category: CATEGORY_SECURITY,
            severity: SEVERITY_WARNING
          },
        ],
      }).then(finishTest);
    });
  }, true);
}
