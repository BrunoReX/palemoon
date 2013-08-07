/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Initial Developer of the Original Code is
 *   Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Benoit Girard <bgirard@mozilla.com>
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

#include <string>
#ifdef MOZ_INSTRUMENT_EVENT_LOOP
#include "EventTracer.h"
#endif
#include "sampler.h"
#include "nsProfiler.h"
#include "nsMemory.h"

using std::string;

NS_IMPL_ISUPPORTS1(nsProfiler, nsIProfiler)


nsProfiler::nsProfiler()
{
}


NS_IMETHODIMP
nsProfiler::StartProfiler(PRUint32 aInterval, PRUint32 aEntries)
{
  SAMPLER_START(aInterval, aEntries);
#ifdef MOZ_INSTRUMENT_EVENT_LOOP
  mozilla::InitEventTracing();
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsProfiler::StopProfiler()
{
  SAMPLER_STOP();
  return NS_OK;
}

NS_IMETHODIMP
nsProfiler::GetProfile(char **aProfile)
{
  char *profile = SAMPLER_GET_PROFILE();
  if (profile) {
    PRUint32 len = strlen(profile);
    char *profileStr = static_cast<char *>
                         (nsMemory::Clone(profile, (len + 1) * sizeof(char)));
    profileStr[len] = '\0';
    *aProfile = profileStr;
    free(profile);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsProfiler::IsActive(bool *aIsActive)
{
  *aIsActive = SAMPLER_IS_ACTIVE();
  return NS_OK;
}

NS_IMETHODIMP
nsProfiler::GetResponsivenessTimes(PRUint32 *aCount, float **aResult)
{
  unsigned int len = 100;
  const float* times = SAMPLER_GET_RESPONSIVENESS();
  if (!times) {
    *aCount = 0;
    *aResult = nsnull;
    return NS_OK;
  }

  float *fs = static_cast<float *>
                       (nsMemory::Clone(times, len * sizeof(float)));

  *aCount = len;
  *aResult = fs;

  return NS_OK;
}

