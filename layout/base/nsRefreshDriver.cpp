/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set shiftwidth=2 tabstop=8 autoindent cindent expandtab: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Code to notify things that animate before a refresh, at an appropriate
 * refresh rate.  (Perhaps temporary, until replaced by compositor.)
 *
 * Chrome and each tab have their own RefreshDriver, which in turn
 * hooks into one of a few global timer based on RefreshDriverTimer,
 * defined below.  There are two main global timers -- one for active
 * animations, and one for inactive ones.  These are implemented as
 * subclasses of RefreshDriverTimer; see below for a description of
 * their implementations.  In the future, additional timer types may
 * implement things like blocking on vsync.
 */

#ifdef XP_WIN
#include <windows.h>
// mmsystem isn't part of WIN32_LEAN_AND_MEAN, so we have
// to manually include it
#include <mmsystem.h>

#include <dwmapi.h>
typedef HRESULT (WINAPI*DwmGetCompositionTimingInfoProc)(HWND hWnd, DWM_TIMING_INFO *info);
#endif

#include "mozilla/Util.h"

#include "nsRefreshDriver.h"
#include "nsITimer.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsComponentManagerUtils.h"
#include "prlog.h"
#include "nsAutoPtr.h"
#include "nsCSSFrameConstructor.h"
#include "nsIDocument.h"
#include "nsGUIEvent.h"
#include "nsEventDispatcher.h"
#include "jsapi.h"
#include "nsContentUtils.h"
#include "nsCxPusher.h"
#include "mozilla/Preferences.h"
#include "nsViewManager.h"
#include "GeckoProfiler.h"
#include "nsNPAPIPluginInstance.h"
#include "nsPerformance.h"
#include "mozilla/dom/WindowBinding.h"

using mozilla::TimeStamp;
using mozilla::TimeDuration;

using namespace mozilla;

#ifdef PR_LOGGING
static PRLogModuleInfo *gLog = nullptr;
#define LOG(...) PR_LOG(gLog, PR_LOG_NOTICE, (__VA_ARGS__))
#else
#define LOG(...) do { } while(0)
#endif

#define DEFAULT_FRAME_RATE 60
#define DEFAULT_THROTTLED_FRAME_RATE 1
// after 10 minutes, stop firing off inactive timers
#define DEFAULT_INACTIVE_TIMER_DISABLE_SECONDS 600

namespace mozilla {

/*
 * The base class for all global refresh driver timers.  It takes care
 * of managing the list of refresh drivers attached to them and
 * provides interfaces for querying/setting the rate and actually
 * running a timer 'Tick'.  Subclasses must implement StartTimer(),
 * StopTimer(), and ScheduleNextTick() -- the first two just
 * start/stop whatever timer mechanism is in use, and ScheduleNextTick
 * is called at the start of the Tick() implementation to set a time
 * for the next tick.
 */
class RefreshDriverTimer {
public:
  /*
   * aRate -- the delay, in milliseconds, requested between timer firings
   */
  RefreshDriverTimer(double aRate)
  {
    SetRate(aRate);
  }

  virtual ~RefreshDriverTimer()
  {
    NS_ASSERTION(mRefreshDrivers.Length() == 0, "Should have removed all refresh drivers from here by now!");
  }

  virtual void AddRefreshDriver(nsRefreshDriver* aDriver)
  {
    LOG("[%p] AddRefreshDriver %p", this, aDriver);

    NS_ASSERTION(!mRefreshDrivers.Contains(aDriver), "AddRefreshDriver for a refresh driver that's already in the list!");
    mRefreshDrivers.AppendElement(aDriver);

    if (mRefreshDrivers.Length() == 1) {
      StartTimer();
    }
  }

  virtual void RemoveRefreshDriver(nsRefreshDriver* aDriver)
  {
    LOG("[%p] RemoveRefreshDriver %p", this, aDriver);

    NS_ASSERTION(mRefreshDrivers.Contains(aDriver), "RemoveRefreshDriver for a refresh driver that's not in the list!");
    mRefreshDrivers.RemoveElement(aDriver);

    if (mRefreshDrivers.Length() == 0) {
      StopTimer();
    }
  }

  double GetRate() const
  {
    return mRateMilliseconds;
  }

  // will take effect at next timer tick
  virtual void SetRate(double aNewRate)
  {
    mRateMilliseconds = aNewRate;
    mRateDuration = TimeDuration::FromMilliseconds(mRateMilliseconds);
  }

  TimeStamp MostRecentRefresh() const { return mLastFireTime; }
  int64_t MostRecentRefreshEpochTime() const { return mLastFireEpoch; }

protected:
  virtual void StartTimer() = 0;
  virtual void StopTimer() = 0;
  virtual void ScheduleNextTick(TimeStamp aNowTime) = 0;

  /*
   * Actually runs a tick, poking all the attached RefreshDrivers.
   * Grabs the "now" time via JS_Now and TimeStamp::Now().
   */
  void Tick()
  {
    int64_t jsnow = JS_Now();
    TimeStamp now = TimeStamp::Now();

    ScheduleNextTick(now);

    mLastFireEpoch = jsnow;
    mLastFireTime = now;

    LOG("[%p] ticking drivers...", this);
    nsTArray<nsRefPtr<nsRefreshDriver> > drivers(mRefreshDrivers);
    for (size_t i = 0; i < drivers.Length(); ++i) {
      // don't poke this driver if it's in test mode
      if (drivers[i]->IsTestControllingRefreshesEnabled()) {
        continue;
      }

      TickDriver(drivers[i], jsnow, now);
    }
    LOG("[%p] done.", this);
  }

  static void TickDriver(nsRefreshDriver* driver, int64_t jsnow, TimeStamp now)
  {
    LOG(">> TickDriver: %p (jsnow: %lld)", driver, jsnow);
    driver->Tick(jsnow, now);
  }

  double mRateMilliseconds;
  TimeDuration mRateDuration;

  int64_t mLastFireEpoch;
  TimeStamp mLastFireTime;
  TimeStamp mTargetTime;

  nsTArray<nsRefPtr<nsRefreshDriver> > mRefreshDrivers;

