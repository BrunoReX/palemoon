/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=8 et :
 */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Code.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Jones <jones.chris.g@gmail.com>
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

#ifndef dom_plugins_PluginBackgroundDestroyer
#define dom_plugins_PluginBackgroundDestroyer

#include "mozilla/plugins/PPluginBackgroundDestroyerChild.h"
#include "mozilla/plugins/PPluginBackgroundDestroyerParent.h"

#include "gfxASurface.h"
#include "gfxSharedImageSurface.h"

namespace mozilla {
namespace plugins {

/**
 * When instances of this class are destroyed, the old background goes
 * along with them, completing the destruction process (whether or not
 * the plugin stayed alive long enough to ack).
 */
class PluginBackgroundDestroyerParent : public PPluginBackgroundDestroyerParent {
public:
    PluginBackgroundDestroyerParent(gfxASurface* aDyingBackground)
      : mDyingBackground(aDyingBackground)
    { }

    virtual ~PluginBackgroundDestroyerParent() { }

private:
    NS_OVERRIDE
    virtual void ActorDestroy(ActorDestroyReason why)
    {
        switch(why) {
        case Deletion:
        case AncestorDeletion:
            if (gfxSharedImageSurface::IsSharedImage(mDyingBackground)) {
                gfxSharedImageSurface* s =
                    static_cast<gfxSharedImageSurface*>(mDyingBackground.get());
                DeallocShmem(s->GetShmem());
            }
            break;
        default:
            // We're shutting down or crashed, let automatic cleanup
            // take care of our shmem, if we have one.
            break;
        }
    }

    nsRefPtr<gfxASurface> mDyingBackground;
};

/**
 * This class exists solely to instruct its instance to release its
 * current background, a new one may be coming.
 */
class PluginBackgroundDestroyerChild : public PPluginBackgroundDestroyerChild {
public:
    PluginBackgroundDestroyerChild() { }
    virtual ~PluginBackgroundDestroyerChild() { }

private:
    // Implementing this for good hygiene.
    NS_OVERRIDE
    virtual void ActorDestroy(ActorDestroyReason why)
    { }
};

} // namespace plugins
} // namespace mozilla

#endif  // dom_plugins_PluginBackgroundDestroyer
