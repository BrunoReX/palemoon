/* Copyright 2013 Mozilla Foundation and Mozilla contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "GonkDisplayICS.h"
#include <ui/FramebufferNativeWindow.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <hardware/hwcomposer.h>
#include <hardware_legacy/power.h>
#include <cutils/log.h>
#include <fcntl.h>

#include "mozilla/FileUtils.h"
#include "mozilla/NullPtr.h"

#include "BootAnimation.h"

using namespace android;

namespace mozilla {

static GonkDisplayICS* sGonkDisplay = nullptr;

GonkDisplayICS::GonkDisplayICS()
    : mModule(nullptr)
    , mHwc(nullptr)
{
    // Some gralloc HALs need this in order to open the
    // framebuffer device after we restart with the screen off.
    //
    // this *must* run BEFORE allocating the
    // FramebufferNativeWindow.
    set_screen_state(1);

    // For some devices, it takes a while for the framebuffer to become
    // usable. So we wait until the framebuffer has woken up before we
    // try to open it.
    {
        char buf;
        int len = 0;
        ScopedClose fd(open("/sys/power/wait_for_fb_wake", O_RDONLY, 0));
        do {
            len = read(fd.get(), &buf, 1);
        } while (len < 0 && errno == EINTR);
        if (len < 0) {
            LOGE("BootAnimation: wait_for_fb_sleep failed errno: %d", errno);
        }
    }

    mFBSurface = new FramebufferNativeWindow();

    int err = hw_get_module(HWC_HARDWARE_MODULE_ID, &mModule);
    LOGW_IF(err, "%s module not found", HWC_HARDWARE_MODULE_ID);
    if (!err) {
        err = hwc_open(mModule, &mHwc);
        LOGE_IF(err, "%s device failed to initialize (%s)",
                 HWC_HARDWARE_COMPOSER, strerror(-err));
    }

    xdpi = mFBSurface->xdpi;

    const framebuffer_device_t *fbdev = mFBSurface->getDevice();
    surfaceformat = fbdev->format;

    StartBootAnimation();
}

GonkDisplayICS::~GonkDisplayICS()
{
    if (mHwc)
        hwc_close(mHwc);
}

ANativeWindow*
GonkDisplayICS::GetNativeWindow()
{
    StopBootAnimation();
    return static_cast<ANativeWindow *>(mFBSurface.get());
}

void
GonkDisplayICS::SetEnabled(bool enabled)
{
    set_screen_state(enabled);
}

void*
GonkDisplayICS::GetHWCDevice()
{
    return mHwc;
}

bool
GonkDisplayICS::SwapBuffers(EGLDisplay dpy, EGLSurface sur)
{
    if (!mHwc)
        return eglSwapBuffers(dpy, sur);

    mHwc->prepare(mHwc, nullptr);
    return !mHwc->set(mHwc, dpy, sur, 0);
}

ANativeWindowBuffer*
GonkDisplayICS::DequeueBuffer()
{
    ANativeWindow *window = static_cast<ANativeWindow *>(mFBSurface.get());
    ANativeWindowBuffer *buf = nullptr;
    window->dequeueBuffer(window, &buf);
    return buf;
}

bool
GonkDisplayICS::QueueBuffer(ANativeWindowBuffer *buf)
{
    ANativeWindow *window = static_cast<ANativeWindow *>(mFBSurface.get());
    return !window->queueBuffer(window, buf);
}

__attribute__ ((visibility ("default")))
GonkDisplay*
GetGonkDisplay()
{
    if (!sGonkDisplay)
        sGonkDisplay = new GonkDisplayICS();
    return sGonkDisplay;
}

} // namespace mozilla