  // useful callback for nsITimer-based derived classes, here
  // bacause of c++ protected shenanigans
  static void TimerTick(nsITimer* aTimer, void* aClosure)
  {
    RefreshDriverTimer *timer = static_cast<RefreshDriverTimer*>(aClosure);
    timer->Tick();
  }
};

/*
 * A RefreshDriverTimer that uses a nsITimer as the underlying timer.  Note that
 * this is a ONE_SHOT timer, not a repeating one!  Subclasses are expected to
 * implement ScheduleNextTick and intelligently calculate the next time to tick,
 * and to reset mTimer.  Using a repeating nsITimer gets us into a lot of pain
 * with its attempt at intelligent slack removal and such, so we don't do it.
 */
class SimpleTimerBasedRefreshDriverTimer :
    public RefreshDriverTimer
{
public:
  SimpleTimerBasedRefreshDriverTimer(double aRate)
    : RefreshDriverTimer(aRate)
  {
    mTimer = do_CreateInstance(NS_TIMER_CONTRACTID);
  }

  virtual ~SimpleTimerBasedRefreshDriverTimer()
  {
    StopTimer();
  }

protected:

  virtual void StartTimer()
  {
    // pretend we just fired, and we schedule the next tick normally
    mLastFireEpoch = JS_Now();
    mLastFireTime = TimeStamp::Now();

    mTargetTime = mLastFireTime + mRateDuration;

    uint32_t delay = static_cast<uint32_t>(mRateMilliseconds);
    mTimer->InitWithFuncCallback(TimerTick, this, delay, nsITimer::TYPE_ONE_SHOT);
  }

  virtual void StopTimer()
  {
    mTimer->Cancel();
  }

  nsRefPtr<nsITimer> mTimer;
};

/*
 * PreciseRefreshDriverTimer schedules ticks based on the current time
 * and when the next tick -should- be sent if we were hitting our
 * rate.  It always schedules ticks on multiples of aRate -- meaning that
 * if some execution takes longer than an alloted slot, the next tick
 * will be delayed instead of triggering instantly.  This might not be
 * desired -- there's an #if 0'd block below that we could put behind
 * a pref to control this behaviour.
 */
class PreciseRefreshDriverTimer :
    public SimpleTimerBasedRefreshDriverTimer
{
public:
  PreciseRefreshDriverTimer(double aRate)
    : SimpleTimerBasedRefreshDriverTimer(aRate)
  {
  }

protected:
  virtual void ScheduleNextTick(TimeStamp aNowTime)
  {
    // The number of (whole) elapsed intervals between the last target
    // time and the actual time.  We want to truncate the double down
    // to an int number of intervals.
    int numElapsedIntervals = static_cast<int>((aNowTime - mTargetTime) / mRateDuration);

    if (numElapsedIntervals < 0) {
      // It's possible that numElapsedIntervals is negative (e.g. timer compensation
      // may result in (aNowTime - mTargetTime) < -1.0/mRateDuration, which will result in
      // negative numElapsedIntervals), so make sure we don't target the same timestamp.
      numElapsedIntervals = 0;
    }

    // the last "tick" that may or may not have been actually sent was
    // at this time.  For example, if the rate is 15ms, the target
    // time is 200ms, and it's now 225ms, the last effective tick
    // would have been at 215ms.  The next one should then be
    // scheduled for 5 ms from now.
    //
    // We then add another mRateDuration to find the next tick target.
    TimeStamp newTarget = mTargetTime + mRateDuration * (numElapsedIntervals + 1);

    // the amount of (integer) ms until the next time we should tick
    uint32_t delay = static_cast<uint32_t>((newTarget - aNowTime).ToMilliseconds());

    // Without this block, we'll always schedule on interval ticks;
    // with it, we'll schedule immediately if we missed our tick target
    // last time.
#if 0
    if (numElapsedIntervals > 0) {
      // we're late, so reset
      newTarget = aNowTime;
      delay = 0;
    }
#endif

    // log info & lateness
    LOG("[%p] precise timer last tick late by %f ms, next tick in %d ms",
        this,
        (aNowTime - mTargetTime).ToMilliseconds(),
        delay);

    // then schedule the timer
    LOG("[%p] scheduling callback for %d ms (2)", this, delay);
    mTimer->InitWithFuncCallback(TimerTick, this, delay, nsITimer::TYPE_ONE_SHOT);

    mTargetTime = newTarget;
  }
};

#ifdef XP_WIN
/*
 * Uses vsync timing on windows with DWM. Falls back dynamically to fixed rate if required.
 * - Call LoadDll() before usage and UnloadDll() when done (static, nesting unsupported)
 */
class PreciseRefreshDriverTimerWindowsDwmVsync :
  public PreciseRefreshDriverTimer
{
public:
  static void LoadDll()
  {
    if (sDwmGetCompositionTimingInfoPtr) {
      return; // Already loaded.
    }

    sDwmDll = ::LoadLibraryW(L"dwmapi.dll");
    if (sDwmDll) {
      sDwmGetCompositionTimingInfoPtr = (DwmGetCompositionTimingInfoProc)::GetProcAddress(sDwmDll, "DwmGetCompositionTimingInfo");
    }

    if (!sDwmDll || !sDwmGetCompositionTimingInfoPtr) {
      UnloadDll();
    }
  }

  // Checks if the vsync API is accessible.
  // Return value is meaningful after calling LoadDll() and before UnloadDll(), and false otherwise.
  // Even when supported, API calls could still fail when DWM is disabled (can change at runtime)
  static bool IsSupported()
  {
    return sDwmGetCompositionTimingInfoPtr ? true : false;
  }

  // OK to call even if never loaded and/or if load failed.
  static void UnloadDll()
  {
    if (sDwmDll) {
      FreeLibrary(sDwmDll);
    }
    sDwmDll = nullptr;
    sDwmGetCompositionTimingInfoPtr = nullptr;
  }

  PreciseRefreshDriverTimerWindowsDwmVsync(double aRate, bool aPreferHwTiming = false)
    : PreciseRefreshDriverTimer(aRate)
    , mPreferHwTiming(aPreferHwTiming)
  {
  }

protected:
  // Indicates we should try to adjust to the HW's timing (get rate from the OS or use vsync)
  // This is typically true if the default refresh-rate value was not modified by the user.
  bool mPreferHwTiming;

  nsresult GetVBlankInfo(mozilla::TimeStamp &aLastVBlank, mozilla::TimeDuration &aInterval)
  {
    MOZ_ASSERT(sDwmGetCompositionTimingInfoPtr, "DwmGetCompositionTimingInfoPtr is unavailable (windows vsync)");

    DWM_TIMING_INFO timingInfo;
    timingInfo.cbSize = sizeof(DWM_TIMING_INFO);
    HRESULT hr = sDwmGetCompositionTimingInfoPtr(0, &timingInfo); // For the desktop window instead of a specific one.

    if (FAILED(hr)) {
      // This happens first time this is called.
      return NS_ERROR_NOT_INITIALIZED;
    }

    LARGE_INTEGER time, freq;
    ::QueryPerformanceCounter(&time);
    ::QueryPerformanceFrequency(&freq);
    aLastVBlank = TimeStamp::Now();
    double secondsPassed = double(time.QuadPart - timingInfo.qpcVBlank) / double(freq.QuadPart);

    aLastVBlank -= TimeDuration::FromSeconds(secondsPassed);
    aInterval = TimeDuration::FromSeconds(double(timingInfo.qpcRefreshPeriod) / double(freq.QuadPart));

    return NS_OK;
  }

  virtual void ScheduleNextTick(TimeStamp aNowTime)
  {
    static const TimeDuration kMinSaneInterval = TimeDuration::FromMilliseconds(3); // 330Hz
    static const TimeDuration kMaxSaneInterval = TimeDuration::FromMilliseconds(44); // 23Hz
    static const TimeDuration kNegativeMaxSaneInterval = TimeDuration::FromMilliseconds(-44); // Saves conversions for abs interval
    TimeStamp lastVblank;
    TimeDuration vblankInterval;

    if (!mPreferHwTiming ||
        NS_OK != GetVBlankInfo(lastVblank, vblankInterval) ||
        vblankInterval > kMaxSaneInterval ||
        vblankInterval < kMinSaneInterval ||
        (aNowTime - lastVblank) > kMaxSaneInterval ||
        (aNowTime - lastVblank) < kNegativeMaxSaneInterval) {
      // Use the default timing without vsync
      PreciseRefreshDriverTimer::ScheduleNextTick(aNowTime);
      return;
    }

    TimeStamp newTarget = lastVblank + vblankInterval; // Base target

    // However, timer callback might return early (or late, but that wouldn't bother us), and vblankInterval
    // appears to be slightly (~1%) different on each call (probably the OS measuring recent actual interval[s])
    // and since we don't want to re-target the same vsync, we keep advancing in vblank intervals until we find the
    // next safe target (next vsync, but not within 10% interval of previous target).
    // This is typically 0 or 1 iteration:
    // If we're too early, next vsync would be the one we've already targeted (1 iteration).
    // If the timer returned late, no iteration will be required.

    const double kSameVsyncThreshold = 0.1;
    while (newTarget <= mTargetTime + vblankInterval.MultDouble(kSameVsyncThreshold)) {
      newTarget += vblankInterval;
    }

    // To make sure we always hit the same "side" of the signal:
    // round the delay up (by adding 1, since we later floor) and add a little (10% by default).
    // Note that newTarget doesn't change (and is the next vblank) as a reference when we're back.
    static const double kDefaultPhaseShiftPercent = 10;
    static const double phaseShiftFactor = 0.01 *
      (Preferences::GetInt("layout.frame_rate.vsync.phasePercentage", kDefaultPhaseShiftPercent) % 100);

    double phaseDelay = 1.0 + vblankInterval.ToMilliseconds() * phaseShiftFactor;

    // ms until the next time we should tick
    double delayMs = (newTarget - aNowTime).ToMilliseconds() + phaseDelay;

    // Make sure the delay is never negative.
    uint32_t delay = static_cast<uint32_t>(delayMs < 0 ? 0 : delayMs);

    // log info & lateness
    LOG("[%p] precise dwm-vsync timer last tick late by %f ms, next tick in %d ms",
        this,
        (aNowTime - mTargetTime).ToMilliseconds(),
        delay);

    // then schedule the timer
    LOG("[%p] scheduling callback for %d ms (2)", this, delay);
    mTimer->InitWithFuncCallback(TimerTick, this, delay, nsITimer::TYPE_ONE_SHOT);

    mTargetTime = newTarget;
  }

private:
  static HMODULE sDwmDll;
  static DwmGetCompositionTimingInfoProc sDwmGetCompositionTimingInfoPtr;
};

HMODULE PreciseRefreshDriverTimerWindowsDwmVsync::sDwmDll = nullptr;
DwmGetCompositionTimingInfoProc PreciseRefreshDriverTimerWindowsDwmVsync::sDwmGetCompositionTimingInfoPtr = nullptr;
#endif

/*
 * A RefreshDriverTimer for inactive documents.  When a new refresh driver is
 * added, the rate is reset to the base (normally 1s/1fps).  Every time
 * it ticks, a single refresh driver is poked.  Once they have all been poked,
 * the duration between ticks doubles, up to mDisableAfterMilliseconds.  At that point,
 * the timer is quiet and doesn't tick (until something is added to it again).
 *
 * When a timer is removed, there is a possibility of another timer
 * being skipped for one cycle.  We could avoid this by adjusting
 * mNextDriverIndex in RemoveRefreshDriver, but there's little need to
 * add that complexity.  All we want is for inactive drivers to tick
 * at some point, but we don't care too much about how often.
 */
class InactiveRefreshDriverTimer :
    public RefreshDriverTimer
{
public:
  InactiveRefreshDriverTimer(double aRate)
    : RefreshDriverTimer(aRate),
      mNextTickDuration(aRate),
      mDisableAfterMilliseconds(-1.0),
      mNextDriverIndex(0)
  {
    mTimer = do_CreateInstance(NS_TIMER_CONTRACTID);
  }

  InactiveRefreshDriverTimer(double aRate, double aDisableAfterMilliseconds)
    : RefreshDriverTimer(aRate),
      mNextTickDuration(aRate),
      mDisableAfterMilliseconds(aDisableAfterMilliseconds),
      mNextDriverIndex(0)
  {
    mTimer = do_CreateInstance(NS_TIMER_CONTRACTID);
  }

  virtual void AddRefreshDriver(nsRefreshDriver* aDriver)
  {
    RefreshDriverTimer::AddRefreshDriver(aDriver);

    LOG("[%p] inactive timer got new refresh driver %p, resetting rate",
        this, aDriver);

    // reset the timer, and start with the newly added one next time.
    mNextTickDuration = mRateMilliseconds;

    // we don't really have to start with the newly added one, but we may as well
    // not tick the old ones at the fastest rate any more than we need to.
    mNextDriverIndex = mRefreshDrivers.Length() - 1;

    StopTimer();
    StartTimer();
  }

protected:
  virtual void StartTimer()
  {
    mLastFireEpoch = JS_Now();
    mLastFireTime = TimeStamp::Now();

    mTargetTime = mLastFireTime + mRateDuration;

    uint32_t delay = static_cast<uint32_t>(mRateMilliseconds);
    mTimer->InitWithFuncCallback(TimerTickOne, this, delay, nsITimer::TYPE_ONE_SHOT);
  }

  virtual void StopTimer()
  {
    mTimer->Cancel();
  }

  virtual void ScheduleNextTick(TimeStamp aNowTime)
  {
    if (mDisableAfterMilliseconds > 0.0 &&
        mNextTickDuration > mDisableAfterMilliseconds)
    {
      // We hit the time after which we should disable
      // inactive window refreshes; don't schedule anything
      // until we get kicked by an AddRefreshDriver call.
      return;
    }

    // double the next tick time if we've already gone through all of them once
    if (mNextDriverIndex >= mRefreshDrivers.Length()) {
      mNextTickDuration *= 2.0;
      mNextDriverIndex = 0;
    }

    // this doesn't need to be precise; do a simple schedule
    uint32_t delay = static_cast<uint32_t>(mNextTickDuration);
    mTimer->InitWithFuncCallback(TimerTickOne, this, delay, nsITimer::TYPE_ONE_SHOT);

    LOG("[%p] inactive timer next tick in %f ms [index %d/%d]", this, mNextTickDuration,
        mNextDriverIndex, mRefreshDrivers.Length());
  }

  /* Runs just one driver's tick. */
  void TickOne()
  {
    int64_t jsnow = JS_Now();
    TimeStamp now = TimeStamp::Now();

    ScheduleNextTick(now);

    mLastFireEpoch = jsnow;
    mLastFireTime = now;

    nsTArray<nsRefPtr<nsRefreshDriver> > drivers(mRefreshDrivers);
    if (mNextDriverIndex < drivers.Length() &&
        !drivers[mNextDriverIndex]->IsTestControllingRefreshesEnabled())
    {
      TickDriver(drivers[mNextDriverIndex], jsnow, now);
    }

    mNextDriverIndex++;
  }

  static void TimerTickOne(nsITimer* aTimer, void* aClosure)
  {
    InactiveRefreshDriverTimer *timer = static_cast<InactiveRefreshDriverTimer*>(aClosure);
    timer->TickOne();
  }

  nsRefPtr<nsITimer> mTimer;
  double mNextTickDuration;
  double mDisableAfterMilliseconds;
  uint32_t mNextDriverIndex;
};

} // namespace mozilla

