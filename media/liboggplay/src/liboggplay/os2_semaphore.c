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
 * The Original Code is the Mozilla browser.
 *
 * The Initial Developer of the Original Code is
 * Richard Walsh
 * Portions created by the Initial Developer are Copyright (c) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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
 * ***** END LICENSE BLOCK ***** *
 */
/*****************************************************************************/
/*  
 *  This is a conservative implementation of a posix counting semaphore.
 *  It relies on the state of the underlying OS/2 event semaphore to
 *  control whether sem_wait() blocks or returns immediately, and only
 *  uses its count to change the sem's state from posted to reset.
 *  (A more "activist" approach would use the count to decide whether
 *  to return immediately or to call DosWaitEventSem().)
 *  
 */
/*****************************************************************************/

#include <stdlib.h>
#define INCL_DOS
#include <os2.h>
#include "os2_semaphore.h"

#ifndef ERROR_SEM_BUSY
#define ERROR_SEM_BUSY  301
#endif

#define SEM_WAIT        20000

/*****************************************************************************/

int sem_init(sem_t *sem, int pshared, unsigned value)
{
  OS2SEM *    psem;

  if (!sem)
    return -1;
  *sem = 0;

  psem = (OS2SEM*)malloc(sizeof(OS2SEM));
  if (!psem)
    return -1;

  if (DosCreateMutexSem(0, &psem->hmtx, 0, 0)) {
    free(psem);
    return -1;
  }

  if (DosCreateEventSem(0, &psem->hev, 0, (value ? 1 : 0))) {
    DosCloseMutexSem(psem->hmtx);
    free(psem);
    return -1;
  }

  psem->cnt = value;
  *sem = psem;
  return 0;
}

/*****************************************************************************/

int sem_wait(sem_t *sem)
{
  OS2SEM *    psem;
  ULONG       cnt;

  if (!sem || !*sem)
    return -1;
  psem = *sem;

  if (DosWaitEventSem(psem->hev, -1))
    return -1;

  if (DosRequestMutexSem(psem->hmtx, SEM_WAIT))
    return -1;

  if (psem->cnt)
    psem->cnt--;
  if (!psem->cnt)
    DosResetEventSem(psem->hev, &cnt);
  DosReleaseMutexSem(psem->hmtx);

  return 0;
}

/*****************************************************************************/

int sem_post(sem_t *sem)
{
  OS2SEM *    psem;

  if (!sem || !*sem)
    return -1;
  psem = *sem;

  if (!DosRequestMutexSem(psem->hmtx, SEM_WAIT)) {
    psem->cnt++;
    DosPostEventSem(psem->hev);
    DosReleaseMutexSem(psem->hmtx);
    return 0;
  }

  return -1;
}

/*****************************************************************************/

int sem_destroy(sem_t *sem)
{
  OS2SEM *    psem;

  if (!sem || !*sem)
    return -1;
  psem = *sem;

  if (DosCloseMutexSem(psem->hmtx) == ERROR_SEM_BUSY) {
    DosRequestMutexSem(psem->hmtx, SEM_WAIT);
    DosReleaseMutexSem(psem->hmtx);
    DosCloseMutexSem(psem->hmtx);
  }

  if (DosCloseEventSem(psem->hev) == ERROR_SEM_BUSY) {
    DosPostEventSem(psem->hev);
    DosSleep(1);
    DosCloseEventSem(psem->hev);
  }

  free(psem);
  return 0;
}

/*****************************************************************************/

