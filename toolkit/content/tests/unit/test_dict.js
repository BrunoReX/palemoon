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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Siddharth Agarwal <sid.bugzilla@gmail.com>
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

Components.utils.import("resource://gre/modules/Dict.jsm");

/**
 * Test that a few basic get, set, has and del operations work.
 */
function test_get_set_has_del() {
  let dict = new Dict({foo: "bar"});
  dict.set("baz", 200);
  do_check_eq(dict.get("foo"), "bar");
  do_check_eq(dict.get("baz"), 200);
  do_check_true(dict.has("foo"));
  do_check_true(dict.has("baz"));
  // Now delete the entries
  do_check_true(dict.del("foo"));
  do_check_true(dict.del("baz"));
  do_check_false(dict.has("foo"));
  do_check_false(dict.has("baz"));
  // and make sure del returns false
  do_check_false(dict.del("foo"));
  do_check_false(dict.del("baz"));
}

/**
 * Test that the second parameter of get (default value) works.
 */
function test_get_default() {
  let dict = new Dict();
  do_check_true(dict.get("foo") === undefined);
  do_check_eq(dict.get("foo", "bar"), "bar");
}

/**
 * Test that there are no collisions with builtins.
 */
function test_collisions_with_builtins() {
  let dict = new Dict();
  // First check that a new dictionary doesn't already have builtins.
  do_check_false(dict.has("toString"));
  do_check_false(dict.has("watch"));
  do_check_false(dict.has("__proto__"));

  // Add elements in an attempt to collide with builtins.
  dict.set("toString", "toString");
  dict.set("watch", "watch");
  // This is a little evil. We set __proto__ to an object to try to make it look
  // up the prototype chain.
  dict.set("__proto__", {prototest: "prototest"});

  // Now check that the entries exist.
  do_check_true(dict.has("toString"));
  do_check_true(dict.has("watch"));
  do_check_true(dict.has("__proto__"));
  // ...and that we aren't looking up the prototype chain.
  do_check_false(dict.has("prototest"));
}

/**
 * Test that the "count" property works as expected.
 */
function test_count() {
  let dict = new Dict({foo: "bar"});
  do_check_eq(dict.count, 1);
  dict.set("baz", "quux");
  do_check_eq(dict.count, 2);
  // This shouldn't change the count
  dict.set("baz", "quux2");
  do_check_eq(dict.count, 2);

  do_check_true(dict.del("baz"));
  do_check_eq(dict.count, 1);
  // This shouldn't change the count either
  do_check_false(dict.del("not"));
  do_check_eq(dict.count, 1);
  do_check_true(dict.del("foo"));
  do_check_eq(dict.count, 0);
}

/**
 * Test that the copy function works as expected.
 */
function test_copy() {
  let obj = {};
  let dict1 = new Dict({foo: "bar", baz: obj});
  let dict2 = dict1.copy();
  do_check_eq(dict2.get("foo"), "bar");
  do_check_eq(dict2.get("baz"), obj);
  // Make sure the two update independent of each other.
  dict1.del("foo");
  do_check_false(dict1.has("foo"));
  do_check_true(dict2.has("foo"));
  dict2.set("test", 400);
  do_check_true(dict2.has("test"));
  do_check_false(dict1.has("test"));

  // Check that the copy is shallow and not deep.
  dict1.get("baz").prop = "proptest";
  do_check_eq(dict2.get("baz").prop, "proptest");
}

// This is used by both test_listers and test_iterators.
function _check_lists(keys, values, items) {
  do_check_eq(keys.length, 2);
  do_check_true(keys.indexOf("x") != -1);
  do_check_true(keys.indexOf("y") != -1);

  do_check_eq(values.length, 2);
  do_check_true(values.indexOf("a") != -1);
  do_check_true(values.indexOf("b") != -1);

  // This is a little more tricky -- we need to check that one of the two
  // entries is ["x", "a"] and the other is ["y", "b"].
  do_check_eq(items.length, 2);
  do_check_eq(items[0].length, 2);
  do_check_eq(items[1].length, 2);
  let ix = (items[0][0] == "x") ? 0 : 1;
  let iy = (ix == 0) ? 1 : 0;
  do_check_eq(items[ix][0], "x");
  do_check_eq(items[ix][1], "a");
  do_check_eq(items[iy][0], "y");
  do_check_eq(items[iy][1], "b");
}

/**
 * Test the list functions.
 */
function test_listers() {
  let dict = new Dict({"x": "a", "y": "b"});
  let keys = dict.listkeys();
  let values = dict.listvalues();
  let items = dict.listitems();
  _check_lists(keys, values, items);
}

/**
 * Test the iterator functions.
 */
function test_iterators() {
  let dict = new Dict({"x": "a", "y": "b"});
  // Convert the generators to lists
  let keys = [x for (x in dict.keys)];
  let values = [x for (x in dict.values)];
  let items = [x for (x in dict.items)];
  _check_lists(keys, values, items);
}

/**
 * Test that setting a property throws an exception in strict mode.
 */
function test_set_property_strict() {
  "use strict";
  var dict = new Dict();
  var thrown = false;
  try {
    dict.foo = "bar";
  }
  catch (ex) {
    thrown = true;
  }
  do_check_true(thrown);
}

/**
 * Test that setting a property has no effect in non-strict mode.
 */
function test_set_property_non_strict() {
  let dict = new Dict();
  dict.foo = "bar";
  do_check_false("foo" in dict);
  let realget = dict.get;
  dict.get = "baz";
  do_check_eq(dict.get, realget);
}

var tests = [
  test_get_set_has_del,
  test_get_default,
  test_collisions_with_builtins,
  test_count,
  test_copy,
  test_listers,
  test_iterators,
  test_set_property_strict,
  test_set_property_non_strict,
];

function run_test() {
  for (let [, test] in Iterator(tests))
    test();
}
