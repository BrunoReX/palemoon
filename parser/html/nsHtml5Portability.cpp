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
 * The Original Code is HTML Parser C++ Translator code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2008-2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Henri Sivonen <hsivonen@iki.fi>
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

#include "prtypes.h"
#include "nsIAtom.h"
#include "nsString.h"
#include "jArray.h"
#include "nsHtml5Portability.h"

nsIAtom*
nsHtml5Portability::newLocalNameFromBuffer(PRUnichar* buf, PRInt32 offset, PRInt32 length)
{
  NS_ASSERTION(!offset, "The offset should always be zero here.");
  return NS_NewAtom(nsDependentSubstring(buf, buf + length));
}

nsString*
nsHtml5Portability::newStringFromBuffer(PRUnichar* buf, PRInt32 offset, PRInt32 length)
{
  return new nsString(buf + offset, length);
}

nsString*
nsHtml5Portability::newEmptyString()
{
  return new nsString();
}

nsString*
nsHtml5Portability::newStringFromLiteral(const char* literal)
{
  nsString* str = new nsString();
  str->AssignASCII(literal);
  return str;
}

jArray<PRUnichar,PRInt32>
nsHtml5Portability::newCharArrayFromLocal(nsIAtom* local)
{
  nsAutoString temp;
  local->ToString(temp);
  PRInt32 len = temp.Length();
  jArray<PRUnichar,PRInt32> arr = jArray<PRUnichar,PRInt32>(len);
  memcpy(arr, temp.BeginReading(), len * sizeof(PRUnichar));
  return arr;
}

jArray<PRUnichar,PRInt32>
nsHtml5Portability::newCharArrayFromString(nsString* string)
{
  PRInt32 len = string->Length();
  jArray<PRUnichar,PRInt32> arr = jArray<PRUnichar,PRInt32>(len);
  memcpy(arr, string->BeginReading(), len * sizeof(PRUnichar));
  return arr;
}

void
nsHtml5Portability::releaseString(nsString* str)
{
  delete str;
}

void
nsHtml5Portability::retainLocal(nsIAtom* local)
{
  NS_IF_ADDREF(local);
}

void
nsHtml5Portability::releaseLocal(nsIAtom* local)
{
  NS_IF_RELEASE(local);
}

void
nsHtml5Portability::retainElement(nsIContent* element)
{
  NS_IF_ADDREF(element);
}

void
nsHtml5Portability::releaseElement(nsIContent* element)
{
  NS_IF_RELEASE(element);
}

PRBool
nsHtml5Portability::localEqualsBuffer(nsIAtom* local, PRUnichar* buf, PRInt32 offset, PRInt32 length)
{
  return local->Equals(nsDependentSubstring(buf + offset, buf + offset + length));
}

PRBool
nsHtml5Portability::lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(const char* lowerCaseLiteral, nsString* string)
{
  if (!string) {
    return PR_FALSE;
  }
  const char* litPtr = lowerCaseLiteral;
  const PRUnichar* strPtr = string->BeginReading();
  const PRUnichar* end = string->EndReading();
  PRUnichar litChar;
  while ((litChar = *litPtr)) {
    NS_ASSERTION(!(litChar >= 'A' && litChar <= 'Z'), "Literal isn't in lower case.");
    if (strPtr == end) {
      return PR_FALSE;
    }
    PRUnichar strChar = *strPtr;
    if (strChar >= 'A' && strChar <= 'Z') {
      strChar += 0x20;
    }
    if (litChar != strChar) {
      return PR_FALSE;
    }
    ++litPtr;
    ++strPtr;
  }
  return PR_TRUE;
}

PRBool
nsHtml5Portability::lowerCaseLiteralEqualsIgnoreAsciiCaseString(const char* lowerCaseLiteral, nsString* string)
{
  if (!string) {
    return PR_FALSE;
  }
  return string->LowerCaseEqualsASCII(lowerCaseLiteral);
}

PRBool
nsHtml5Portability::literalEqualsString(const char* literal, nsString* string)
{
  if (!string) {
    return PR_FALSE;
  }
  return string->EqualsASCII(literal);
}

jArray<PRUnichar,PRInt32>
nsHtml5Portability::isIndexPrompt()
{
  // XXX making this localizable is bug 500631
  const char* literal = "This is a searchable index. Insert your search keywords here: ";
  jArray<PRUnichar,PRInt32> arr = jArray<PRUnichar,PRInt32>(62);
  for (PRInt32 i = 0; i < 62; ++i) {
    arr[i] = literal[i];
  }
  return arr;
}

void
nsHtml5Portability::initializeStatics()
{
}

void
nsHtml5Portability::releaseStatics()
{
}
