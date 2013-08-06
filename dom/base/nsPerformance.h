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
 * The Original Code is implementation of Web Timing draft specification
 * http://dev.w3.org/2006/webapi/WebTiming/
 *
 * The Initial Developer of the Original Code is Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Sergey Novikov <sergeyn@google.com> (original author)
 *   Igor Bazarny <igor.bazarny@gmail.com> (update to match bearly-final spec)
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
#ifndef nsPerformance_h___
#define nsPerformance_h___

#include "nsIDOMPerformance.h"
#include "nsIDOMPerformanceTiming.h"
#include "nsIDOMPerformanceNavigation.h"
#include "nscore.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"

class nsIDocument;
class nsIURI;
class nsDOMNavigationTiming;
class nsITimedChannel;

// Script "performance.timing" object
class nsPerformanceTiming : public nsIDOMPerformanceTiming
{
public:
  nsPerformanceTiming(nsDOMNavigationTiming* aDOMTiming, nsITimedChannel* aChannel);
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMPERFORMANCETIMING
private:
  ~nsPerformanceTiming();
  nsRefPtr<nsDOMNavigationTiming> mDOMTiming;
  nsCOMPtr<nsITimedChannel> mChannel;
};

// Script "performance.navigation" object
class nsPerformanceNavigation : public nsIDOMPerformanceNavigation
{
public:
  nsPerformanceNavigation(nsDOMNavigationTiming* data);
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMPERFORMANCENAVIGATION
private:
  ~nsPerformanceNavigation();
  nsRefPtr<nsDOMNavigationTiming> mData;
};

// Script "performance" object
class nsPerformance : public nsIDOMPerformance
{
public:
  nsPerformance(nsDOMNavigationTiming* aDOMTiming, nsITimedChannel* aChannel);

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMPERFORMANCE

private:
  ~nsPerformance();

  nsRefPtr<nsDOMNavigationTiming> mDOMTiming;
  nsCOMPtr<nsITimedChannel> mChannel;
  nsCOMPtr<nsIDOMPerformanceTiming> mTiming;
  nsCOMPtr<nsIDOMPerformanceNavigation> mNavigation;
};

#endif /* nsPerformance_h___ */