static uint32_t
GetFirstFrameDelay(imgIRequest* req)
{
  nsCOMPtr<imgIContainer> container;
  if (NS_FAILED(req->GetImage(getter_AddRefs(container))) || !container) {
    return 0;
  }

  // If this image isn't animated, there isn't a first frame delay.
  int32_t delay = container->GetFirstFrameDelay();
  if (delay < 0)
    return 0;

  return static_cast<uint32_t>(delay);
}

static PreciseRefreshDriverTimer *sRegularRateTimer = nullptr;
static InactiveRefreshDriverTimer *sThrottledRateTimer = nullptr;

#ifdef XP_WIN
static int32_t sHighPrecisionTimerRequests = 0;
// a bare pointer to avoid introducing a static constructor
static nsITimer *sDisableHighPrecisionTimersTimer = nullptr;
#endif

/* static */ void
nsRefreshDriver::InitializeStatics()
{
#ifdef XP_WIN
  PreciseRefreshDriverTimerWindowsDwmVsync::LoadDll();
#endif

#ifdef PR_LOGGING
  if (!gLog) {
    gLog = PR_NewLogModule("nsRefreshDriver");
  }
#endif
}

/* static */ void
nsRefreshDriver::Shutdown()
{
  // clean up our timers
  delete sRegularRateTimer;
  delete sThrottledRateTimer;

  sRegularRateTimer = nullptr;
  sThrottledRateTimer = nullptr;

#ifdef XP_WIN
  PreciseRefreshDriverTimerWindowsDwmVsync::UnloadDll();

  if (sDisableHighPrecisionTimersTimer) {
    sDisableHighPrecisionTimersTimer->Cancel();
    NS_RELEASE(sDisableHighPrecisionTimersTimer);
    timeEndPeriod(1);
  } else if (sHighPrecisionTimerRequests) {
    timeEndPeriod(1);
  }
#endif
}

