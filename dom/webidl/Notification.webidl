/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://notifications.spec.whatwg.org/
 *
 * Copyright:
 * To the extent possible under law, the editors have waived all copyright and
 * related or neighboring rights to this work.
 */

[PrefControlled, Constructor(DOMString title, optional NotificationOptions options)]
interface Notification : EventTarget {
  [GetterThrows]
  static readonly attribute NotificationPermission permission;

  [Throws]
  static void requestPermission(optional NotificationPermissionCallback permissionCallback);

  [SetterThrows]
  attribute EventHandler onclick;

  [SetterThrows]
  attribute EventHandler onshow;

  [SetterThrows]
  attribute EventHandler onerror;

  [SetterThrows]
  attribute EventHandler onclose;

  void close();
};

dictionary NotificationOptions {
  NotificationDirection dir = "auto";
  DOMString lang = "";
  DOMString body = "";
  DOMString tag;
  DOMString icon = "";
};

enum NotificationPermission {
  "default",
  "denied",
  "granted"
};

callback NotificationPermissionCallback = void (NotificationPermission permission);

enum NotificationDirection {
  "auto",
  "ltr",
  "rtl"
};

