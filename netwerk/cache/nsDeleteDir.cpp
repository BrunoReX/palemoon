/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
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
 * The Initial Developer of the Original Code is Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Darin Fisher <darin@meer.net>
 *  Jason Duell <jduell.mcbugs@gmail.com>
 *  Michal Novotny <michal.novotny@gmail.com>
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

#include "nsDeleteDir.h"
#include "nsIFile.h"
#include "nsString.h"
#include "mozilla/Telemetry.h"
#include "nsITimer.h"
#include "nsISimpleEnumerator.h"
#include "nsAutoPtr.h"
#include "nsThreadUtils.h"
#include "nsISupportsPriority.h"

using namespace mozilla;

class nsBlockOnBackgroundThreadEvent : public nsRunnable {
public:
  nsBlockOnBackgroundThreadEvent() {}
  NS_IMETHOD Run()
  {
    MutexAutoLock lock(nsDeleteDir::gInstance->mLock);
    nsDeleteDir::gInstance->mCondVar.Notify();
    return NS_OK;
  }
};

class nsDestroyThreadEvent : public nsRunnable {
public:
  nsDestroyThreadEvent(nsIThread *thread)
    : mThread(thread)
  {}
  NS_IMETHOD Run()
  {
    mThread->Shutdown();
    return NS_OK;
  }
private:
  nsCOMPtr<nsIThread> mThread;
};


nsDeleteDir * nsDeleteDir::gInstance = nsnull;

nsDeleteDir::nsDeleteDir()
  : mLock("nsDeleteDir.mLock"),
    mCondVar(mLock, "nsDeleteDir.mCondVar"),
    mShutdownPending(false),
    mStopDeleting(false)
{
  NS_ASSERTION(gInstance==nsnull, "multiple nsCacheService instances!");
}

nsDeleteDir::~nsDeleteDir()
{
  gInstance = nsnull;
}

nsresult
nsDeleteDir::Init()
{
  if (gInstance)
    return NS_ERROR_ALREADY_INITIALIZED;

  gInstance = new nsDeleteDir();
  return NS_OK;
}

nsresult
nsDeleteDir::Shutdown(bool finishDeleting)
{
  if (!gInstance)
    return NS_ERROR_NOT_INITIALIZED;

  nsCOMArray<nsIFile> dirsToRemove;
  nsCOMPtr<nsIThread> thread;
  {
    MutexAutoLock lock(gInstance->mLock);
    NS_ASSERTION(!gInstance->mShutdownPending,
                 "Unexpected state in nsDeleteDir::Shutdown()");
    gInstance->mShutdownPending = true;

    if (!finishDeleting)
      gInstance->mStopDeleting = true;

    // remove all pending timers
    for (PRInt32 i = gInstance->mTimers.Count(); i > 0; i--) {
      nsCOMPtr<nsITimer> timer = gInstance->mTimers[i-1];
      gInstance->mTimers.RemoveObjectAt(i-1);
      timer->Cancel();

      nsCOMArray<nsIFile> *arg;
      timer->GetClosure((reinterpret_cast<void**>(&arg)));

      if (finishDeleting)
        dirsToRemove.AppendObjects(*arg);

      // delete argument passed to the timer
      delete arg;
    }

    thread.swap(gInstance->mThread);
    if (thread) {
      // dispatch event and wait for it to run and notify us, so we know thread
      // has completed all work and can be shutdown
      nsCOMPtr<nsIRunnable> event = new nsBlockOnBackgroundThreadEvent();
      nsresult rv = thread->Dispatch(event, NS_DISPATCH_NORMAL);
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed dispatching block-event");
        return NS_ERROR_UNEXPECTED;
      }

      rv = gInstance->mCondVar.Wait();
      thread->Shutdown();
    }
  }

  delete gInstance;

  for (PRInt32 i = 0; i < dirsToRemove.Count(); i++)
    dirsToRemove[i]->Remove(true);

  return NS_OK;
}

nsresult
nsDeleteDir::InitThread()
{
  if (mThread)
    return NS_OK;

  nsresult rv = NS_NewThread(getter_AddRefs(mThread));
  if (NS_FAILED(rv)) {
    NS_WARNING("Can't create background thread");
    return rv;
  }

  nsCOMPtr<nsISupportsPriority> p = do_QueryInterface(mThread);
  if (p) {
    p->SetPriority(nsISupportsPriority::PRIORITY_LOWEST);
  }
  return NS_OK;
}