/* static */ int32_t
nsRefreshDriver::DefaultInterval()
{
  return NSToIntRound(1000.0 / DEFAULT_FRAME_RATE);
}

// Compute the interval to use for the refresh driver timer, in milliseconds.
// outIsDefault indicates that rate was not explicitly set by the user
// so we might choose other, more appropriate rates (e.g. vsync, etc)
double
nsRefreshDriver::GetRegularTimerInterval(bool *outIsDefault) const
{
  int32_t rate = Preferences::GetInt("layout.frame_rate", -1);
  if (rate <= 0) {
    rate = DEFAULT_FRAME_RATE;
    if (outIsDefault) {
      *outIsDefault = true;
    }
  } else {
    if (outIsDefault) {
      *outIsDefault = false;
    }
  }
  return 1000.0 / rate;
}

double
nsRefreshDriver::GetThrottledTimerInterval() const
{
  int32_t rate = Preferences::GetInt("layout.throttled_frame_rate", -1);
  if (rate <= 0) {
    rate = DEFAULT_THROTTLED_FRAME_RATE;
  }
  return 1000.0 / rate;
}

double
nsRefreshDriver::GetRefreshTimerInterval() const
{
  return mThrottled ? GetThrottledTimerInterval() : GetRegularTimerInterval();
}

