/* -*- Mode: c++; c-basic-offset: 4; tab-width: 40; indent-tabs-mode: nil -*- */
/* vim: set ts=40 sw=4 et tw=99: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jstypedarrayinlines_h
#define jstypedarrayinlines_h

#include "jsapi.h"
#include "jsobj.h"
#include "jstypedarray.h"

#include "jsobjinlines.h"

inline uint32_t
js::ArrayBufferObject::byteLength() const
{
    JS_ASSERT(isArrayBuffer());
    return getElementsHeader()->length;
}

inline uint8_t *
js::ArrayBufferObject::dataPointer() const
{
    return (uint8_t *) elements;
}

inline js::ArrayBufferObject &
JSObject::asArrayBuffer()
{
    JS_ASSERT(isArrayBuffer());
    return *static_cast<js::ArrayBufferObject *>(this);
}

inline js::DataViewObject &
JSObject::asDataView()
{
    JS_ASSERT(isDataView());
    return *static_cast<js::DataViewObject *>(this);
}

namespace js {

inline bool
ArrayBufferObject::hasData() const
{
    return getClass() == &ArrayBufferClass;
}

static inline int32_t
ClampIntForUint8Array(int32_t x)
{
    if (x < 0)
        return 0;
    if (x > 255)
        return 255;
    return x;
}

inline uint32_t
TypedArray::getLength(JSObject *obj) {
    JS_ASSERT(obj->isTypedArray());
    return obj->getFixedSlot(FIELD_LENGTH).toInt32();
}

inline uint32_t
TypedArray::getByteOffset(JSObject *obj) {
    JS_ASSERT(obj->isTypedArray());
    return obj->getFixedSlot(FIELD_BYTEOFFSET).toInt32();
}

inline uint32_t
TypedArray::getByteLength(JSObject *obj) {
    JS_ASSERT(obj->isTypedArray());
    return obj->getFixedSlot(FIELD_BYTELENGTH).toInt32();
}

inline uint32_t
TypedArray::getType(JSObject *obj) {
    JS_ASSERT(obj->isTypedArray());
    return obj->getFixedSlot(FIELD_TYPE).toInt32();
}

inline ArrayBufferObject *
TypedArray::getBuffer(JSObject *obj) {
    JS_ASSERT(obj->isTypedArray());
    return &obj->getFixedSlot(FIELD_BUFFER).toObject().asArrayBuffer();
}

inline void *
TypedArray::getDataOffset(JSObject *obj) {
    JS_ASSERT(obj->isTypedArray());
    return (void *)obj->getPrivate(NUM_FIXED_SLOTS);
}

inline DataViewObject *
DataViewObject::create(JSContext *cx, uint32_t byteOffset, uint32_t byteLength,
                       Handle<ArrayBufferObject*> arrayBuffer, JSObject *proto)
{
    JS_ASSERT(byteOffset <= INT32_MAX);
    JS_ASSERT(byteLength <= INT32_MAX);

    RootedObject obj(cx, NewBuiltinClassInstance(cx, &DataViewClass));
    if (!obj)
        return NULL;

    types::TypeObject *type;
    if (proto) {
        type = proto->getNewType(cx);
    } else {
        /*
         * Specialize the type of the object on the current scripted location,
         * and mark the type as definitely a data view
         */
        JSProtoKey key = JSCLASS_CACHED_PROTO_KEY(&DataViewClass);
        type = types::GetTypeCallerInitObject(cx, key);
        if (!type)
            return NULL;
    }
    obj->setType(type);

    JS_ASSERT(arrayBuffer->isArrayBuffer());

    DataViewObject &dvobj = obj->asDataView();
    dvobj.setFixedSlot(BYTEOFFSET_SLOT, Int32Value(byteOffset));
    dvobj.setFixedSlot(BYTELENGTH_SLOT, Int32Value(byteLength));
    dvobj.setFixedSlot(BUFFER_SLOT, ObjectValue(*arrayBuffer));
    dvobj.setPrivate(arrayBuffer->dataPointer() + byteOffset);
    JS_ASSERT(byteOffset + byteLength <= arrayBuffer->byteLength());

    JS_ASSERT(dvobj.numFixedSlots() == RESERVED_SLOTS);

    return &dvobj;
}

inline uint32_t
DataViewObject::byteLength()
{
    JS_ASSERT(isDataView());
    int32_t length = getReservedSlot(BYTELENGTH_SLOT).toInt32();
    JS_ASSERT(length >= 0);
    return static_cast<uint32_t>(length);
}

inline uint32_t
DataViewObject::byteOffset()
{
    JS_ASSERT(isDataView());
    int32_t offset = getReservedSlot(BYTEOFFSET_SLOT).toInt32();
    JS_ASSERT(offset >= 0);
    return static_cast<uint32_t>(offset);
}

inline void *
DataViewObject::dataPointer()
{
    JS_ASSERT(isDataView());
    return getPrivate();
}

inline JSObject &
DataViewObject::arrayBuffer()
{
    JS_ASSERT(isDataView());
    return getReservedSlot(BUFFER_SLOT).toObject();
}

inline bool
DataViewObject::hasBuffer() const
{
    JS_ASSERT(isDataView());
    return getReservedSlot(BUFFER_SLOT).isObject();
}

} /* namespace js */

#endif /* jstypedarrayinlines_h */
