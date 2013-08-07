/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Android code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2012
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Patrick Walton <pcwalton@mozilla.com>
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

package org.mozilla.gecko.ui;

import org.mozilla.gecko.gfx.PointUtils;
import org.json.JSONException;
import android.graphics.PointF;
import android.util.Log;
import android.view.MotionEvent;
import java.util.LinkedList;
import java.util.ListIterator;
import java.util.Stack;

/**
 * A less buggy, and smoother, replacement for the built-in Android ScaleGestureDetector.
 *
 * This gesture detector is more reliable than the built-in ScaleGestureDetector because:
 *
 *   - It doesn't assume that pointer IDs are numbered 0 and 1.
 *
 *   - It doesn't attempt to correct for "slop" when resting one's hand on the device. On some
 *     devices (e.g. the Droid X) this can cause the ScaleGestureDetector to lose track of how many
 *     pointers are down, with disastrous results (bug 706684).
 *
 *   - Cancelling a zoom into a pan is handled correctly.
 *
 *   - Starting with three or more fingers down, releasing fingers so that only two are down, and
 *     then performing a scale gesture is handled correctly.
 *
 *   - It doesn't take pressure into account, which results in smoother scaling.
 */
public class SimpleScaleGestureDetector {
    private static final String LOGTAG = "GeckoSimpleScaleGestureDetector";

    private SimpleScaleGestureListener mListener;
    private long mLastEventTime;

    /* Information about all pointers that are down. */
    private LinkedList<PointerInfo> mPointerInfo;

    /** Creates a new gesture detector with the given listener. */
    public SimpleScaleGestureDetector(SimpleScaleGestureListener listener) {
        mListener = listener;
        mPointerInfo = new LinkedList<PointerInfo>();
    }