void
nsDeleteDir::DestroyThread()
{
  if (!mThread)
    return;

  if (mTimers.Count())
    // more work to do, so don't delete thread.
    return;

  NS_DispatchToMainThread(new nsDestroyThreadEvent(mThread));
  mThread = nsnull;
}

void
nsDeleteDir::TimerCallback(nsITimer *aTimer, void *arg)
{
  Telemetry::AutoTimer<Telemetry::NETWORK_DISK_CACHE_DELETEDIR> timer;
  {
    MutexAutoLock lock(gInstance->mLock);

    PRInt32 idx = gInstance->mTimers.IndexOf(aTimer);
    if (idx == -1) {
      // Timer was canceled and removed during shutdown.
      return;
    }

    gInstance->mTimers.RemoveObjectAt(idx);
  }

  nsAutoPtr<nsCOMArray<nsIFile> > dirList;
  dirList = static_cast<nsCOMArray<nsIFile> *>(arg);

  bool shuttingDown = false;
  for (PRInt32 i = 0; i < dirList->Count() && !shuttingDown; i++) {
    gInstance->RemoveDir((*dirList)[i], &shuttingDown);
  }

  {
    MutexAutoLock lock(gInstance->mLock);
    gInstance->DestroyThread();
  }
}

nsresult
nsDeleteDir::DeleteDir(nsIFile *dirIn, bool moveToTrash, PRUint32 delay)
{
  Telemetry::AutoTimer<Telemetry::NETWORK_DISK_CACHE_TRASHRENAME> timer;

  if (!gInstance)
    return NS_ERROR_NOT_INITIALIZED;

  nsresult rv;
  nsCOMPtr<nsIFile> trash, dir;

  // Need to make a clone of this since we don't want to modify the input
  // file object.
  rv = dirIn->Clone(getter_AddRefs(dir));
  if (NS_FAILED(rv))
    return rv;

  if (moveToTrash) {
    rv = GetTrashDir(dir, &trash);
    if (NS_FAILED(rv))
      return rv;
    nsCAutoString origLeaf;
    rv = trash->GetNativeLeafName(origLeaf);
    if (NS_FAILED(rv))
      return rv;

    // Important: must rename directory w/o changing parent directory: else on
    // NTFS we'll wait (with cache lock) while nsIFile's ACL reset walks file
    // tree: was hanging GUI for *minutes* on large cache dirs.
    // Append random number to the trash directory and check if it exists.
    nsCAutoString leaf;
    for (PRInt32 i = 0; i < 10; i++) {
      leaf = origLeaf;
      leaf.AppendInt(rand());
      rv = trash->SetNativeLeafName(leaf);
      if (NS_FAILED(rv))
        return rv;

      bool exists;
      if (NS_SUCCEEDED(trash->Exists(&exists)) && !exists) {
        break;
      }

      leaf.Truncate();
    }

    // Fail if we didn't find unused trash directory within the limit
    if (!leaf.Length())
      return NS_ERROR_FAILURE;

    rv = dir->MoveToNative(nsnull, leaf);
    if (NS_FAILED(rv))
      return rv;
  } else {
    // we want to pass a clone of the original off to the worker thread.
    trash.swap(dir);
  }

  nsAutoPtr<nsCOMArray<nsIFile> > arg(new nsCOMArray<nsIFile>);
  arg->AppendObject(trash);

  rv = gInstance->PostTimer(arg, delay);
  if (NS_FAILED(rv))
    return rv;

  arg.forget();
  return NS_OK;
}

nsresult
nsDeleteDir::GetTrashDir(nsIFile *target, nsCOMPtr<nsIFile> *result)
{
  nsresult rv = target->Clone(getter_AddRefs(*result));
  if (NS_FAILED(rv))
    return rv;

  nsCAutoString leaf;
  rv = (*result)->GetNativeLeafName(leaf);
  if (NS_FAILED(rv))
    return rv;
  leaf.AppendLiteral(".Trash");

  return (*result)->SetNativeLeafName(leaf);
}