RefreshDriverTimer*
nsRefreshDriver::ChooseTimer() const
{
  if (mThrottled) {
    if (!sThrottledRateTimer) 
      sThrottledRateTimer = new InactiveRefreshDriverTimer(GetThrottledTimerInterval(),
                                                           DEFAULT_INACTIVE_TIMER_DISABLE_SECONDS * 1000.0);
    return sThrottledRateTimer;
  }

  if (!sRegularRateTimer) {
    bool isDefault = true;
    double rate = GetRegularTimerInterval(&isDefault);
#ifdef XP_WIN
    if (PreciseRefreshDriverTimerWindowsDwmVsync::IsSupported()) {
      sRegularRateTimer = new PreciseRefreshDriverTimerWindowsDwmVsync(rate, isDefault);
    }
#endif
    if (!sRegularRateTimer) {
      sRegularRateTimer = new PreciseRefreshDriverTimer(rate);
    }
  }
  return sRegularRateTimer;
}

nsRefreshDriver::nsRefreshDriver(nsPresContext* aPresContext)
  : mActiveTimer(nullptr),
    mPresContext(aPresContext),
    mFrozen(false),
    mThrottled(false),
    mTestControllingRefreshes(false),
    mViewManagerFlushIsPending(false),
    mRequestedHighPrecision(false)
{
  mMostRecentRefreshEpochTime = JS_Now();
  mMostRecentRefresh = TimeStamp::Now();

  mPaintFlashing = Preferences::GetBool("nglayout.debug.paint_flashing");

  mRequests.Init();
  mStartTable.Init();
}

nsRefreshDriver::~nsRefreshDriver()
{
  NS_ABORT_IF_FALSE(ObserverCount() == 0,
                    "observers should have unregistered");
  NS_ABORT_IF_FALSE(!mActiveTimer, "timer should be gone");
  
  for (uint32_t i = 0; i < mPresShellsToInvalidateIfHidden.Length(); i++) {
    mPresShellsToInvalidateIfHidden[i]->InvalidatePresShellIfHidden();
  }
  mPresShellsToInvalidateIfHidden.Clear();
}

// Method for testing.  See nsIDOMWindowUtils.advanceTimeAndRefresh
// for description.
void
nsRefreshDriver::AdvanceTimeAndRefresh(int64_t aMilliseconds)
{
  // ensure that we're removed from our driver
  StopTimer();

  if (!mTestControllingRefreshes) {
    mMostRecentRefreshEpochTime = JS_Now();
    mMostRecentRefresh = TimeStamp::Now();

    mTestControllingRefreshes = true;
  }

  mMostRecentRefreshEpochTime += aMilliseconds * 1000;
  mMostRecentRefresh += TimeDuration::FromMilliseconds((double) aMilliseconds);

  nsCxPusher pusher;
  pusher.PushNull();
  DoTick();
}

void
nsRefreshDriver::RestoreNormalRefresh()
{
  mTestControllingRefreshes = false;
  EnsureTimerStarted(false);
}

TimeStamp
nsRefreshDriver::MostRecentRefresh() const
{
  const_cast<nsRefreshDriver*>(this)->EnsureTimerStarted(false);

  return mMostRecentRefresh;
}

int64_t
nsRefreshDriver::MostRecentRefreshEpochTime() const
{
  const_cast<nsRefreshDriver*>(this)->EnsureTimerStarted(false);

  return mMostRecentRefreshEpochTime;
}

bool
nsRefreshDriver::AddRefreshObserver(nsARefreshObserver* aObserver,
                                    mozFlushType aFlushType)
{
  ObserverArray& array = ArrayFor(aFlushType);
  bool success = array.AppendElement(aObserver) != nullptr;

  EnsureTimerStarted(false);

  return success;
}

bool
nsRefreshDriver::RemoveRefreshObserver(nsARefreshObserver* aObserver,
                                       mozFlushType aFlushType)
{
  ObserverArray& array = ArrayFor(aFlushType);
  return array.RemoveElement(aObserver);
}

bool
nsRefreshDriver::AddImageRequest(imgIRequest* aRequest)
{
  uint32_t delay = GetFirstFrameDelay(aRequest);
  if (delay == 0) {
    if (!mRequests.PutEntry(aRequest)) {
      return false;
    }
  } else {
    ImageStartData* start = mStartTable.Get(delay);
    if (!start) {
      start = new ImageStartData();
      mStartTable.Put(delay, start);
    }
    start->mEntries.PutEntry(aRequest);
  }

  EnsureTimerStarted(false);

  return true;
}

void
nsRefreshDriver::RemoveImageRequest(imgIRequest* aRequest)
{
  // Try to remove from both places, just in case, because we can't tell
  // whether RemoveEntry() succeeds.
  mRequests.RemoveEntry(aRequest);
  uint32_t delay = GetFirstFrameDelay(aRequest);
  if (delay != 0) {
    ImageStartData* start = mStartTable.Get(delay);
    if (start) {
      start->mEntries.RemoveEntry(aRequest);
    }
  }
}

void
nsRefreshDriver::EnsureTimerStarted(bool aAdjustingTimer)
{
  if (mTestControllingRefreshes)
    return;

  // will it already fire, and no other changes needed?
  if (mActiveTimer && !aAdjustingTimer)
    return;

  if (mFrozen || !mPresContext) {
    // If we don't want to start it now, or we've been disconnected.
    StopTimer();
    return;
  }

  // We got here because we're either adjusting the time *or* we're
  // starting it for the first time.  Add to the right timer,
  // prehaps removing it from a previously-set one.
  RefreshDriverTimer *newTimer = ChooseTimer();
  if (newTimer != mActiveTimer) {
    if (mActiveTimer)
      mActiveTimer->RemoveRefreshDriver(this);
    mActiveTimer = newTimer;
    mActiveTimer->AddRefreshDriver(this);
  }

  mMostRecentRefresh = mActiveTimer->MostRecentRefresh();
  mMostRecentRefreshEpochTime = mActiveTimer->MostRecentRefreshEpochTime();
}

void
nsRefreshDriver::StopTimer()
{
  if (!mActiveTimer)
    return;

  mActiveTimer->RemoveRefreshDriver(this);
  mActiveTimer = nullptr;

  if (mRequestedHighPrecision) {
    SetHighPrecisionTimersEnabled(false);
  }
}

