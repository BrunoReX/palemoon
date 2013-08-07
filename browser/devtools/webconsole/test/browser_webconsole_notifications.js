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

const TEST_URI = "http://example.com/browser/browser/devtools/webconsole/test//test-console.html";

function test() {
  observer.init();
  addTab(TEST_URI);
  browser.addEventListener("DOMContentLoaded", onLoad, false);
}

function webConsoleCreated(aID)
{
  Services.obs.removeObserver(observer, "web-console-created");
  executeSoon(function (){
    ok(HUDService.hudReferences[aID], "We have a hud reference");
    let console = browser.contentWindow.wrappedJSObject.console;
    console.log("adding a log message");
  });
}

function webConsoleDestroyed(aID)
{
  Services.obs.removeObserver(observer, "web-console-destroyed");
  ok(!HUDService.hudReferences[aID], "We do not have a hud reference");
  finishTest();
}

function webConsoleMessage(aID, aNodeID)
{
  Services.obs.removeObserver(observer, "web-console-message-created");
  ok(aID, "we have a console ID");
  ok(typeof aNodeID == 'string', "message node id is not null");
  closeConsole();
}

let observer = {

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver]),

  observe: function observe(aSubject, aTopic, aData)
  {
    aSubject = aSubject.QueryInterface(Ci.nsISupportsString);

    switch(aTopic) {
      case "web-console-created":
        webConsoleCreated(aSubject);
        break;
      case "web-console-destroyed":
        webConsoleDestroyed(aSubject);
        break;
      case "web-console-message-created":
        webConsoleMessage(aSubject, aData);
        break;
      default:
        break;
    }
  },

  init: function init()
  {
    Services.obs.addObserver(this, "web-console-created", false);
    Services.obs.addObserver(this, "web-console-destroyed", false);
    Services.obs.addObserver(this, "web-console-message-created", false);
  }
};

function onLoad() {
  browser.removeEventListener("DOMContentLoaded", onLoad, false);
  openConsole();
}