nsresult
nsDeleteDir::RemoveOldTrashes(nsIFile *cacheDir)
{
  if (!gInstance)
    return NS_ERROR_NOT_INITIALIZED;

  nsresult rv;

  static bool firstRun = true;

  if (!firstRun)
    return NS_OK;

  firstRun = false;

  nsCOMPtr<nsIFile> trash;
  rv = GetTrashDir(cacheDir, &trash);
  if (NS_FAILED(rv))
    return rv;

  nsAutoString trashName;
  rv = trash->GetLeafName(trashName);
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsIFile> parent;
  rv = cacheDir->GetParent(getter_AddRefs(parent));
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsISimpleEnumerator> iter;
  rv = parent->GetDirectoryEntries(getter_AddRefs(iter));
  if (NS_FAILED(rv))
    return rv;

  bool more;
  nsCOMPtr<nsISupports> elem;
  nsAutoPtr<nsCOMArray<nsIFile> > dirList;

  while (NS_SUCCEEDED(iter->HasMoreElements(&more)) && more) {
    rv = iter->GetNext(getter_AddRefs(elem));
    if (NS_FAILED(rv))
      continue;

    nsCOMPtr<nsIFile> file = do_QueryInterface(elem);
    if (!file)
      continue;

    nsAutoString leafName;
    rv = file->GetLeafName(leafName);
    if (NS_FAILED(rv))
      continue;

    // match all names that begin with the trash name (i.e. "Cache.Trash")
    if (Substring(leafName, 0, trashName.Length()).Equals(trashName)) {
      if (!dirList)
        dirList = new nsCOMArray<nsIFile>;
      dirList->AppendObject(file);
    }
  }

  if (dirList) {
    rv = gInstance->PostTimer(dirList, 90000);
    if (NS_FAILED(rv))
      return rv;

    dirList.forget();
  }

  return NS_OK;
}

nsresult
nsDeleteDir::PostTimer(void *arg, PRUint32 delay)
{
  nsresult rv;

  nsCOMPtr<nsITimer> timer = do_CreateInstance("@mozilla.org/timer;1", &rv);
  if (NS_FAILED(rv))
    return NS_ERROR_UNEXPECTED;

  MutexAutoLock lock(mLock);

  rv = InitThread();
  if (NS_FAILED(rv))
    return rv;

  rv = timer->SetTarget(mThread);
  if (NS_FAILED(rv))
    return rv;

  rv = timer->InitWithFuncCallback(TimerCallback, arg, delay,
                                   nsITimer::TYPE_ONE_SHOT);
  if (NS_FAILED(rv))
    return rv;

  mTimers.AppendObject(timer);
  return NS_OK;
}

nsresult
nsDeleteDir::RemoveDir(nsIFile *file, bool *stopDeleting)
{
  nsresult rv;
  bool isLink;

  rv = file->IsSymlink(&isLink);
  if (NS_FAILED(rv) || isLink)
    return NS_ERROR_UNEXPECTED;

  bool isDir;
  rv = file->IsDirectory(&isDir);
  if (NS_FAILED(rv))
    return rv;

  if (isDir) {
    nsCOMPtr<nsISimpleEnumerator> iter;
    rv = file->GetDirectoryEntries(getter_AddRefs(iter));
    if (NS_FAILED(rv))
      return rv;

    bool more;
    nsCOMPtr<nsISupports> elem;
    while (NS_SUCCEEDED(iter->HasMoreElements(&more)) && more) {
      rv = iter->GetNext(getter_AddRefs(elem));
      if (NS_FAILED(rv)) {
        NS_WARNING("Unexpected failure in nsDeleteDir::RemoveDir");
        continue;
      }

      nsCOMPtr<nsIFile> file2 = do_QueryInterface(elem);
      if (!file2) {
        NS_WARNING("Unexpected failure in nsDeleteDir::RemoveDir");
        continue;
      }

      RemoveDir(file2, stopDeleting);
      // No check for errors to remove as much as possible

      if (*stopDeleting)
        return NS_OK;
    }
  }

  file->Remove(false);
  // No check for errors to remove as much as possible

  MutexAutoLock lock(mLock);
  if (mStopDeleting)
    *stopDeleting = true;

  return NS_OK;
}
