/**
 * SimpleTest, a partial Test.Simple/Test.More API compatible test library.
 *
 * Why?
 *
 * Test.Simple doesn't work on IE < 6.
 * TODO:
 *  * Support the Test.Simple API used by MochiKit, to be able to test MochiKit
 * itself against IE 5.5
 *
 * NOTE: Pay attention to cross-browser compatibility in this file. For
 * instance, do not use const or JS > 1.5 features which are not yet
 * implemented everywhere.
 *
**/

if (typeof(SimpleTest) == "undefined") {
    var SimpleTest = {};
}

var parentRunner = null;
if (typeof(parent) != "undefined" && parent.TestRunner) {
    parentRunner = parent.TestRunner;
} else if (parent && parent.wrappedJSObject &&
           parent.wrappedJSObject.TestRunner) {
    parentRunner = parent.wrappedJSObject.TestRunner;
}

// Check to see if the TestRunner is present and has logging
if (parentRunner) {
    SimpleTest._logEnabled = parentRunner.logEnabled;
}

SimpleTest._tests = [];
SimpleTest._stopOnLoad = true;

/**
 * Something like assert.
**/
SimpleTest.ok = function (condition, name, diag) {
    var test = {'result': !!condition, 'name': name, 'diag': diag};
    if (SimpleTest._logEnabled)
        SimpleTest._logResult(test, "TEST-PASS", "TEST-UNEXPECTED-FAIL");
    SimpleTest._tests.push(test);
};

/**
 * Roughly equivalent to ok(a==b, name)
**/
SimpleTest.is = function (a, b, name) {
    var repr = MochiKit.Base.repr;
    SimpleTest.ok(a == b, name, repr(a) + " should equal " + repr(b));
};

SimpleTest.isnot = function (a, b, name) {
    var repr = MochiKit.Base.repr;
    SimpleTest.ok(a != b, name, repr(a) + " should not equal " + repr(b));
};

//  --------------- Test.Builder/Test.More todo() -----------------

SimpleTest.todo = function(condition, name, diag) {
  var test = {'result': !!condition, 'name': name, 'diag': diag, todo: true};
  if (SimpleTest._logEnabled)
      SimpleTest._logResult(test, "TEST-UNEXPECTED-PASS", "TEST-KNOWN-FAIL");
  SimpleTest._tests.push(test);
};

SimpleTest._logResult = function(test, passString, failString) {
  var msg = test.result ? passString : failString;
  msg += " | ";
  if (parentRunner.currentTestURL)
    msg += parentRunner.currentTestURL;
  msg += " | " + test.name;
  if (test.diag)
    msg += " - " + test.diag;
  if (test.result) {
      if (test.todo)
          parentRunner.logger.error(msg);
      else
          parentRunner.logger.log(msg);
  } else {
      if (test.todo)
          parentRunner.logger.log(msg);
      else
          parentRunner.logger.error(msg);
  }
};

/**
 * Copies of is and isnot with the call to ok replaced by a call to todo.
**/

SimpleTest.todo_is = function (a, b, name) {
    var repr = MochiKit.Base.repr;
    SimpleTest.todo(a == b, name, repr(a) + " should equal " + repr(b));
};

SimpleTest.todo_isnot = function (a, b, name) {
    var repr = MochiKit.Base.repr;
    SimpleTest.todo(a != b, name, repr(a) + " should not equal " + repr(b));
};