#ifdef XP_WIN
static void
DisableHighPrecisionTimersCallback(nsITimer *aTimer, void *aClosure)
{
  timeEndPeriod(1);
  NS_RELEASE(sDisableHighPrecisionTimersTimer);
}
#endif

void
nsRefreshDriver::ConfigureHighPrecision()
{
  bool haveFrameRequestCallbacks = mFrameRequestCallbackDocs.Length() > 0;

  // if the only change that's needed is that we need high precision,
  // then just set that
  if (!mThrottled && !mRequestedHighPrecision && haveFrameRequestCallbacks) {
    SetHighPrecisionTimersEnabled(true);
  } else if (mRequestedHighPrecision && !haveFrameRequestCallbacks) {
    SetHighPrecisionTimersEnabled(false);
  }
}

void
nsRefreshDriver::SetHighPrecisionTimersEnabled(bool aEnable)
{
  LOG("[%p] SetHighPrecisionTimersEnabled (%s)", this, aEnable ? "true" : "false");

  if (aEnable) {
    NS_ASSERTION(!mRequestedHighPrecision, "SetHighPrecisionTimersEnabled(true) called when already requested!");
#ifdef XP_WIN
    if (++sHighPrecisionTimerRequests == 1) {
      // If we had a timer scheduled to disable it, that means that it's already
      // enabled; just cancel the timer.  Otherwise, really enable it.
      if (sDisableHighPrecisionTimersTimer) {
        sDisableHighPrecisionTimersTimer->Cancel();
        NS_RELEASE(sDisableHighPrecisionTimersTimer);
      } else {
        timeBeginPeriod(1);
      }
    }
#endif
    mRequestedHighPrecision = true;
  } else {
    NS_ASSERTION(mRequestedHighPrecision, "SetHighPrecisionTimersEnabled(false) called when not requested!");
#ifdef XP_WIN
    if (--sHighPrecisionTimerRequests == 0) {
      // Don't jerk us around between high precision and low precision
      // timers; instead, only allow leaving high precision timers
      // after 90 seconds.  This is arbitrary, but hopefully good
      // enough.
      NS_ASSERTION(!sDisableHighPrecisionTimersTimer, "We shouldn't have an outstanding disable-high-precision timer !");

      nsCOMPtr<nsITimer> timer = do_CreateInstance(NS_TIMER_CONTRACTID);
      if (timer) {
        timer.forget(&sDisableHighPrecisionTimersTimer);
        sDisableHighPrecisionTimersTimer->InitWithFuncCallback(DisableHighPrecisionTimersCallback,
                                                               nullptr,
                                                               90 * 1000,
                                                               nsITimer::TYPE_ONE_SHOT);
      } else {
        // might happen if we're shutting down XPCOM; just drop the time period down
        // immediately
        timeEndPeriod(1);
      }
    }
#endif
    mRequestedHighPrecision = false;
  }
}

uint32_t
nsRefreshDriver::ObserverCount() const
{
  uint32_t sum = 0;
  for (uint32_t i = 0; i < ArrayLength(mObservers); ++i) {
    sum += mObservers[i].Length();
  }

  // Even while throttled, we need to process layout and style changes.  Style
  // changes can trigger transitions which fire events when they complete, and
  // layout changes can affect media queries on child documents, triggering
  // style changes, etc.
  sum += mStyleFlushObservers.Length();
  sum += mLayoutFlushObservers.Length();
  sum += mFrameRequestCallbackDocs.Length();
  sum += mViewManagerFlushIsPending;
  return sum;
}

/* static */ PLDHashOperator
nsRefreshDriver::StartTableRequestCounter(const uint32_t& aKey,
                                          ImageStartData* aEntry,
                                          void* aUserArg)
{
  uint32_t *count = static_cast<uint32_t*>(aUserArg);
  *count += aEntry->mEntries.Count();

  return PL_DHASH_NEXT;
}

uint32_t
nsRefreshDriver::ImageRequestCount() const
{
  uint32_t count = 0;
  mStartTable.EnumerateRead(nsRefreshDriver::StartTableRequestCounter, &count);
  return count + mRequests.Count();
}

nsRefreshDriver::ObserverArray&
nsRefreshDriver::ArrayFor(mozFlushType aFlushType)
{
  switch (aFlushType) {
    case Flush_Style:
      return mObservers[0];
    case Flush_Layout:
      return mObservers[1];
    case Flush_Display:
      return mObservers[2];
    default:
      NS_ABORT_IF_FALSE(false, "bad flush type");
      return *static_cast<ObserverArray*>(nullptr);
  }
}

/*
 * nsISupports implementation
 */

NS_IMPL_ISUPPORTS1(nsRefreshDriver, nsISupports)

/*
 * nsITimerCallback implementation
 */

void
nsRefreshDriver::DoTick()
{
  NS_PRECONDITION(!mFrozen, "Why are we notified while frozen?");
  NS_PRECONDITION(mPresContext, "Why are we notified after disconnection?");
  NS_PRECONDITION(!nsContentUtils::GetCurrentJSContext(),
                  "Shouldn't have a JSContext on the stack");

  if (mTestControllingRefreshes) {
    Tick(mMostRecentRefreshEpochTime, mMostRecentRefresh);
  } else {
    Tick(JS_Now(), TimeStamp::Now());
  }
}

struct DocumentFrameCallbacks {
  DocumentFrameCallbacks(nsIDocument* aDocument) :
    mDocument(aDocument)
  {}

  nsCOMPtr<nsIDocument> mDocument;
  nsIDocument::FrameRequestCallbackList mCallbacks;
};

