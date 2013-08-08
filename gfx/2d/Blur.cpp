/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/gfx/Blur.h"

#include <algorithm>
#include <math.h>
#include <string.h>
#include <windows.h>

#include "mozilla/CheckedInt.h"
#include "mozilla/Util.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace std;

namespace mozilla {
namespace gfx {

/**
 * Box blur involves looking at one pixel, and setting its value to the average
 * of its neighbouring pixels.
 * @param aInput The input buffer.
 * @param aOutput The output buffer.
 * @param aLeftLobe The number of pixels to blend on the left.
 * @param aRightLobe The number of pixels to blend on the right.
 * @param aWidth The number of columns in the buffers.
 * @param aRows The number of rows in the buffers.
 * @param aSkipRect An area to skip blurring in.
 * XXX shouldn't we pass stride in separately here?
 */
static DWORD NumberOfProcessors = 0;

static void
GetNumberOfLogicalProcessors(void)
{
    SYSTEM_INFO SystemInfo;

    GetSystemInfo(&SystemInfo);
    NumberOfProcessors = SystemInfo.dwNumberOfProcessors;
}

static void
GetNumberOfProcessors(void)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION SystemLogicalProcessorInformation = NULL;
    DWORD SizeSystemLogicalProcessorInformation = 0;

    while(!GetLogicalProcessorInformation(SystemLogicalProcessorInformation, &SizeSystemLogicalProcessorInformation)) {
        if(SystemLogicalProcessorInformation) free(SystemLogicalProcessorInformation);

        if(GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            SystemLogicalProcessorInformation =
                static_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION>(malloc(SizeSystemLogicalProcessorInformation));
        } else {
            GetNumberOfLogicalProcessors();
            return;
        }
    }

    DWORD ProcessorCore = 0;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Ptr = SystemLogicalProcessorInformation;

    for(DWORD Offset = sizeof SYSTEM_LOGICAL_PROCESSOR_INFORMATION;
        Offset <= SizeSystemLogicalProcessorInformation;
        Offset += sizeof SYSTEM_LOGICAL_PROCESSOR_INFORMATION) {
        if(Ptr++->Relationship == RelationProcessorCore) ProcessorCore++;
    }

    free(SystemLogicalProcessorInformation);

