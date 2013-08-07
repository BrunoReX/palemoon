
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkShape_DEFINED
#define SkShape_DEFINED

#include "SkFlattenable.h"

class SkCanvas;
class SkMatrix;
class SkWStream;

class SkShape : public SkFlattenable {
public:
    SkShape();
    virtual ~SkShape();

    void draw(SkCanvas*);

    /** Draw the shape translated by (dx,dy), which is applied before the
        shape's matrix (if any).
     */
    void drawXY(SkCanvas*, SkScalar dx, SkScalar dy);

    /** Draw the shape with the specified matrix, applied before the shape's
        matrix (if any).
     */
    void drawMatrix(SkCanvas*, const SkMatrix&);

    // overrides
    virtual Factory getFactory();
    virtual void flatten(SkFlattenableWriteBuffer&);

    // public for Registrar
    static SkFlattenable* CreateProc(SkFlattenableReadBuffer&);

    SK_DECLARE_FLATTENABLE_REGISTRAR()

protected:
    virtual void onDraw(SkCanvas*);

    SkShape(SkFlattenableReadBuffer&);

private:

    typedef SkFlattenable INHERITED;
};

#endif
