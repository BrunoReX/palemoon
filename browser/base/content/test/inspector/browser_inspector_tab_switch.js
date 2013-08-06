/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Inspector Tab Switch Tests.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Rob Campbell <rcampbell@mozilla.com>
 *   Mihai Șucan <mihai.sucan@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

let div;
let tab1;
let tab2;
let tab1window;

function inspectorTabOpen1()
{
  ok(window.InspectorUI, "InspectorUI variable exists");
  ok(!InspectorUI.inspecting, "Inspector is not highlighting");
  ok(InspectorStore.isEmpty(), "InspectorStore is empty");

  Services.obs.addObserver(inspectorUIOpen1,
    INSPECTOR_NOTIFICATIONS.OPENED, false);
  InspectorUI.openInspectorUI();
}

function inspectorUIOpen1()
{
  Services.obs.removeObserver(inspectorUIOpen1,
    INSPECTOR_NOTIFICATIONS.OPENED, false);

  // Make sure the inspector is open.
  ok(InspectorUI.inspecting, "Inspector is highlighting");
  ok(InspectorUI.isTreePanelOpen, "Inspector Tree Panel is open");
  ok(!InspectorStore.isEmpty(), "InspectorStore is not empty");
  is(InspectorStore.length, 1, "InspectorStore.length = 1");

  // Highlight a node.
  div = content.document.getElementsByTagName("div")[0];
  InspectorUI.inspectNode(div);
  is(InspectorUI.selection, div, "selection matches the div element");

  // Open the second tab.
  tab2 = gBrowser.addTab();
  gBrowser.selectedTab = tab2;

  gBrowser.selectedBrowser.addEventListener("load", function(evt) {
    gBrowser.selectedBrowser.removeEventListener(evt.type, arguments.callee,
      true);
    waitForFocus(inspectorTabOpen2, content);
  }, true);

  content.location = "data:text/html,<p>tab 2: the inspector should close now";
}

function inspectorTabOpen2()
{
  // Make sure the inspector is closed.
  ok(!InspectorUI.inspecting, "Inspector is not highlighting");
  ok(!InspectorUI.isPanelOpen, "Inspector Tree Panel is closed");
  is(InspectorStore.length, 1, "InspectorStore.length = 1");

  // Activate the inspector again.
  executeSoon(function() {
    Services.obs.addObserver(inspectorUIOpen2,
      INSPECTOR_NOTIFICATIONS.OPENED, false);
    InspectorUI.openInspectorUI();
  });
}

function inspectorUIOpen2()
{
  Services.obs.removeObserver(inspectorUIOpen2,
    INSPECTOR_NOTIFICATIONS.OPENED, false);

  // Make sure the inspector is open.
  ok(InspectorUI.inspecting, "Inspector is highlighting");
  ok(InspectorUI.isTreePanelOpen, "Inspector Tree Panel is open");
  is(InspectorStore.length, 2, "InspectorStore.length = 2");

  // Disable highlighting.
  InspectorUI.toggleInspection();
  ok(!InspectorUI.inspecting, "Inspector is not highlighting");

  // Switch back to tab 1.
  executeSoon(function() {
    Services.obs.addObserver(inspectorFocusTab1,
      INSPECTOR_NOTIFICATIONS.OPENED, false);
    gBrowser.selectedTab = tab1;
  });
}

function inspectorFocusTab1()
{
  Services.obs.removeObserver(inspectorFocusTab1,
    INSPECTOR_NOTIFICATIONS.OPENED, false);

  // Make sure the inspector is still open.
  ok(InspectorUI.inspecting, "Inspector is highlighting");
  ok(InspectorUI.isTreePanelOpen, "Inspector Tree Panel is open");
  is(InspectorStore.length, 2, "InspectorStore.length = 2");
  is(InspectorUI.selection, div, "selection matches the div element");

  // Switch back to tab 2.
  Services.obs.addObserver(inspectorFocusTab2,
    INSPECTOR_NOTIFICATIONS.OPENED, false);
  gBrowser.selectedTab = tab2;
}

function inspectorFocusTab2()
{
  Services.obs.removeObserver(inspectorFocusTab2,
    INSPECTOR_NOTIFICATIONS.OPENED, false);

  // Make sure the inspector is still open.
  ok(!InspectorUI.inspecting, "Inspector is not highlighting");
  ok(InspectorUI.isTreePanelOpen, "Inspector Tree Panel is open");
  is(InspectorStore.length, 2, "InspectorStore.length = 2");
  isnot(InspectorUI.selection, div, "selection does not match the div element");

  // Remove tab 1.
  tab1window = gBrowser.getBrowserForTab(tab1).contentWindow;
  tab1window.addEventListener("pagehide", inspectorTabUnload1, false);
  gBrowser.removeTab(tab1);
}

function inspectorTabUnload1(evt)
{
  tab1window.removeEventListener(evt.type, arguments.callee, false);
  tab1window = tab1 = tab2 = div = null;

  // Make sure the Inspector is still open and that the state is correct.
  ok(!InspectorUI.inspecting, "Inspector is not highlighting");
  ok(InspectorUI.isTreePanelOpen, "Inspector Tree Panel is open");
  is(InspectorStore.length, 1, "InspectorStore.length = 1");

  InspectorUI.closeInspectorUI();
  gBrowser.removeCurrentTab();
  finish();
}

function test()
{
  waitForExplicitFinish();

  tab1 = gBrowser.addTab();
  gBrowser.selectedTab = tab1;
  gBrowser.selectedBrowser.addEventListener("load", function(evt) {
    gBrowser.selectedBrowser.removeEventListener(evt.type, arguments.callee,
      true);
    waitForFocus(inspectorTabOpen1, content);
  }, true);

  content.location = "data:text/html,<p>tab switching tests for inspector" +
    "<div>tab 1</div>";
}