    if(ProcessorCore) {
        NumberOfProcessors = ProcessorCore;
    } else {
        GetNumberOfLogicalProcessors();
    }
}

struct BoxBlurHorizontal_Param {
    BoxBlurHorizontal_Param* Ptr;
    unsigned char* aInput;
    unsigned char* aOutput;
    int32_t aLeftLobe;
    int32_t aWidth;
    IntRect* aSkipRect;
    int32_t boxSize;
    bool skipRectCoversWholeRow;
    int32_t y;
    int32_t Loop;
    uint32_t reciprocal;
    int32_t* aInput_next_last;
};

DWORD WINAPI
BoxBlurHorizontal_Thread(void* Param)
{
//    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    unsigned char* const aInput = static_cast<BoxBlurHorizontal_Param*>(Param)->Ptr->aInput;
    unsigned char* const aOutput = static_cast<BoxBlurHorizontal_Param*>(Param)->Ptr->aOutput;
    const int32_t aLeftLobe = static_cast<BoxBlurHorizontal_Param*>(Param)->Ptr->aLeftLobe;
    const int32_t aWidth = static_cast<BoxBlurHorizontal_Param*>(Param)->Ptr->aWidth;
    IntRect* const aSkipRect = static_cast<BoxBlurHorizontal_Param*>(Param)->Ptr->aSkipRect;
    const int32_t boxSize = static_cast<BoxBlurHorizontal_Param*>(Param)->Ptr->boxSize;
    const bool skipRectCoversWholeRow = static_cast<BoxBlurHorizontal_Param*>(Param)->Ptr->skipRectCoversWholeRow;
    const int32_t Loop = static_cast<BoxBlurHorizontal_Param*>(Param)->Loop;
    const int64_t reciprocal = static_cast<BoxBlurHorizontal_Param*>(Param)->Ptr->reciprocal;
    int32_t* const aInput_next = static_cast<BoxBlurHorizontal_Param*>(Param)->Ptr->aInput_next_last;
    int32_t* const aInput_last = aInput_next + aWidth;

    for (int32_t y = static_cast<BoxBlurHorizontal_Param*>(Param)->y; y < Loop; y++) {
        // Check whether the skip rect intersects this row. If the skip
        // rect covers the whole surface in this row, we can avoid
        // this row entirely (and any others along the skip rect).
        bool inSkipRectY = y >= aSkipRect->y && y < aSkipRect->YMost();

        if (inSkipRectY && skipRectCoversWholeRow) {
            y = aSkipRect->YMost() - 1;
            continue;
        }

        const int32_t aWidth_y = aWidth * y;
        uint32_t alphaSum = 0;

        for (int32_t i = 0; i < boxSize; i++) {
            int32_t pos = i - aLeftLobe;
            // See assertion above; if aWidth is zero, then we would have no
            // valid position to clamp to.
            pos = max(pos, 0);
            pos = min(pos, aWidth - 1);
            alphaSum += aInput[aWidth_y + pos];
        }

        unsigned char* ptr_aInput = aInput + aWidth_y;
        unsigned char* ptr_aOutput = aOutput + aWidth_y;
        int32_t* ptr_aInput_next = aInput_next;
        int32_t* ptr_aInput_last = aInput_last;

        for (int32_t x = 0; x < aWidth; x++) {
            // Check whether we are within the skip rect. If so, go
            // to the next point outside the skip rect.
            if (inSkipRectY && x >= aSkipRect->x &&
                x < aSkipRect->XMost()) {
                x = aSkipRect->XMost();
                if (x >= aWidth)
                    break;

                // Recalculate the neighbouring alpha values for
                // our new point on the surface.
                alphaSum = 0;

                ptr_aOutput = aOutput + aWidth_y + x;
                ptr_aInput_next = aInput_next + x;
                ptr_aInput_last = aInput_last + x;

                for (int32_t i = 0; i < boxSize; i++) {
                    int32_t pos = x + i - aLeftLobe;
                    // See assertion above; if aWidth is zero, then we would have no
                    // valid position to clamp to.
                    pos = max(pos, 0);
                    pos = min(pos, aWidth - 1);
                    alphaSum += aInput[aWidth_y + pos];
                }
            }

            *ptr_aOutput++ = (uint64_t(alphaSum) * reciprocal) >> 32;

            alphaSum += *(ptr_aInput + *ptr_aInput_next++) - *(ptr_aInput + *ptr_aInput_last++);
        }
    }

    ExitThread(0);

    return 0;
}

static void
BoxBlurHorizontal(unsigned char* aInput,
                  unsigned char* aOutput,
                  int32_t aLeftLobe,
                  int32_t aRightLobe,
                  int32_t aWidth,
                  int32_t aRows,
                  const IntRect& aSkipRect)
{
    MOZ_ASSERT(aWidth > 0);

    const int32_t boxSize = aLeftLobe + aRightLobe + 1;

    if(boxSize == 1) {
        memcpy(aOutput, aInput, aWidth * aRows);
        return;
    }

    if(NumberOfProcessors == 0) GetNumberOfProcessors();

    BoxBlurHorizontal_Param* Param = new BoxBlurHorizontal_Param[NumberOfProcessors];

    Param->aInput = aInput;
    Param->aOutput = aOutput;
    Param->aLeftLobe = aLeftLobe;
    Param->aWidth = aWidth;
    Param->aSkipRect = &const_cast<IntRect&>(aSkipRect);
    Param->boxSize = boxSize;
    Param->skipRectCoversWholeRow = 0 >= aSkipRect.x && aWidth <= aSkipRect.XMost();
    Param->reciprocal = (uint64_t(1) << 32) / boxSize;
    Param->aInput_next_last = new int32_t[aWidth * 2];

    int32_t* ptr_aInput_next = Param->aInput_next_last;
    int32_t* ptr_aInput_last = ptr_aInput_next + aWidth;
    int32_t tmp = -aLeftLobe;
    const int32_t aWidth_1 = aWidth - 1;

    for(int32_t x = 0; x < aWidth; x++) {
        *ptr_aInput_next++ = min(tmp + boxSize, aWidth_1);
        *ptr_aInput_last++ = max(tmp, 0);
        tmp++;
    }

    const int Step = aRows / NumberOfProcessors;
    const int Remain = aRows - Step * NumberOfProcessors;
    HANDLE* Thread = new HANDLE[NumberOfProcessors];

    for(int Idx = 0, y = 0; Idx < NumberOfProcessors; Idx++) {
        Param[Idx].Ptr = Param;
        Param[Idx].y = y;
        y += Step + (Idx == 0 ? Remain : 0);
        Param[Idx].Loop = y;

        Thread[Idx] = CreateThread(NULL, 0, BoxBlurHorizontal_Thread, Param + Idx, 0, NULL);
    }

    WaitForMultipleObjects(NumberOfProcessors, Thread, true, INFINITE);

    for(int Idx = 0; Idx < NumberOfProcessors; Idx++) {
        CloseHandle(Thread[Idx]);
    }

    delete[] Thread;
    delete[] Param->aInput_next_last;
    delete[] Param;
}

/**
 * Identical to BoxBlurHorizontal, except it blurs top and bottom instead of
 * left and right.
 * XXX shouldn't we pass stride in separately here?
 */
struct BoxBlurVertical_Param {
    BoxBlurVertical_Param* Ptr;
    unsigned char* aInput;
    unsigned char* aOutput;
    int32_t aTopLobe;
    int32_t aWidth;
    int32_t aRows;
    IntRect* aSkipRect;
    int32_t boxSize;
    bool skipRectCoversWholeColumn;
    int32_t x;
    int32_t Loop;
    uint32_t reciprocal;
    int32_t* aInput_next_last;
};

DWORD WINAPI
BoxBlurVertical_Thread(void* Param)
{
//    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    unsigned char* const aInput = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->aInput;
    unsigned char* const aOutput = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->aOutput;
    const int32_t aTopLobe = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->aTopLobe;
    const int32_t aWidth = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->aWidth;
    const int32_t aRows = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->aRows;
    IntRect* const aSkipRect = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->aSkipRect;
    const int32_t boxSize = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->boxSize;
    const bool skipRectCoversWholeColumn = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->skipRectCoversWholeColumn;
    const int32_t Loop = static_cast<BoxBlurVertical_Param*>(Param)->Loop;
    const int64_t reciprocal = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->reciprocal;
    int32_t* const aInput_next = static_cast<BoxBlurVertical_Param*>(Param)->Ptr->aInput_next_last;
    int32_t* const aInput_last = aInput_next + aRows;

    for (int32_t x = static_cast<BoxBlurVertical_Param*>(Param)->x; x < Loop; x++) {
        bool inSkipRectX = x >= aSkipRect->x && x < aSkipRect->XMost();

        if (inSkipRectX && skipRectCoversWholeColumn) {
            x = aSkipRect->XMost() - 1;
            continue;
        }

        uint32_t alphaSum = 0;

        for (int32_t i = 0; i < boxSize; i++) {
            int32_t pos = i - aTopLobe;
            // See assertion above; if aRows is zero, then we would have no
            // valid position to clamp to.
            pos = max(pos, 0);
            pos = min(pos, aRows - 1);
            alphaSum += aInput[aWidth * pos + x];
        }

        unsigned char* ptr_aInput = aInput + x;
        unsigned char* ptr_aOutput = aOutput + x;
        int32_t* ptr_aInput_next = aInput_next;
        int32_t* ptr_aInput_last = aInput_last;

        for (int32_t y = 0; y < aRows; y++) {
            if (inSkipRectX && y >= aSkipRect->y &&
                y < aSkipRect->YMost()) {
                y = aSkipRect->YMost();
                if (y >= aRows)
                    break;

                alphaSum = 0;

                ptr_aOutput = aOutput + aWidth * y + x;
                ptr_aInput_next = aInput_next + y;
                ptr_aInput_last = aInput_last + y;

                for (int32_t i = 0; i < boxSize; i++) {
                    int32_t pos = y + i - aTopLobe;
                    // See assertion above; if aRows is zero, then we would have no
                    // valid position to clamp to.
                    pos = max(pos, 0);
                    pos = min(pos, aRows - 1);
                    alphaSum += aInput[aWidth * pos + x];
                }
            }

            *ptr_aOutput = (uint64_t(alphaSum) * reciprocal) >> 32;
            ptr_aOutput += aWidth;

            alphaSum += *(ptr_aInput + *ptr_aInput_next++) - *(ptr_aInput + *ptr_aInput_last++);
        }
    }

    ExitThread(0);

    return 0;
}

static void
BoxBlurVertical(unsigned char* aInput,
                unsigned char* aOutput,
                int32_t aTopLobe,
                int32_t aBottomLobe,
                int32_t aWidth,
                int32_t aRows,
                const IntRect& aSkipRect)
{
    MOZ_ASSERT(aRows > 0);

    const int32_t boxSize = aTopLobe + aBottomLobe + 1;

    if(boxSize == 1) {
        memcpy(aOutput, aInput, aWidth * aRows);
        return;
    }

    if(NumberOfProcessors == 0) GetNumberOfProcessors();

    BoxBlurVertical_Param* Param = new BoxBlurVertical_Param[NumberOfProcessors];

    Param->aInput = aInput;
    Param->aOutput = aOutput;
    Param->aTopLobe = aTopLobe;
    Param->aWidth = aWidth;
    Param->aRows = aRows;
    Param->aSkipRect = &const_cast<IntRect&>(aSkipRect);
    Param->boxSize = boxSize;
    Param->skipRectCoversWholeColumn = 0 >= aSkipRect.y && aRows <= aSkipRect.YMost();
    Param->reciprocal = (uint64_t(1) << 32) / boxSize;
    Param->aInput_next_last = new int32_t[aRows * 2];

    int32_t* ptr_aInput_next = Param->aInput_next_last;
    int32_t* ptr_aInput_last = ptr_aInput_next + aRows;
    int32_t tmp = -aTopLobe;
    const int32_t aRows_1 = aRows - 1;

    for(int32_t y = 0; y < aRows; y++) {
        *ptr_aInput_next++ = aWidth * min(tmp + boxSize, aRows_1);
        *ptr_aInput_last++ = aWidth * max(tmp, 0);
        tmp++;
    }

    const int Step = aWidth / NumberOfProcessors;
    const int Remain = aWidth - Step * NumberOfProcessors;
    HANDLE* Thread = new HANDLE[NumberOfProcessors];

    for(int Idx = 0, x = 0; Idx < NumberOfProcessors; Idx++) {
        Param[Idx].Ptr = Param;
        Param[Idx].x = x;
        x += Step + (Idx == 0 ? Remain : 0);
        Param[Idx].Loop = x;

        Thread[Idx] = CreateThread(NULL, 0, BoxBlurVertical_Thread, Param + Idx, 0, NULL);
    }

    WaitForMultipleObjects(NumberOfProcessors, Thread, true, INFINITE);

    for(int Idx = 0; Idx < NumberOfProcessors; Idx++) {
        CloseHandle(Thread[Idx]);
    }

    delete[] Thread;
    delete[] Param->aInput_next_last;
    delete[] Param;
}

static void ComputeLobes(int32_t aRadius, int32_t aLobes[3][2])
{
    int32_t major, minor, final;

    /* See http://www.w3.org/TR/SVG/filters.html#feGaussianBlur for
     * some notes about approximating the Gaussian blur with box-blurs.
     * The comments below are in the terminology of that page.
     */
    int32_t z = aRadius / 3;
    switch (aRadius % 3) {
    case 0:
        // aRadius = z*3; choose d = 2*z + 1
        major = minor = final = z;
        break;
    case 1:
        // aRadius = z*3 + 1
        // This is a tricky case since there is no value of d which will
        // yield a radius of exactly aRadius. If d is odd, i.e. d=2*k + 1
        // for some integer k, then the radius will be 3*k. If d is even,
        // i.e. d=2*k, then the radius will be 3*k - 1.
        // So we have to choose values that don't match the standard
        // algorithm.
        major = z + 1;
        minor = final = z;
        break;
    case 2:
        // aRadius = z*3 + 2; choose d = 2*z + 2
        major = final = z + 1;
        minor = z;
        break;
    default:
        // Mathematical impossibility!
        MOZ_ASSERT(false);
        major = minor = final = 0;
    }
    MOZ_ASSERT(major + minor + final == aRadius);

    aLobes[0][0] = major;
    aLobes[0][1] = minor;
    aLobes[1][0] = minor;
    aLobes[1][1] = major;
    aLobes[2][0] = final;
    aLobes[2][1] = final;
}

static void
SpreadHorizontal(unsigned char* aInput,
                 unsigned char* aOutput,
                 int32_t aRadius,
                 int32_t aWidth,
                 int32_t aRows,
                 int32_t aStride,
                 const IntRect& aSkipRect)
{
    if (aRadius == 0) {
        memcpy(aOutput, aInput, aStride * aRows);
        return;
    }

    bool skipRectCoversWholeRow = 0 >= aSkipRect.x &&
                                    aWidth <= aSkipRect.XMost();
    for (int32_t y = 0; y < aRows; y++) {
        // Check whether the skip rect intersects this row. If the skip
        // rect covers the whole surface in this row, we can avoid
        // this row entirely (and any others along the skip rect).
        bool inSkipRectY = y >= aSkipRect.y &&
                             y < aSkipRect.YMost();
        if (inSkipRectY && skipRectCoversWholeRow) {
            y = aSkipRect.YMost() - 1;
            continue;
        }

        for (int32_t x = 0; x < aWidth; x++) {
            // Check whether we are within the skip rect. If so, go
            // to the next point outside the skip rect.
            if (inSkipRectY && x >= aSkipRect.x &&
                x < aSkipRect.XMost()) {
                x = aSkipRect.XMost();
                if (x >= aWidth)
                    break;
            }

            int32_t sMin = max(x - aRadius, 0);
            int32_t sMax = min(x + aRadius, aWidth - 1);
            int32_t v = 0;
            for (int32_t s = sMin; s <= sMax; ++s) {
                v = max<int32_t>(v, aInput[aStride * y + s]);
            }
            aOutput[aStride * y + x] = v;
        }
    }
}

static void
SpreadVertical(unsigned char* aInput,
               unsigned char* aOutput,
               int32_t aRadius,
               int32_t aWidth,
               int32_t aRows,
               int32_t aStride,
               const IntRect& aSkipRect)
{
    if (aRadius == 0) {
        memcpy(aOutput, aInput, aStride * aRows);
        return;
    }

    bool skipRectCoversWholeColumn = 0 >= aSkipRect.y &&
                                     aRows <= aSkipRect.YMost();
    for (int32_t x = 0; x < aWidth; x++) {
        bool inSkipRectX = x >= aSkipRect.x &&
                           x < aSkipRect.XMost();
        if (inSkipRectX && skipRectCoversWholeColumn) {
            x = aSkipRect.XMost() - 1;
            continue;
        }

        for (int32_t y = 0; y < aRows; y++) {
            // Check whether we are within the skip rect. If so, go
            // to the next point outside the skip rect.
            if (inSkipRectX && y >= aSkipRect.y &&
                y < aSkipRect.YMost()) {
                y = aSkipRect.YMost();
                if (y >= aRows)
                    break;
            }

            int32_t sMin = max(y - aRadius, 0);
            int32_t sMax = min(y + aRadius, aRows - 1);
            int32_t v = 0;
            for (int32_t s = sMin; s <= sMax; ++s) {
                v = max<int32_t>(v, aInput[aStride * s + x]);
            }
            aOutput[aStride * y + x] = v;
        }
    }
}

static CheckedInt<int32_t>
RoundUpToMultipleOf4(int32_t aVal)
{
  CheckedInt<int32_t> val(aVal);

  val += 3;
  val /= 4;
  val *= 4;

  return val;
}

AlphaBoxBlur::AlphaBoxBlur(const Rect& aRect,
                           const IntSize& aSpreadRadius,
                           const IntSize& aBlurRadius,
                           const Rect* aDirtyRect,
                           const Rect* aSkipRect)
 : mSpreadRadius(aSpreadRadius),
   mBlurRadius(aBlurRadius),
   mData(NULL)
{
  Rect rect(aRect);
  rect.Inflate(Size(aBlurRadius + aSpreadRadius));
  rect.RoundOut();

  if (aDirtyRect) {
    // If we get passed a dirty rect from layout, we can minimize the
    // shadow size and make painting faster.
    mHasDirtyRect = true;
    mDirtyRect = *aDirtyRect;
    Rect requiredBlurArea = mDirtyRect.Intersect(rect);
    requiredBlurArea.Inflate(Size(aBlurRadius + aSpreadRadius));
    rect = requiredBlurArea.Intersect(rect);
  } else {
    mHasDirtyRect = false;
  }

  if (rect.IsEmpty()) {
    return;
  }

  if (aSkipRect) {
    // If we get passed a skip rect, we can lower the amount of
    // blurring/spreading we need to do. We convert it to IntRect to avoid
    // expensive int<->float conversions if we were to use Rect instead.
    Rect skipRect = *aSkipRect;
    skipRect.RoundIn();
    skipRect.Deflate(Size(aBlurRadius + aSpreadRadius));
    mSkipRect = IntRect(skipRect.x, skipRect.y, skipRect.width, skipRect.height);

    IntRect shadowIntRect(rect.x, rect.y, rect.width, rect.height);
    mSkipRect.IntersectRect(mSkipRect, shadowIntRect);

    if (mSkipRect.IsEqualInterior(shadowIntRect))
      return;

    mSkipRect -= shadowIntRect.TopLeft();
  } else {
    mSkipRect = IntRect(0, 0, 0, 0);
  }

  mRect = IntRect(rect.x, rect.y, rect.width, rect.height);

  CheckedInt<int32_t> stride = RoundUpToMultipleOf4(mRect.width);
  if (stride.isValid()) {
    mStride = stride.value();

    CheckedInt<int32_t> size = CheckedInt<int32_t>(mStride) * mRect.height *
                               sizeof(unsigned char);
    if (size.isValid()) {
      mData = static_cast<unsigned char*>(malloc(size.value()));
      memset(mData, 0, size.value());
    }
  }
}

AlphaBoxBlur::~AlphaBoxBlur()
{
  free(mData);
}

unsigned char*
AlphaBoxBlur::GetData()
{
  return mData;
}

IntSize
AlphaBoxBlur::GetSize()
{
  IntSize size(mRect.width, mRect.height);
  return size;
}

int32_t
AlphaBoxBlur::GetStride()
{
  return mStride;
}

IntRect
AlphaBoxBlur::GetRect()
{
  return mRect;
}

Rect*
AlphaBoxBlur::GetDirtyRect()
{
  if (mHasDirtyRect) {
    return &mDirtyRect;
  }

  return NULL;
}

void
AlphaBoxBlur::Blur()
{
  if (!mData) {
    return;
  }

  // no need to do all this if not blurring or spreading
  if (mBlurRadius != IntSize(0,0) || mSpreadRadius != IntSize(0,0)) {
    int32_t stride = GetStride();

    // No need to use CheckedInt here - we have validated it in the constructor.
    size_t szB = stride * GetSize().height * sizeof(unsigned char);
    unsigned char* tmpData = static_cast<unsigned char*>(malloc(szB));
    if (!tmpData)
      return; // OOM

    memset(tmpData, 0, szB);

    if (mSpreadRadius.width > 0 || mSpreadRadius.height > 0) {
      SpreadHorizontal(mData, tmpData, mSpreadRadius.width, GetSize().width, GetSize().height, stride, mSkipRect);
      SpreadVertical(tmpData, mData, mSpreadRadius.height, GetSize().width, GetSize().height, stride, mSkipRect);
    }

    if (mBlurRadius.width > 0) {
      int32_t lobes[3][2];
      ComputeLobes(mBlurRadius.width, lobes);
      BoxBlurHorizontal(mData, tmpData, lobes[0][0], lobes[0][1], stride, GetSize().height, mSkipRect);
      BoxBlurHorizontal(tmpData, mData, lobes[1][0], lobes[1][1], stride, GetSize().height, mSkipRect);
      BoxBlurHorizontal(mData, tmpData, lobes[2][0], lobes[2][1], stride, GetSize().height, mSkipRect);
    } else {
      memcpy(tmpData, mData, stride * GetSize().height);
    }

    if (mBlurRadius.height > 0) {
      int32_t lobes[3][2];
      ComputeLobes(mBlurRadius.height, lobes);
      BoxBlurVertical(tmpData, mData, lobes[0][0], lobes[0][1], stride, GetSize().height, mSkipRect);
      BoxBlurVertical(mData, tmpData, lobes[1][0], lobes[1][1], stride, GetSize().height, mSkipRect);
      BoxBlurVertical(tmpData, mData, lobes[2][0], lobes[2][1], stride, GetSize().height, mSkipRect);
    } else {
      memcpy(mData, tmpData, stride * GetSize().height);
    }

    free(tmpData);
  }

}

/**
 * Compute the box blur size (which we're calling the blur radius) from
 * the standard deviation.
 *
 * Much of this, the 3 * sqrt(2 * pi) / 4, is the known value for
 * approximating a Gaussian using box blurs.  This yields quite a good
 * approximation for a Gaussian.  Then we multiply this by 1.5 since our
 * code wants the radius of the entire triple-box-blur kernel instead of
 * the diameter of an individual box blur.  For more details, see:
 *   http://www.w3.org/TR/SVG11/filters.html#feGaussianBlurElement
 *   https://bugzilla.mozilla.org/show_bug.cgi?id=590039#c19
 */
static const Float GAUSSIAN_SCALE_FACTOR = (3 * sqrt(2 * M_PI) / 4) * 1.5;

IntSize
AlphaBoxBlur::CalculateBlurRadius(const Point& aStd)
{
    IntSize size(static_cast<int32_t>(floor(aStd.x * GAUSSIAN_SCALE_FACTOR + 0.5)),
                 static_cast<int32_t>(floor(aStd.y * GAUSSIAN_SCALE_FACTOR + 0.5)));

    return size;
}

}
}
