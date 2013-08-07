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
 * Portions created by the Initial Developer are Copyright (C) 2009-2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Patrick Walton <pcwalton@mozilla.com>
 *   Chris Lord <chrislord.net@gmail.com>
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

package org.mozilla.gecko.gfx;

import org.mozilla.gecko.FloatUtils;
import org.json.JSONException;
import org.json.JSONObject;

public class FloatSize {
    public final float width, height;

    public FloatSize(FloatSize size) { width = size.width; height = size.height; }
    public FloatSize(IntSize size) { width = size.width; height = size.height; }
    public FloatSize(float aWidth, float aHeight) { width = aWidth; height = aHeight; }

    public FloatSize(JSONObject json) {
        try {
            width = (float)json.getDouble("width");
            height = (float)json.getDouble("height");
        } catch (JSONException e) {
            throw new RuntimeException(e);
        }
    }

    @Override
    public String toString() { return "(" + width + "," + height + ")"; }

    public boolean isPositive() {
        return (width > 0 && height > 0);
    }

    public boolean fuzzyEquals(FloatSize size) {
        return (FloatUtils.fuzzyEquals(size.width, width) &&
                FloatUtils.fuzzyEquals(size.height, height));
    }

    public FloatSize scale(float factor) {
        return new FloatSize(width * factor, height * factor);
    }

    /*
     * Returns the size that represents a linear transition between this size and `to` at time `t`,
     * which is on the scale [0, 1).
     */
    public FloatSize interpolate(FloatSize to, float t) {
        return new FloatSize(FloatUtils.interpolate(width, to.width, t),
                             FloatUtils.interpolate(height, to.height, t));
    }
}

