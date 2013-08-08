/* -*- Mode: C++; tab-width: 40; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Hal.h"
#include "mozilla/HalWakeLock.h"
#include "mozilla/ClearOnShutdown.h"
#include "nsDataHashtable.h"
#include "nsHashKeys.h"

using namespace mozilla::hal;

namespace mozilla {
namespace hal {

WakeLockState
ComputeWakeLockState(int aNumLocks, int aNumHidden)
{
  if (aNumLocks == 0) {
    return WAKE_LOCK_STATE_UNLOCKED;
  } else if (aNumLocks == aNumHidden) {
    return WAKE_LOCK_STATE_HIDDEN;
  } else {
    return WAKE_LOCK_STATE_VISIBLE;
  }
}

} // hal
} // mozilla

namespace mozilla {
namespace hal_impl {

namespace {
struct LockCount {
  PRUint32 numLocks;
  PRUint32 numHidden;
};
}

static int sActiveChildren = 0;
static nsAutoPtr<nsDataHashtable<nsStringHashKey, LockCount> > sLockTable;
static bool sInitialized = false;

static void
Init()
{
  sLockTable = new nsDataHashtable<nsStringHashKey, LockCount>();
  sLockTable->Init();
  ClearOnShutdown(&sLockTable);
  sInitialized = true;
}

void
EnableWakeLockNotifications()
{
  sActiveChildren++;
}

void
DisableWakeLockNotifications()
{
  sActiveChildren--;
}

void
ModifyWakeLock(const nsAString &aTopic,
               hal::WakeLockControl aLockAdjust,
               hal::WakeLockControl aHiddenAdjust)
{
  if (!sInitialized) {
    Init();
  }

  LockCount count;
  count.numLocks = 0;
  count.numHidden = 0;
  sLockTable->Get(aTopic, &count);
  MOZ_ASSERT(count.numLocks >= count.numHidden);
  MOZ_ASSERT(aLockAdjust >= 0 || count.numLocks > 0);
  MOZ_ASSERT(aHiddenAdjust >= 0 || count.numHidden > 0);

  WakeLockState oldState = ComputeWakeLockState(count.numLocks, count.numHidden);

  count.numLocks += aLockAdjust;
  count.numHidden += aHiddenAdjust;
  MOZ_ASSERT(count.numLocks >= count.numHidden);

  if (count.numLocks) {
    sLockTable->Put(aTopic, count);
  } else {
    sLockTable->Remove(aTopic);
  }

  WakeLockState newState = ComputeWakeLockState(count.numLocks, count.numHidden);

  if (sActiveChildren && oldState != newState) {
    WakeLockInformation info;
    info.numLocks() = count.numLocks;
    info.numHidden() = count.numHidden;
    info.topic() = aTopic;
    NotifyWakeLockChange(info);
  }
}

void
GetWakeLockInfo(const nsAString &aTopic, WakeLockInformation *aWakeLockInfo)
{
  if (!sInitialized) {
    Init();
  }

  LockCount count;
  count.numLocks = 0;
  count.numHidden = 0;
  sLockTable->Get(aTopic, &count);

  aWakeLockInfo->numLocks() = count.numLocks;
  aWakeLockInfo->numHidden() = count.numHidden;
  aWakeLockInfo->topic() = aTopic;
}

} // hal_impl
} // mozilla
