#!/usr/bin/env node
/* vim:set ts=2 sw=2 sts=2 et tw=80:
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is the Source Editor component.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mihai Sucan <mihai.sucan@gmail.com> (original author)
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
 * ***** END LICENSE BLOCK *****/

var copy = require('dryice').copy;

const ORION_EDITOR = "org.eclipse.orion.client.editor/web";

var js_src = copy.createDataObject();

copy({
  source: [
    ORION_EDITOR + "/orion/textview/global.js",
    ORION_EDITOR + "/orion/textview/eventTarget.js",
    ORION_EDITOR + "/orion/editor/regex.js",
    ORION_EDITOR + "/orion/textview/keyBinding.js",
    ORION_EDITOR + "/orion/textview/rulers.js",
    ORION_EDITOR + "/orion/textview/undoStack.js",
    ORION_EDITOR + "/orion/textview/textModel.js",
    ORION_EDITOR + "/orion/textview/annotations.js",
    ORION_EDITOR + "/orion/textview/tooltip.js",
    ORION_EDITOR + "/orion/textview/textView.js",
    ORION_EDITOR + "/orion/textview/textDND.js",
    ORION_EDITOR + "/orion/editor/htmlGrammar.js",
    ORION_EDITOR + "/orion/editor/textMateStyler.js",
    ORION_EDITOR + "/examples/textview/textStyler.js",
  ],
  dest: js_src,
});

copy({
    source: js_src,
    dest: "orion.js",
});

var css_src = copy.createDataObject();

copy({
  source: [
    ORION_EDITOR + "/orion/textview/textview.css",
    ORION_EDITOR + "/orion/textview/rulers.css",
    ORION_EDITOR + "/orion/textview/annotations.css",
    ORION_EDITOR + "/examples/textview/textstyler.css",
    ORION_EDITOR + "/examples/editor/htmlStyles.css",
  ],
  dest: css_src,
});

copy({
    source: css_src,
    dest: "orion.css",
});