void
nsRefreshDriver::Tick(int64_t aNowEpoch, TimeStamp aNowTime)
{
  NS_PRECONDITION(!nsContentUtils::GetCurrentJSContext(),
                  "Shouldn't have a JSContext on the stack");

  if (nsNPAPIPluginInstance::InPluginCallUnsafeForReentry()) {
    NS_ERROR("Refresh driver should not run during plugin call!");
    // Try to survive this by just ignoring the refresh tick.
    return;
  }

  PROFILER_LABEL("nsRefreshDriver", "Tick");

  // We're either frozen or we were disconnected (likely in the middle
  // of a tick iteration).  Just do nothing here, since our
  // prescontext went away.
  if (mFrozen || !mPresContext) {
    return;
  }

  TimeStamp previousRefresh = mMostRecentRefresh;

  mMostRecentRefresh = aNowTime;
  mMostRecentRefreshEpochTime = aNowEpoch;

  nsCOMPtr<nsIPresShell> presShell = mPresContext->GetPresShell();
  if (!presShell || (ObserverCount() == 0 && ImageRequestCount() == 0)) {
    // Things are being destroyed, or we no longer have any observers.
    // We don't want to stop the timer when observers are initially
    // removed, because sometimes observers can be added and removed
    // often depending on what other things are going on and in that
    // situation we don't want to thrash our timer.  So instead we
    // wait until we get a Notify() call when we have no observers
    // before stopping the timer.
    StopTimer();
    return;
  }

  /*
   * The timer holds a reference to |this| while calling |Notify|.
   * However, implementations of |WillRefresh| are permitted to destroy
   * the pres context, which will cause our |mPresContext| to become
   * null.  If this happens, we must stop notifying observers.
   */
  for (uint32_t i = 0; i < ArrayLength(mObservers); ++i) {
    ObserverArray::EndLimitedIterator etor(mObservers[i]);
    while (etor.HasMore()) {
      nsRefPtr<nsARefreshObserver> obs = etor.GetNext();
      obs->WillRefresh(aNowTime);
      
      if (!mPresContext || !mPresContext->GetPresShell()) {
        StopTimer();
        return;
      }
    }

    if (i == 0) {
      // Grab all of our frame request callbacks up front.
      nsTArray<DocumentFrameCallbacks>
        frameRequestCallbacks(mFrameRequestCallbackDocs.Length());
      for (uint32_t i = 0; i < mFrameRequestCallbackDocs.Length(); ++i) {
        frameRequestCallbacks.AppendElement(mFrameRequestCallbackDocs[i]);
        mFrameRequestCallbackDocs[i]->
          TakeFrameRequestCallbacks(frameRequestCallbacks.LastElement().mCallbacks);
      }
      // OK, now reset mFrameRequestCallbackDocs so they can be
      // readded as needed.
      mFrameRequestCallbackDocs.Clear();

      int64_t eventTime = aNowEpoch / PR_USEC_PER_MSEC;
      for (uint32_t i = 0; i < frameRequestCallbacks.Length(); ++i) {
        const DocumentFrameCallbacks& docCallbacks = frameRequestCallbacks[i];
        // XXXbz Bug 863140: GetInnerWindow can return the outer
        // window in some cases.
        nsPIDOMWindow* innerWindow = docCallbacks.mDocument->GetInnerWindow();
        DOMHighResTimeStamp timeStamp = 0;
        if (innerWindow && innerWindow->IsInnerWindow()) {
          nsPerformance* perf = innerWindow->GetPerformance();
          if (perf) {
            timeStamp = perf->GetDOMTiming()->TimeStampToDOMHighRes(aNowTime);
          }
          // else window is partially torn down already
        }
        for (uint32_t j = 0; j < docCallbacks.mCallbacks.Length(); ++j) {
          const nsIDocument::FrameRequestCallbackHolder& holder =
            docCallbacks.mCallbacks[j];
          nsAutoMicroTask mt;
          if (holder.HasWebIDLCallback()) {
            ErrorResult ignored;
            holder.GetWebIDLCallback()->Call(timeStamp, ignored);
          } else {
            holder.GetXPCOMCallback()->Sample(eventTime);
          }
        }
      }

      // This is the Flush_Style case.
      if (mPresContext && mPresContext->GetPresShell()) {
        nsAutoTArray<nsIPresShell*, 16> observers;
        observers.AppendElements(mStyleFlushObservers);
        for (uint32_t j = observers.Length();
             j && mPresContext && mPresContext->GetPresShell(); --j) {
          // Make sure to not process observers which might have been removed
          // during previous iterations.
          nsIPresShell* shell = observers[j - 1];
          if (!mStyleFlushObservers.Contains(shell))
            continue;
          NS_ADDREF(shell);
          mStyleFlushObservers.RemoveElement(shell);
          shell->FrameConstructor()->mObservingRefreshDriver = false;
          shell->FlushPendingNotifications(ChangesToFlush(Flush_Style, false));
          NS_RELEASE(shell);
        }
      }
    } else if  (i == 1) {
      // This is the Flush_Layout case.
      if (mPresContext && mPresContext->GetPresShell()) {
        nsAutoTArray<nsIPresShell*, 16> observers;
        observers.AppendElements(mLayoutFlushObservers);
        for (uint32_t j = observers.Length();
             j && mPresContext && mPresContext->GetPresShell(); --j) {
          // Make sure to not process observers which might have been removed
          // during previous iterations.
          nsIPresShell* shell = observers[j - 1];
          if (!mLayoutFlushObservers.Contains(shell))
            continue;
          NS_ADDREF(shell);
          mLayoutFlushObservers.RemoveElement(shell);
          shell->mReflowScheduled = false;
          shell->mSuppressInterruptibleReflows = false;
          shell->FlushPendingNotifications(ChangesToFlush(Flush_InterruptibleLayout,
                                                          false));
          NS_RELEASE(shell);
        }
      }
    }
  }

  /*
   * Perform notification to imgIRequests subscribed to listen
   * for refresh events.
   */

  ImageRequestParameters parms = {aNowTime, previousRefresh, &mRequests};

  mStartTable.EnumerateRead(nsRefreshDriver::StartTableRefresh, &parms);

  if (mRequests.Count()) {
    mRequests.EnumerateEntries(nsRefreshDriver::ImageRequestEnumerator, &parms);
  }

  for (uint32_t i = 0; i < mPresShellsToInvalidateIfHidden.Length(); i++) {
    mPresShellsToInvalidateIfHidden[i]->InvalidatePresShellIfHidden();
  }
  mPresShellsToInvalidateIfHidden.Clear();

  if (mViewManagerFlushIsPending) {
#ifdef MOZ_DUMP_PAINTING
    if (nsLayoutUtils::InvalidationDebuggingIsEnabled()) {
      printf("Starting ProcessPendingUpdates\n");
    }
#endif
#ifndef MOZ_WIDGET_GONK
    // Waiting for bug 830475 to work on B2G.
    nsRefPtr<layers::LayerManager> mgr = mPresContext->GetPresShell()->GetLayerManager();
    if (mgr) {
      mgr->SetPaintStartTime(mMostRecentRefresh);
    }
#endif

    mViewManagerFlushIsPending = false;
    nsRefPtr<nsViewManager> vm = mPresContext->GetPresShell()->GetViewManager();
    vm->ProcessPendingUpdates();
#ifdef MOZ_DUMP_PAINTING
    if (nsLayoutUtils::InvalidationDebuggingIsEnabled()) {
      printf("Ending ProcessPendingUpdates\n");
    }
#endif
  }
}