/**
 * Makes a test report, returns it as a DIV element.
**/
SimpleTest.report = function () {
    var DIV = MochiKit.DOM.DIV;
    var passed = 0;
    var failed = 0;
    var todo = 0;

    // Report tests which did not actually check anything.
    if (SimpleTest._tests.length == 0)
      // ToDo: Do s/todo/ok/ when all the tests are fixed. (Bug 483407)
      SimpleTest.todo(false, "[SimpleTest.report()] No checks actually run.");

    var results = MochiKit.Base.map(
        function (test) {
            var cls, msg;
            var diag = test.diag ? " - " + test.diag : "";
            if (test.todo && !test.result) {
                todo++;
                cls = "test_todo";
                msg = "todo | " + test.name + diag;
            } else if (test.result && !test.todo) {
                passed++;
                cls = "test_ok";
                msg = "passed | " + test.name + diag;
            } else {
                failed++;
                cls = "test_not_ok";
                msg = "failed | " + test.name + diag;
            }
            return DIV({"class": cls}, msg);
        },
        SimpleTest._tests
    );

    var summary_class = failed != 0 ? 'some_fail' :
                          passed == 0 ? 'todo_only' : 'all_pass';

    return DIV({'class': 'tests_report'},
        DIV({'class': 'tests_summary ' + summary_class},
            DIV({'class': 'tests_passed'}, "Passed: " + passed),
            DIV({'class': 'tests_failed'}, "Failed: " + failed),
            DIV({'class': 'tests_todo'}, "Todo: " + todo)),
        results
    );
};

/**
 * Toggle element visibility
**/
SimpleTest.toggle = function(el) {
    if (MochiKit.Style.computedStyle(el, 'display') == 'block') {
        el.style.display = 'none';
    } else {
        el.style.display = 'block';
    }
};

/**
 * Toggle visibility for divs with a specific class.
**/
SimpleTest.toggleByClass = function (cls, evt) {
    var elems = getElementsByTagAndClassName('div', cls);
    MochiKit.Base.map(SimpleTest.toggle, elems);
    if (evt)
        evt.preventDefault();
};

/**
 * Shows the report in the browser
**/

SimpleTest.showReport = function() {
    var togglePassed = A({'href': '#'}, "Toggle passed checks");
    var toggleFailed = A({'href': '#'}, "Toggle failed checks");
    var toggleTodo = A({'href': '#'}, "Toggle todo checks");
    togglePassed.onclick = partial(SimpleTest.toggleByClass, 'test_ok');
    toggleFailed.onclick = partial(SimpleTest.toggleByClass, 'test_not_ok');
    toggleTodo.onclick = partial(SimpleTest.toggleByClass, 'test_todo');
    var body = document.body;  // Handles HTML documents
    if (!body) {
        // Do the XML thing.
        body = document.getElementsByTagNameNS("http://www.w3.org/1999/xhtml",
                                               "body")[0];
    }
    var firstChild = body.childNodes[0];
    var addNode;
    if (firstChild) {
        addNode = function (el) {
            body.insertBefore(el, firstChild);
        };
    } else {
        addNode = function (el) {
            body.appendChild(el)
        };
    }
    addNode(togglePassed);
    addNode(SPAN(null, " "));
    addNode(toggleFailed);
    addNode(SPAN(null, " "));
    addNode(toggleTodo);
    addNode(SimpleTest.report());
    // Add a separator from the test content.
    addNode(HR());
};

/**
 * Tells SimpleTest to don't finish the test when the document is loaded,
 * useful for asynchronous tests.
 *
 * When SimpleTest.waitForExplicitFinish is called,
 * explicit SimpleTest.finish() is required.
**/
SimpleTest.waitForExplicitFinish = function () {
    SimpleTest._stopOnLoad = false;
};

SimpleTest.waitForFocus_started = false;
SimpleTest.waitForFocus_loaded = false;
SimpleTest.waitForFocus_focused = false;

/**
 * If the page is not yet loaded, waits for the load event. If the page is
 * not yet focused, focuses and waits for the window to be focused. Calls
 * the callback when completed.
 *
 * targetWindow should be specified if it is different than 'window'.
 */
