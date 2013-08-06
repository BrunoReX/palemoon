/* vim:set ts=2 sw=2 sts=2 et: */
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
 * The Original Code is DevTools test code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  David Dahl <ddahl@mozilla.com>
 *  Patrick Walton <pcwalton@mozilla.com>
 *  Julian Viereck <jviereck@mozilla.com>
 *  Mihai Sucan <mihai.sucan@gmail.com>
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

// Tests that console groups behave properly.

const TEST_URI = "http://example.com/browser/toolkit/components/console/hudservice/tests/browser/test-console.html";

function test() {
  addTab(TEST_URI);
  browser.addEventListener("DOMContentLoaded", testGroups, false);
}

function testGroups() {
  browser.removeEventListener("DOMContentLoaded", testGroups, false);

  openConsole();

  let hudId = HUDService.displaysIndex()[0];

  let HUD = HUDService.hudReferences[hudId];
  let jsterm = HUD.jsterm;
  let outputNode = jsterm.outputNode;

  // We test for one group by testing for zero "new" groups. The
  // "webconsole-new-group" class creates a divider. Thus one group is
  // indicated by zero new groups, two groups are indicated by one new group,
  // and so on.

  let timestamp0 = Date.now();
  jsterm.execute("0");
  is(outputNode.querySelectorAll(".webconsole-new-group").length, 0,
     "no group dividers exist after the first console message");

  jsterm.execute("1");
  let timestamp1 = Date.now();
  if (timestamp1 - timestamp0 < 5000) {
    is(outputNode.querySelectorAll(".webconsole-new-group").length, 0,
       "no group dividers exist after the second console message");
  }

  for (let i = 0; i < outputNode.itemCount; i++) {
    outputNode.getItemAtIndex(i).timestamp = 0;   // a "far past" value
  }

  jsterm.execute("2");
  is(outputNode.querySelectorAll(".webconsole-new-group").length, 1,
     "one group divider exists after the third console message");

  jsterm.clearOutput();
  jsterm.history.splice(0);   // workaround for bug 592552

  finishTest();
}