/* static */ PLDHashOperator
nsRefreshDriver::ImageRequestEnumerator(nsISupportsHashKey* aEntry,
                                        void* aUserArg)
{
  ImageRequestParameters* parms =
    static_cast<ImageRequestParameters*> (aUserArg);
  mozilla::TimeStamp mostRecentRefresh = parms->mCurrent;
  imgIRequest* req = static_cast<imgIRequest*>(aEntry->GetKey());
  NS_ABORT_IF_FALSE(req, "Unable to retrieve the image request");
  nsCOMPtr<imgIContainer> image;
  if (NS_SUCCEEDED(req->GetImage(getter_AddRefs(image)))) {
    image->RequestRefresh(mostRecentRefresh);
  }

  return PL_DHASH_NEXT;
}

/* static */ PLDHashOperator
nsRefreshDriver::BeginRefreshingImages(nsISupportsHashKey* aEntry,
                                       void* aUserArg)
{
  ImageRequestParameters* parms =
    static_cast<ImageRequestParameters*> (aUserArg);

  imgIRequest* req = static_cast<imgIRequest*>(aEntry->GetKey());
  NS_ABORT_IF_FALSE(req, "Unable to retrieve the image request");

  parms->mRequests->PutEntry(req);

  nsCOMPtr<imgIContainer> image;
  if (NS_SUCCEEDED(req->GetImage(getter_AddRefs(image)))) {
    image->SetAnimationStartTime(parms->mDesired);
  }

  return PL_DHASH_REMOVE;
}

/* static */ PLDHashOperator
nsRefreshDriver::StartTableRefresh(const uint32_t& aDelay,
                                   ImageStartData* aData,
                                   void* aUserArg)
{
  ImageRequestParameters* parms =
    static_cast<ImageRequestParameters*> (aUserArg);

  if (!aData->mStartTime.empty()) {
    TimeStamp& start = aData->mStartTime.ref();
    TimeDuration prev = parms->mPrevious - start;
    TimeDuration curr = parms->mCurrent - start;
    uint32_t prevMultiple = static_cast<uint32_t>(prev.ToMilliseconds()) / aDelay;

    // We want to trigger images' refresh if we've just crossed over a multiple
    // of the first image's start time. If so, set the animation start time to
    // the nearest multiple of the delay and move all the images in this table
    // to the main requests table.
    if (prevMultiple != static_cast<uint32_t>(curr.ToMilliseconds()) / aDelay) {
      parms->mDesired = start + TimeDuration::FromMilliseconds(prevMultiple * aDelay);
      aData->mEntries.EnumerateEntries(nsRefreshDriver::BeginRefreshingImages, parms);
    }
  } else {
    // This is the very first time we've drawn images with this time delay.
    // Set the animation start time to "now" and move all the images in this
    // table to the main requests table.
    parms->mDesired = parms->mCurrent;
    aData->mEntries.EnumerateEntries(nsRefreshDriver::BeginRefreshingImages, parms);
    aData->mStartTime.construct(parms->mCurrent);
  }

  return PL_DHASH_NEXT;
}

void
nsRefreshDriver::Freeze()
{
  NS_ASSERTION(!mFrozen, "Freeze called on already-frozen refresh driver");
  StopTimer();
  mFrozen = true;
}

void
nsRefreshDriver::Thaw()
{
  NS_ASSERTION(mFrozen, "Thaw called on an unfrozen refresh driver");
  mFrozen = false;
  if (ObserverCount() || ImageRequestCount()) {
    // FIXME: This isn't quite right, since our EnsureTimerStarted call
    // updates our mMostRecentRefresh, but the DoRefresh call won't run
    // and notify our observers until we get back to the event loop.
    // Thus MostRecentRefresh() will lie between now and the DoRefresh.
    NS_DispatchToCurrentThread(NS_NewRunnableMethod(this, &nsRefreshDriver::DoRefresh));
    EnsureTimerStarted(false);
  }
}

void
nsRefreshDriver::SetThrottled(bool aThrottled)
{
  if (aThrottled != mThrottled) {
    mThrottled = aThrottled;
    if (mActiveTimer) {
      // We want to switch our timer type here, so just stop and
      // restart the timer.
      EnsureTimerStarted(true);
    }
  }
}

void
nsRefreshDriver::DoRefresh()
{
  // Don't do a refresh unless we're in a state where we should be refreshing.
  if (!mFrozen && mPresContext && mActiveTimer) {
    DoTick();
  }
}

#ifdef DEBUG
bool
nsRefreshDriver::IsRefreshObserver(nsARefreshObserver* aObserver,
                                   mozFlushType aFlushType)
{
  ObserverArray& array = ArrayFor(aFlushType);
  return array.Contains(aObserver);
}
#endif

void
nsRefreshDriver::ScheduleViewManagerFlush()
{
  NS_ASSERTION(mPresContext->IsRoot(),
               "Should only schedule view manager flush on root prescontexts");
  mViewManagerFlushIsPending = true;
  EnsureTimerStarted(false);
}

void
nsRefreshDriver::ScheduleFrameRequestCallbacks(nsIDocument* aDocument)
{
  NS_ASSERTION(mFrameRequestCallbackDocs.IndexOf(aDocument) ==
               mFrameRequestCallbackDocs.NoIndex,
               "Don't schedule the same document multiple times");
  mFrameRequestCallbackDocs.AppendElement(aDocument);

  // make sure that the timer is running
  ConfigureHighPrecision();
  EnsureTimerStarted(false);
}

void
nsRefreshDriver::RevokeFrameRequestCallbacks(nsIDocument* aDocument)
{
  mFrameRequestCallbackDocs.RemoveElement(aDocument);
  ConfigureHighPrecision();
  // No need to worry about restarting our timer in slack mode if it's already
  // running; that will happen automatically when it fires.
}