    /** Forward touch events to this function. */
    public void onTouchEvent(MotionEvent event) {
        switch (event.getAction() & event.ACTION_MASK) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN:
            onTouchStart(event);
            break;
        case MotionEvent.ACTION_MOVE:
            onTouchMove(event);
            break;
        case MotionEvent.ACTION_POINTER_UP:
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_CANCEL:
            onTouchEnd(event);
            break;
        }
    }

    private int getPointersDown() {
        return mPointerInfo.size();
    }

    private int getActionIndex(MotionEvent event) {
        return (event.getAction() & MotionEvent.ACTION_POINTER_INDEX_MASK)
            >> MotionEvent.ACTION_POINTER_INDEX_SHIFT;
    }

    private void onTouchStart(MotionEvent event) {
        mLastEventTime = event.getEventTime();
        mPointerInfo.addFirst(PointerInfo.create(event, getActionIndex(event)));
        if (getPointersDown() == 2) {
            sendScaleGesture(EventType.BEGIN);
        }
    }

    private void onTouchMove(MotionEvent event) {
        mLastEventTime = event.getEventTime();
        for (int i = 0; i < event.getPointerCount(); i++) {
            PointerInfo pointerInfo = pointerInfoForEventIndex(event, i);
            if (pointerInfo != null) {
                pointerInfo.populate(event, i);
            }
        }

        if (getPointersDown() == 2) {
            sendScaleGesture(EventType.CONTINUE);
        }
    }

    private void onTouchEnd(MotionEvent event) {
        mLastEventTime = event.getEventTime();

        int id = event.getPointerId(getActionIndex(event));
        ListIterator<PointerInfo> iterator = mPointerInfo.listIterator();
        while (iterator.hasNext()) {
            PointerInfo pointerInfo = iterator.next();
            if (pointerInfo.getId() != id) {
                continue;
            }

            // One of the pointers we were tracking was lifted. Remove its info object from the
            // list, recycle it to avoid GC pauses, and send an onScaleEnd() notification if this
            // ended the gesture.
            iterator.remove();
            pointerInfo.recycle();
            if (getPointersDown() == 1) {
                sendScaleGesture(EventType.END);
            }
        }
    }

    /**
     * Returns the X coordinate of the focus location (the midpoint of the two fingers). If only
     * one finger is down, returns the location of that finger.
     */
    public float getFocusX() {
        switch (getPointersDown()) {
        case 1:
            return mPointerInfo.getFirst().getCurrent().x;
        case 2:
            PointerInfo pointerA = mPointerInfo.getFirst(), pointerB = mPointerInfo.getLast();
            return (pointerA.getCurrent().x + pointerB.getCurrent().x) / 2.0f;
        }

        Log.e(LOGTAG, "No gesture taking place in getFocusX()!");
        return 0.0f;
    }

    /**
     * Returns the Y coordinate of the focus location (the midpoint of the two fingers). If only
     * one finger is down, returns the location of that finger.
     */
    public float getFocusY() {
        switch (getPointersDown()) {
        case 1:
            return mPointerInfo.getFirst().getCurrent().y;
        case 2:
            PointerInfo pointerA = mPointerInfo.getFirst(), pointerB = mPointerInfo.getLast();
            return (pointerA.getCurrent().y + pointerB.getCurrent().y) / 2.0f;
        }

        Log.e(LOGTAG, "No gesture taking place in getFocusY()!");
        return 0.0f;
    }

    /** Returns the most recent distance between the two pointers. */
    public float getCurrentSpan() {
        if (getPointersDown() != 2) {
            Log.e(LOGTAG, "No gesture taking place in getCurrentSpan()!");
            return 0.0f;
        }

        PointerInfo pointerA = mPointerInfo.getFirst(), pointerB = mPointerInfo.getLast();
        return PointUtils.distance(pointerA.getCurrent(), pointerB.getCurrent());
    }

    /** Returns the second most recent distance between the two pointers. */
    public float getPreviousSpan() {
        if (getPointersDown() != 2) {
            Log.e(LOGTAG, "No gesture taking place in getPreviousSpan()!");
            return 0.0f;
        }

        PointerInfo pointerA = mPointerInfo.getFirst(), pointerB = mPointerInfo.getLast();
        PointF a = pointerA.getPrevious(), b = pointerB.getPrevious();
        if (a == null || b == null) {
            a = pointerA.getCurrent();
            b = pointerB.getCurrent();
        }

        return PointUtils.distance(a, b);
    }

    /** Returns the time of the last event related to the gesture. */
    public long getEventTime() {
        return mLastEventTime;
    }

    /** Returns true if the scale gesture is in progress and false otherwise. */
    public boolean isInProgress() {
        return getPointersDown() == 2;
    }

    /* Sends the requested scale gesture notification to the listener. */
    private void sendScaleGesture(EventType eventType) {
        switch (eventType) {
        case BEGIN:     mListener.onScaleBegin(this);   break;
        case CONTINUE:  mListener.onScale(this);        break;
        case END:       mListener.onScaleEnd(this);     break;
        }
    }

    /*
     * Returns the pointer info corresponding to the given pointer index, or null if the pointer
     * isn't one that's being tracked.
     */
    private PointerInfo pointerInfoForEventIndex(MotionEvent event, int index) {
        int id = event.getPointerId(index);
        for (PointerInfo pointerInfo : mPointerInfo) {
            if (pointerInfo.getId() == id) {
                return pointerInfo;
            }
        }
        return null;
    }

    private enum EventType {
        BEGIN,
        CONTINUE,
        END,
    }

    /* Encapsulates information about one of the two fingers involved in the gesture. */
    private static class PointerInfo {
        /* A free list that recycles pointer info objects, to reduce GC pauses. */
        private static Stack<PointerInfo> sPointerInfoFreeList;

        private int mId;
        private PointF mCurrent, mPrevious;

        private PointerInfo() {
            // External users should use create() instead.
        }

        /* Creates or recycles a new PointerInfo instance from an event and a pointer index. */
        public static PointerInfo create(MotionEvent event, int index) {
            if (sPointerInfoFreeList == null) {
                sPointerInfoFreeList = new Stack<PointerInfo>();
            }

            PointerInfo pointerInfo;
            if (sPointerInfoFreeList.empty()) {
                pointerInfo = new PointerInfo();
            } else {
                pointerInfo = sPointerInfoFreeList.pop();
            }

            pointerInfo.populate(event, index);
            return pointerInfo;
        }

        /*
         * Fills in the fields of this instance from the given motion event and pointer index
         * within that event.
         */
        public void populate(MotionEvent event, int index) {
            mId = event.getPointerId(index);
            mPrevious = mCurrent;
            mCurrent = new PointF(event.getX(index), event.getY(index));
        }

        public void recycle() {
            mId = -1;
            mPrevious = mCurrent = null;
            sPointerInfoFreeList.push(this);
        }

        public int getId() { return mId; }
        public PointF getCurrent() { return mCurrent; }
        public PointF getPrevious() { return mPrevious; }

        @Override
        public String toString() {
            if (mId == -1) {
                return "(up)";
            }

            try {
                String prevString;
                if (mPrevious == null) {
                    prevString = "n/a";
                } else {
                    prevString = PointUtils.toJSON(mPrevious).toString();
                }

                // The current position should always be non-null.
                String currentString = PointUtils.toJSON(mCurrent).toString();
                return "id=" + mId + " cur=" + currentString + " prev=" + prevString;
            } catch (JSONException e) {
                throw new RuntimeException(e);
            }
        }
    }

    public static interface SimpleScaleGestureListener {
        public boolean onScale(SimpleScaleGestureDetector detector);
        public boolean onScaleBegin(SimpleScaleGestureDetector detector);
        public void onScaleEnd(SimpleScaleGestureDetector detector);
    }
}