SimpleTest.waitForFocus = function (callback, targetWindow) {
    if (!targetWindow)
      targetWindow = window;

    SimpleTest.waitForFocus_started = false;

    netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
    var fm = Components.classes["@mozilla.org/focus-manager;1"].
                        getService(Components.interfaces.nsIFocusManager);

    var usedTargetWindow = {};
    fm.getFocusedElementForWindow(targetWindow, true, usedTargetWindow);
    targetWindow = usedTargetWindow.value;

    function debugFocusLog(prefix) {
        netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");

        var baseWindow = targetWindow.QueryInterface(Components.interfaces.nsIInterfaceRequestor)
                                     .getInterface(Components.interfaces.nsIWebNavigation)
                                     .QueryInterface(Components.interfaces.nsIBaseWindow);
        ok(true, prefix + " -- loaded: " + targetWindow.document.readyState +
           " active window: " +
               (fm.activeWindow ? "(" + fm.activeWindow + ") " + fm.activeWindow.location : "<no window active>") +
           " focused window: " +
               (fm.focusedWindow ? "(" + fm.focusedWindow + ") " + fm.focusedWindow.location : "<no window focused>") +
           " desired window: (" + targetWindow + ") " + targetWindow.location +
           " docshell visible: " + baseWindow.visibility);
    }

    debugFocusLog("before wait for focus");

    function maybeRunTests() {
        debugFocusLog("maybe run tests <load:" +
                      SimpleTest.waitForFocus_loaded + ", focus:" + SimpleTest.waitForFocus_focused + ">");
        if (SimpleTest.waitForFocus_loaded &&
            SimpleTest.waitForFocus_focused &&
            !SimpleTest.waitForFocus_started) {
            SimpleTest.waitForFocus_started = true;
            setTimeout(callback, 0, targetWindow);
        }
    }

    function waitForEvent(event) {
        SimpleTest["waitForFocus_" + event.type + "ed"] = true;
        targetWindow.removeEventListener(event.type, waitForEvent, false);
        if (event.type == "MozAfterPaint")
          ok(true, "MozAfterPaint event received");
        maybeRunTests();
    }

    // wait for the page to load if it hasn't already
    SimpleTest.waitForFocus_loaded = (targetWindow.document.readyState == "complete");
    if (!SimpleTest.waitForFocus_loaded) {
        ok(true, "must wait for load");
        targetWindow.addEventListener("load", waitForEvent, false);
    }

    // check if the window is focused, and focus it if it is not
    var focusedWindow = { };
    if (fm.activeWindow)
      fm.getFocusedElementForWindow(fm.activeWindow, true, focusedWindow);

    // if this is a child frame, ensure that the frame is focused
    SimpleTest.waitForFocus_focused = (focusedWindow.value == targetWindow);
    if (SimpleTest.waitForFocus_focused) {
        ok(true, "already focused");
        // if the frame is already focused and loaded, call the callback directly
        maybeRunTests();
    }
    else {
        ok(true, "must wait for focus");
        targetWindow.addEventListener("focus", waitForEvent, false);
        targetWindow.focus();
    }

    targetWindow.addEventListener("MozAfterPaint", waitForEvent, false);
};

/**
 * Executes a function shortly after the call, but lets the caller continue
 * working (or finish).
 */
SimpleTest.executeSoon = function(aFunc) {
    if ("Components" in window && "classes" in window.Components) {
        netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
        var tm = Components.classes["@mozilla.org/thread-manager;1"]
                   .getService(Components.interfaces.nsIThreadManager);

        tm.mainThread.dispatch({
            run: function() {
                aFunc();
            }
        }, Components.interfaces.nsIThread.DISPATCH_NORMAL);
    } else {
        setTimeout(aFunc, 0);
    }
}

/**
 * Talks to the TestRunner if being ran on a iframe and the parent has a
 * TestRunner object.
**/
SimpleTest.talkToRunner = function () {
    if (parentRunner) {
        parentRunner.testFinished(document);
    }
};

/**
 * Finishes the tests. This is automatically called, except when
 * SimpleTest.waitForExplicitFinish() has been invoked.
**/
SimpleTest.finish = function () {
    SimpleTest.showReport();
    SimpleTest.talkToRunner();
};


addLoadEvent(function() {
    if (SimpleTest._stopOnLoad) {
        SimpleTest.finish();
    }
});

//  --------------- Test.Builder/Test.More isDeeply() -----------------


