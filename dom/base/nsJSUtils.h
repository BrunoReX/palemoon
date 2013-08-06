/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef nsJSUtils_h__
#define nsJSUtils_h__

/**
 * This is not a generated file. It contains common utility functions 
 * invoked from the JavaScript code generated from IDL interfaces.
 * The goal of the utility functions is to cut down on the size of
 * the generated code itself.
 */

#include "nsISupports.h"
#include "jsapi.h"
#include "nsString.h"

class nsIDOMEventListener;
class nsIScriptContext;
class nsIScriptGlobalObject;
class nsIPrincipal;

class nsJSUtils
{
public:
  static JSBool GetCallingLocation(JSContext* aContext, const char* *aFilename,
                                   PRUint32* aLineno);

  static nsIScriptGlobalObject *GetStaticScriptGlobal(JSContext* aContext,
                                                      JSObject* aObj);

  static nsIScriptContext *GetStaticScriptContext(JSContext* aContext,
                                                  JSObject* aObj);

  static nsIScriptGlobalObject *GetDynamicScriptGlobal(JSContext *aContext);

  static nsIScriptContext *GetDynamicScriptContext(JSContext *aContext);

  /**
   * Retrieve the outer window ID based on the given JSContext.
   *
   * @param JSContext aContext
   *        The JSContext from which you want to find the outer window ID.
   *
   * @returns PRUint64 the outer window ID.
   */
  static PRUint64 GetCurrentlyRunningCodeWindowID(JSContext *aContext);
};


class nsDependentJSString : public nsDependentString
{
public:
  /**
   * In the case of string ids, getting the string's chars is infallible, so
   * the dependent string can be constructed directly.
   */
  explicit nsDependentJSString(jsid id)
    : nsDependentString(JS_GetInternedStringChars(JSID_TO_STRING(id)),
                        JS_GetStringLength(JSID_TO_STRING(id)))
  {
  }

  /**
   * For all other strings, the nsDependentJSString object should be default
   * constructed, which leaves it empty (this->IsEmpty()), and initialized with
   * one of the fallible init() methods below.
   */

  nsDependentJSString()
  {
  }

  JSBool init(JSContext* aContext, JSString* str)
  {
      size_t length;
      const jschar* chars = JS_GetStringCharsZAndLength(aContext, str, &length);
      if (!chars)
          return JS_FALSE;

      NS_ASSERTION(IsEmpty(), "init() on initialized string");
      nsDependentString* base = this;
      new(base) nsDependentString(chars, length);
      return JS_TRUE;
  }

  JSBool init(JSContext* aContext, const jsval &v)
  {
      return init(aContext, JSVAL_TO_STRING(v));
  }

  ~nsDependentJSString()
  {
  }
};

#endif /* nsJSUtils_h__ */
