/* -*-  Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
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
 * The Original Code is Startup Cache.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Foundation <http://www.mozilla.org/>.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Benedict Hsieh <bhsieh@mozilla.com>
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

#include <string.h>

#include "nscore.h"
#include "pratom.h"
#include "prmem.h"
#include "prio.h"
#include "plstr.h"
#include "prlog.h"

#include "nsID.h"
#include "nsIComponentManager.h"
#include "nsIServiceManager.h"
#include "nsCOMPtr.h"
#include "nsIModule.h"
#include "mozilla/ModuleUtils.h"
#include "mozilla/scache/StartupCache.h"

using namespace mozilla::scache;

// XXX Need help with guard for ENABLE_TEST
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(StartupCacheWrapper,
                                         StartupCacheWrapper::GetSingleton)
NS_DEFINE_NAMED_CID(NS_STARTUPCACHE_CID);

static const mozilla::Module::CIDEntry kStartupCacheCIDs[] = {
    { &kNS_STARTUPCACHE_CID, false, NULL, StartupCacheWrapperConstructor },
    { NULL }
};

static const mozilla::Module::ContractIDEntry kStartupCacheContracts[] = {
    { "@mozilla.org/startupcache/cache;1", &kNS_STARTUPCACHE_CID },
    { NULL }
};

static const mozilla::Module kStartupCacheModule = {
    mozilla::Module::kVersion,
    kStartupCacheCIDs,
    kStartupCacheContracts,
    NULL,
    NULL,
    NULL,
    NULL
};

NSMODULE_DEFN(StartupCacheModule) = &kStartupCacheModule;