SimpleTest.DNE = {dne: 'Does not exist'};
SimpleTest.LF = "\r\n";
SimpleTest._isRef = function (object) {
    var type = typeof(object);
    return type == 'object' || type == 'function';
};


SimpleTest._deepCheck = function (e1, e2, stack, seen) {
    var ok = false;
    // Either they're both references or both not.
    var sameRef = !(!SimpleTest._isRef(e1) ^ !SimpleTest._isRef(e2));
    if (e1 == null && e2 == null) {
        ok = true;
    } else if (e1 != null ^ e2 != null) {
        ok = false;
    } else if (e1 == SimpleTest.DNE ^ e2 == SimpleTest.DNE) {
        ok = false;
    } else if (sameRef && e1 == e2) {
        // Handles primitives and any variables that reference the same
        // object, including functions.
        ok = true;
    } else if (SimpleTest.isa(e1, 'Array') && SimpleTest.isa(e2, 'Array')) {
        ok = SimpleTest._eqArray(e1, e2, stack, seen);
    } else if (typeof e1 == "object" && typeof e2 == "object") {
        ok = SimpleTest._eqAssoc(e1, e2, stack, seen);
    } else {
        // If we get here, they're not the same (function references must
        // always simply rererence the same function).
        stack.push({ vals: [e1, e2] });
        ok = false;
    }
    return ok;
};

SimpleTest._eqArray = function (a1, a2, stack, seen) {
    // Return if they're the same object.
    if (a1 == a2) return true;

    // JavaScript objects have no unique identifiers, so we have to store
    // references to them all in an array, and then compare the references
    // directly. It's slow, but probably won't be much of an issue in
    // practice. Start by making a local copy of the array to as to avoid
    // confusing a reference seen more than once (such as [a, a]) for a
    // circular reference.
    for (var j = 0; j < seen.length; j++) {
        if (seen[j][0] == a1) {
            return seen[j][1] == a2;
        }
    }

    // If we get here, we haven't seen a1 before, so store it with reference
    // to a2.
    seen.push([ a1, a2 ]);

    var ok = true;
    // Only examines enumerable attributes. Only works for numeric arrays!
    // Associative arrays return 0. So call _eqAssoc() for them, instead.
    var max = a1.length > a2.length ? a1.length : a2.length;
    if (max == 0) return SimpleTest._eqAssoc(a1, a2, stack, seen);
    for (var i = 0; i < max; i++) {
        var e1 = i > a1.length - 1 ? SimpleTest.DNE : a1[i];
        var e2 = i > a2.length - 1 ? SimpleTest.DNE : a2[i];
        stack.push({ type: 'Array', idx: i, vals: [e1, e2] });
        if (ok = SimpleTest._deepCheck(e1, e2, stack, seen)) {
            stack.pop();
        } else {
            break;
        }
    }
    return ok;
};

SimpleTest._eqAssoc = function (o1, o2, stack, seen) {
    // Return if they're the same object.
    if (o1 == o2) return true;

    // JavaScript objects have no unique identifiers, so we have to store
    // references to them all in an array, and then compare the references
    // directly. It's slow, but probably won't be much of an issue in
    // practice. Start by making a local copy of the array to as to avoid
    // confusing a reference seen more than once (such as [a, a]) for a
    // circular reference.
    seen = seen.slice(0);
    for (var j = 0; j < seen.length; j++) {
        if (seen[j][0] == o1) {
            return seen[j][1] == o2;
        }
    }

    // If we get here, we haven't seen o1 before, so store it with reference
    // to o2.
    seen.push([ o1, o2 ]);

    // They should be of the same class.

    var ok = true;
    // Only examines enumerable attributes.
    var o1Size = 0; for (var i in o1) o1Size++;
    var o2Size = 0; for (var i in o2) o2Size++;
    var bigger = o1Size > o2Size ? o1 : o2;
    for (var i in bigger) {
        var e1 = o1[i] == undefined ? SimpleTest.DNE : o1[i];
        var e2 = o2[i] == undefined ? SimpleTest.DNE : o2[i];
        stack.push({ type: 'Object', idx: i, vals: [e1, e2] });
        if (ok = SimpleTest._deepCheck(e1, e2, stack, seen)) {
            stack.pop();
        } else {
            break;
        }
    }
    return ok;
};

