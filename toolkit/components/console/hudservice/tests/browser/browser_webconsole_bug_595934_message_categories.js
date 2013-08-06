/* vim:set ts=2 sw=2 sts=2 et: */
/* ***** BEGIN LICENSE BLOCK *****
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * Contributor(s):
 *  Mihai Șucan <mihai.sucan@gmail.com>
 *
 * ***** END LICENSE BLOCK ***** */

const TESTS_PATH = "http://example.com/browser/toolkit/components/console/hudservice/tests/browser/";
const TESTS = [
  { // #0
    file: "test-bug-595934-css-loader.html",
    category: "CSS Loader",
    matchString: "text/css",
  },
  { // #1
    file: "test-bug-595934-dom-events.html",
    category: "DOM Events",
    matchString: "preventBubble()",
  },
  { // #2
    file: "test-bug-595934-dom-html.html",
    category: "DOM:HTML",
    matchString: "getElementById",
  },
  { // #3
    file: "test-bug-595934-imagemap.html",
    category: "ImageMap",
    matchString: "shape=\"rect\"",
  },
  { // #4
    file: "test-bug-595934-html.html",
    category: "HTML",
    matchString: "multipart/form-data",
    onload: function() {
      let form = content.document.querySelector("form");
      form.submit();
    },
  },
  { // #5
    file: "test-bug-595934-malformedxml.xhtml",
    category: "malformed-xml",
    matchString: "no element found",
  },
  { // #6
    file: "test-bug-595934-svg.xhtml",
    category: "SVG",
    matchString: "fooBarSVG",
  },
  { // #7
    file: "test-bug-595934-workers.html",
    category: "DOM Worker javascript",
    matchString: "fooBarWorker",
  },
  { // #8
    file: "test-bug-595934-dom-html-external.html",
    category: "DOM:HTML",
    matchString: "document.all",
  },
  { // #9
    file: "test-bug-595934-dom-events-external.html",
    category: "DOM Events",
    matchString: "clientWidth",
  },
  { // #10
    file: "test-bug-595934-dom-events-external2.html",
    category: "DOM Events",
    matchString: "preventBubble()",
  },
  { // #11
    file: "test-bug-595934-canvas.html",
    category: "Canvas",
    matchString: "strokeStyle",
  },
  { // #12
    file: "test-bug-595934-css-parser.html",
    category: "CSS Parser",
    matchString: "foobarCssParser",
  },
  { // #13
    file: "test-bug-595934-malformedxml-external.html",
    category: "malformed-xml",
    matchString: "</html>",
  },
  { // #14
    file: "test-bug-595934-empty-getelementbyid.html",
    category: "DOM",
    matchString: "getElementById",
  },
  { // #15
    file: "test-bug-595934-canvas-css.html",
    category: "CSS Parser",
    matchString: "foobarCanvasCssParser",
  },
  { // #17
    file: "test-bug-595934-getselection.html",
    category: "content javascript",
    matchString: "getSelection",
  },
  { // #18
    file: "test-bug-595934-image.html",
    category: "Image",
    matchString: "corrupt",
  },
];

let pos = -1;

let foundCategory = false;
let foundText = false;
let output = null;
let jsterm = null;

let TestObserver = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver]),

  observe: function test_observe(aSubject)
  {
    if (!(aSubject instanceof Ci.nsIScriptError)) {
      return;
    }

    is(aSubject.category, TESTS[pos].category,
      "test #" + pos + ": error category '" + TESTS[pos].category + "'");

    if (aSubject.category == TESTS[pos].category) {
      foundCategory = true;
      if (foundText) {
        executeSoon(testNext);
      }
    }
    else {
      executeSoon(finish);
    }
  }
};

function tabLoad(aEvent) {
  browser.removeEventListener(aEvent.type, arguments.callee, true);

  openConsole();

  let hudId = HUDService.getHudIdByWindow(content);
  let hud = HUDService.hudReferences[hudId];
  output = hud.outputNode;
  output.addEventListener("DOMNodeInserted", onDOMNodeInserted, false);
  jsterm = hud.jsterm;

  Services.console.registerListener(TestObserver);

  executeSoon(testNext);
}

function testNext() {
  jsterm.clearOutput();
  foundCategory = false;
  foundText = false;

  pos++;
  if (pos < TESTS.length) {
    if (TESTS[pos].onload) {
      browser.addEventListener("load", function(aEvent) {
        browser.removeEventListener(aEvent.type, arguments.callee, true);
        TESTS[pos].onload(aEvent);
      }, true);
    }

    content.location = TESTS_PATH + TESTS[pos].file;
  }
  else {
    executeSoon(finish);
  }
}

function testEnd() {
  Services.console.unregisterListener(TestObserver);
  output.removeEventListener("DOMNodeInserted", onDOMNodeInserted, false);
  output = jsterm = null;
  finishTest();
}

function onDOMNodeInserted(aEvent) {
  let textContent = output.textContent;
  foundText = textContent.indexOf(TESTS[pos].matchString) > -1;
  if (foundText) {
    ok(foundText, "test #" + pos + ": message found '" + TESTS[pos].matchString + "'");
  }

  if (foundCategory) {
    executeSoon(testNext);
  }
}

function test() {
  registerCleanupFunction(testEnd);

  addTab("data:text/html,Web Console test for bug 595934 - message categories coverage.");
  browser.addEventListener("load", tabLoad, true);
}

