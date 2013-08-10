/* Any copyright", " is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that various editors work as expected.  Also checks
 * that the various changes are properly undoable and redoable.
 * For each step in the test, we:
 * - Run the setup for that test (if any)
 * - Check that the node we're editing is as we expect
 * - Make the change, check that the change was made as we expect
 * - Undo the change, check that the node is back in its original state
 * - Redo the change, check that the node change was made again correctly.
 *
 * This test mostly tries to verify that the editor makes changes to the
 * underlying DOM, not that the UI updates - UI updates are based on
 * underlying DOM changes, and the mutation tests should cover those cases.
 */

function test() {
  let tempScope = {}
  Cu.import("resource:///modules/devtools/CssRuleView.jsm", tempScope);
  let inplaceEditor = tempScope._getInplaceEditorForSpan;

  waitForExplicitFinish();

  // Will hold the doc we're viewing
  let doc;

  // Holds the MarkupTool object we're testing.
  let markup;

  /**
   * Edit a given editableField
   */
  function editField(aField, aValue)
  {
    aField.focus();
    EventUtils.sendKey("return");
    let input = inplaceEditor(aField).input;
    input.value = aValue;
    input.blur();
  }

  function assertAttributes(aElement, aAttributes)
  {
    let attrs = Object.getOwnPropertyNames(aAttributes);
    is(aElement.attributes.length, attrs.length, "Node has the correct number of attributes");
    for (let attr of attrs) {
      is(aElement.getAttribute(attr), aAttributes[attr], "Node has the correct " + attr + " attribute.");
    }
  }

  // All the mutation types we want to test.
  let edits = [
    // Change an attribute
    {
      before: function() {
        assertAttributes(doc.querySelector("#node1"), {
          id: "node1",
          class: "node1"
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node1")).editor;
        let attr = editor.attrs["class"].querySelector(".editable");
        editField(attr, 'class="changednode1"');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node1"), {
          id: "node1",
          class: "changednode1"
        });
      }
    },

    // Try change an attribute to a badly formed string
    {
      before: function() {
        assertAttributes(doc.querySelector("#node22"), {
          id: "node22",
          class: "unchanged"
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node22")).editor;
        let attr = editor.attrs["class"].querySelector(".editable");
        editField(attr, 'class="""');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node22"), {
          id: "node22",
          class: "unchanged"
        });
      }
    },


    // Remove an attribute
    {
      before: function() {
        assertAttributes(doc.querySelector("#node4"), {
          id: "node4",
          class: "node4"
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node4")).editor;
        let attr = editor.attrs["class"].querySelector(".editable");
        editField(attr, '');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node4"), {
          id: "node4",
        });
      }
    },

    // Add an attribute by clicking the empty space after a node
    {
      before: function() {
        assertAttributes(doc.querySelector("#node14"), {
          id: "node14",
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node14")).editor;
        let attr = editor.newAttr;
        editField(attr, 'class="newclass" style="color:green"');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node14"), {
          id: "node14",
          class: "newclass",
          style: "color:green"
        });
      }
    },

    // Try add a badly formed attribute by clicking the empty space after a node
    {
      before: function() {
        assertAttributes(doc.querySelector("#node23"), {
          id: "node23",
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node23")).editor;
        let attr = editor.newAttr;
        editField(attr, 'class="newclass" style="""');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node23"), {
          id: "node23",
        });
      }
    },

    // Add attributes by adding to an existing attribute's entry
    {
      setup: function() {
        InspectorUI.select(doc.querySelector("#node18"), true, true, true);
      },
      before: function() {
        assertAttributes(doc.querySelector("#node18"), {
          id: "node18",
        });
        is(InspectorUI.highlighter.nodeInfo.classesBox.textContent, "", "No classes in the infobar before edit.");
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node18")).editor;
        let attr = editor.attrs["id"].querySelector(".editable");
        editField(attr, attr.textContent + ' class="newclass" style="color:green"');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node18"), {
          id: "node18",
          class: "newclass",
          style: "color:green"
        });
        is(InspectorUI.highlighter.nodeInfo.classesBox.textContent, ".newclass", "Correct classes in the infobar after edit.");
      }
    },

    // Try add attributes by adding to an existing attribute's entry
    {
      before: function() {
        assertAttributes(doc.querySelector("#node24"), {
          id: "node24",
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node24")).editor;
        let attr = editor.attrs["id"].querySelector(".editable");
        editField(attr, attr.textContent + ' class="""');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node24"), {
          id: "node24",
        });
      }
    },

    // Remove an element with the delete key
    {
      before: function() {
        ok(!!doc.querySelector("#node18"), "Node 18 should exist.");
      },
      execute: function() {
        markup.selectNode(doc.querySelector("#node18"));
        EventUtils.sendKey("delete");
      },
      after: function() {
        ok(!doc.querySelector("#node18"), "Node 18 should not exist.")
      }
    },

    // Edit text
    {
      before: function() {
        let node = doc.querySelector('.node6').firstChild;
        is(node.nodeValue, "line6", "Text should be unchanged");
      },
      execute: function() {
        let node = doc.querySelector('.node6').firstChild;
        let editor = markup.getContainer(node).editor;
        let field = editor.elt.querySelector("pre");
        editField(field, "New text");
      },
      after: function() {
        let node = doc.querySelector('.node6').firstChild;
        is(node.nodeValue, "New text", "Text should be changed.");
      },
    },

    // Edit the tag name
    {
      setup: function() {
        markup.selectNode(doc.querySelector("#retag-me"));
      },
      before: function() {
        let node = doc.querySelector("#retag-me");
        let container = markup.getContainer(node);
        is(node.tagName, "DIV", "retag-me should be a div.");
        ok(container.selected, "retag-me should be selected.");
        ok(container.expanded, "retag-me should be expanded.");
        is(doc.querySelector("#retag-me-2").parentNode, node, "retag-me-2 should be a child of the old element.");
      },
      execute: function() {
        let node = doc.querySelector("#retag-me");
        let editor = markup.getContainer(node).editor;
        let field = editor.tag;
        editField(field, "p");
      },
      after: function() {
        let node = doc.querySelector("#retag-me");
        let container = markup.getContainer(node);
        is(node.tagName, "P", "retag-me should be a p.");
        ok(container.selected, "retag-me should be selected.");
        ok(container.expanded, "retag-me should be expanded.");
        is(doc.querySelector("#retag-me-2").parentNode, node, "retag-me-2 should be a child of the new element.");
      }
    },
  ];

  // Create the helper tab for parsing...
  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function onload() {
    gBrowser.selectedBrowser.removeEventListener("load", onload, true);
    doc = content.document;
    waitForFocus(setupTest, content);
  }, true);
  content.location = "http://mochi.test:8888/browser/browser/devtools/markupview/test/browser_inspector_markup_edit.html";

  function setupTest() {
    Services.obs.addObserver(runTests, InspectorUI.INSPECTOR_NOTIFICATIONS.OPENED, false);
    InspectorUI.toggleInspectorUI();
  }

  function runTests() {
    Services.obs.removeObserver(runTests, InspectorUI.INSPECTOR_NOTIFICATIONS.OPENED);
    InspectorUI.currentInspector.once("markuploaded", startTests);
    InspectorUI.select(doc.body, true, true, true);
    InspectorUI.stopInspecting();
    InspectorUI.toggleHTMLPanel();
  }

  function startTests() {
    let startNode = doc.documentElement.cloneNode();
    markup = InspectorUI.currentInspector.markup;
    markup.expandAll();
    for (let step of edits) {
      if (step.setup) {
        step.setup();
      }
      step.before();
      step.execute();
      step.after();
      ok(markup.undo.canUndo(), "Should be able to undo.");
      markup.undo.undo();
      step.before();
      ok(markup.undo.canRedo(), "Should be able to redo.");
      markup.undo.redo();
      step.after();
    }

    while (markup.undo.canUndo()) {
      markup.undo.undo();
    }
    // By now we should have a healthy undo stack, clear it out and we should be back where
    // we started.
    ok(doc.documentElement.isEqualNode(startNode), "Clearing the undo stack should leave us where we started.");
    Services.obs.addObserver(finishUp, InspectorUI.INSPECTOR_NOTIFICATIONS.CLOSED, false);
    InspectorUI.closeInspectorUI();
  }

  function finishUp() {
    Services.obs.removeObserver(finishUp, InspectorUI.INSPECTOR_NOTIFICATIONS.CLOSED);
    doc = null;
    gBrowser.removeCurrentTab();
    finish();
  }
}