SimpleTest._formatStack = function (stack) {
    var variable = '$Foo';
    for (var i = 0; i < stack.length; i++) {
        var entry = stack[i];
        var type = entry['type'];
        var idx = entry['idx'];
        if (idx != null) {
            if (/^\d+$/.test(idx)) {
                // Numeric array index.
                variable += '[' + idx + ']';
            } else {
                // Associative array index.
                idx = idx.replace("'", "\\'");
                variable += "['" + idx + "']";
            }
        }
    }

    var vals = stack[stack.length-1]['vals'].slice(0, 2);
    var vars = [
        variable.replace('$Foo',     'got'),
        variable.replace('$Foo',     'expected')
    ];

    var out = "Structures begin differing at:" + SimpleTest.LF;
    for (var i = 0; i < vals.length; i++) {
        var val = vals[i];
        if (val == null) {
            val = 'undefined';
        } else {
            val == SimpleTest.DNE ? "Does not exist" : "'" + val + "'";
        }
    }

    out += vars[0] + ' = ' + vals[0] + SimpleTest.LF;
    out += vars[1] + ' = ' + vals[1] + SimpleTest.LF;

    return '    ' + out;
};


SimpleTest.isDeeply = function (it, as, name) {
    var ok;
    // ^ is the XOR operator.
    if (SimpleTest._isRef(it) ^ SimpleTest._isRef(as)) {
        // One's a reference, one isn't.
        ok = false;
    } else if (!SimpleTest._isRef(it) && !SimpleTest._isRef(as)) {
        // Neither is an object.
        ok = SimpleTest.is(it, as, name);
    } else {
        // We have two objects. Do a deep comparison.
        var stack = [], seen = [];
        if ( SimpleTest._deepCheck(it, as, stack, seen)) {
            ok = SimpleTest.ok(true, name);
        } else {
            ok = SimpleTest.ok(false, name, SimpleTest._formatStack(stack));
        }
    }
    return ok;
};

SimpleTest.typeOf = function (object) {
    var c = Object.prototype.toString.apply(object);
    var name = c.substring(8, c.length - 1);
    if (name != 'Object') return name;
    // It may be a non-core class. Try to extract the class name from
    // the constructor function. This may not work in all implementations.
    if (/function ([^(\s]+)/.test(Function.toString.call(object.constructor))) {
        return RegExp.$1;
    }
    // No idea. :-(
    return name;
};

SimpleTest.isa = function (object, clas) {
    return SimpleTest.typeOf(object) == clas;
};

// Global symbols:
var ok = SimpleTest.ok;
var is = SimpleTest.is;
var isnot = SimpleTest.isnot;
var todo = SimpleTest.todo;
var todo_is = SimpleTest.todo_is;
var todo_isnot = SimpleTest.todo_isnot;
var isDeeply = SimpleTest.isDeeply;

var gOldOnError = window.onerror;
window.onerror = function simpletestOnerror(errorMsg, url, lineNumber) {
  var funcIdentifier = "[SimpleTest/SimpleTest.js, window.onerror] ";

  // Log the message.
  ok(false, funcIdentifier + "An error occurred", errorMsg);
  // There is no Components.stack.caller to log. (See bug 511888.)

  // Call previous handler.
  if (gOldOnError) {
    try {
      // Ignore return value: always run default handler.
      gOldOnError(errorMsg, url, lineNumber);
    } catch (e) {
      // Log the error.
      ok(false, funcIdentifier + "Exception thrown by gOldOnError()", e);
      // Log its stack.
      if (e.stack)
        ok(false, funcIdentifier + "JavaScript error stack:\n" + e.stack);
    }
  }

  if (!SimpleTest._stopOnLoad) {
    // Need to finish() manually here, yet let the test actually end first.
    SimpleTest.executeSoon(SimpleTest.finish);
  }
}
