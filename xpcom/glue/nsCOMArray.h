/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCOMArray_h__
#define nsCOMArray_h__

#include "mozilla/Attributes.h"

#include "nsCycleCollectionNoteChild.h"
#include "nsVoidArray.h"
#include "nsISupports.h"

// See below for the definition of nsCOMArray<T>

// a class that's nsISupports-specific, so that we can contain the
// work of this class in the XPCOM dll
class NS_COM_GLUE nsCOMArray_base
{
    friend class nsArray;
protected:
    nsCOMArray_base() {}
    nsCOMArray_base(int32_t aCount) : mArray(aCount) {}
    nsCOMArray_base(const nsCOMArray_base& other);
    ~nsCOMArray_base();

    int32_t IndexOf(nsISupports* aObject) const {
        return mArray.IndexOf(aObject);
    }

    int32_t IndexOfObject(nsISupports* aObject) const;

    bool EnumerateForwards(nsVoidArrayEnumFunc aFunc, void* aData) {
        return mArray.EnumerateForwards(aFunc, aData);
    }
    
    bool EnumerateBackwards(nsVoidArrayEnumFunc aFunc, void* aData) {
        return mArray.EnumerateBackwards(aFunc, aData);
    }
    
    void Sort(nsVoidArrayComparatorFunc aFunc, void* aData) {
        mArray.Sort(aFunc, aData);
    }
    
    // any method which is not a direct forward to mArray should
    // avoid inline bodies, so that the compiler doesn't inline them
    // all over the place
    void Clear();
    bool InsertObjectAt(nsISupports* aObject, int32_t aIndex);
    bool InsertObjectsAt(const nsCOMArray_base& aObjects, int32_t aIndex);
    bool ReplaceObjectAt(nsISupports* aObject, int32_t aIndex);
    bool AppendObject(nsISupports *aObject) {
        return InsertObjectAt(aObject, Count());
    }
    bool AppendObjects(const nsCOMArray_base& aObjects) {
        return InsertObjectsAt(aObjects, Count());
    }
    bool RemoveObject(nsISupports *aObject);
    bool RemoveObjectAt(int32_t aIndex);
    bool RemoveObjectsAt(int32_t aIndex, int32_t aCount);

public:
    // override nsVoidArray stuff so that they can be accessed by
    // consumers of nsCOMArray
    int32_t Count() const {
        return mArray.Count();
    }
    // If the array grows, the newly created entries will all be null;
    // if the array shrinks, the excess entries will all be released.
    bool SetCount(int32_t aNewCount);

    nsISupports* ObjectAt(int32_t aIndex) const {
        return static_cast<nsISupports*>(mArray.FastElementAt(aIndex));
    }
    
    nsISupports* SafeObjectAt(int32_t aIndex) const {
        return static_cast<nsISupports*>(mArray.SafeElementAt(aIndex));
    }

    nsISupports* operator[](int32_t aIndex) const {
        return ObjectAt(aIndex);
    }

    // Ensures there is enough space to store a total of aCapacity objects.
    // This method never deletes any objects.
    bool SetCapacity(uint32_t aCapacity) {
      return aCapacity > 0 ? mArray.SizeTo(static_cast<int32_t>(aCapacity))
                           : true;
    }

    // Measures the size of the array's element storage, and if
    // |aSizeOfElement| is non-NULL, measures the size of things pointed to by
    // elements.
    size_t SizeOfExcludingThis(
             nsVoidArraySizeOfElementIncludingThisFunc aSizeOfElementIncludingThis,
             nsMallocSizeOfFun aMallocSizeOf, void* aData = NULL) const {
        return mArray.SizeOfExcludingThis(aSizeOfElementIncludingThis,
                                          aMallocSizeOf, aData);
    }
    
private:
    
    // the actual storage
    nsVoidArray mArray;

    // don't implement these, defaults will muck with refcounts!
    nsCOMArray_base& operator=(const nsCOMArray_base& other) MOZ_DELETE;

    // needs to call Clear() which is protected
    friend void ImplCycleCollectionUnlink(nsCOMArray_base& aField);
};

inline void
ImplCycleCollectionUnlink(nsCOMArray_base& aField)
{
    aField.Clear();
}

inline void
ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& aCallback,
                            nsCOMArray_base& aField,
                            const char* aName,
                            uint32_t aFlags = 0)
{
    aFlags |= CycleCollectionEdgeNameArrayFlag;
    size_t length = aField.Count();
    for (size_t i = 0; i < length; ++i) {
        CycleCollectionNoteChild(aCallback, aField[i], aName, aFlags);
    }
}


// a non-XPCOM, refcounting array of XPCOM objects
// used as a member variable or stack variable - this object is NOT
// refcounted, but the objects that it holds are
//
// most of the read-only accessors like ObjectAt()/etc do NOT refcount
// on the way out. This means that you can do one of two things:
//
// * does an addref, but holds onto a reference
// nsCOMPtr<T> foo = array[i];
//
// * avoids the refcount, but foo might go stale if array[i] is ever
// * modified/removed. Be careful not to NS_RELEASE(foo)!
// T* foo = array[i];
//
// This array will accept null as an argument for any object, and will
// store null in the array, just like nsVoidArray. But that also means
// that methods like ObjectAt() may return null when referring to an
// existing, but null entry in the array.
template <class T>
class nsCOMArray : public nsCOMArray_base
{
 public:
    nsCOMArray() {}
    nsCOMArray(int32_t aCount) : nsCOMArray_base(aCount) {}
    
    // only to be used by trusted classes who are going to pass us the
    // right type!
    nsCOMArray(const nsCOMArray<T>& aOther) : nsCOMArray_base(aOther) { }

    ~nsCOMArray() {}

    // these do NOT refcount on the way out, for speed
    T* ObjectAt(int32_t aIndex) const {
        return static_cast<T*>(nsCOMArray_base::ObjectAt(aIndex));
    }

    // these do NOT refcount on the way out, for speed
    T* SafeObjectAt(int32_t aIndex) const {
        return static_cast<T*>(nsCOMArray_base::SafeObjectAt(aIndex));
    }

    // indexing operator for syntactic sugar
    T* operator[](int32_t aIndex) const {
        return ObjectAt(aIndex);
    }

    // index of the element in question.. does NOT refcount
    // note: this does not check COM object identity. Use
    // IndexOfObject() for that purpose
    int32_t IndexOf(T* aObject) const {
        return nsCOMArray_base::IndexOf(static_cast<nsISupports*>(aObject));
    }

    // index of the element in question.. be careful!
    // this is much slower than IndexOf() because it uses
    // QueryInterface to determine actual COM identity of the object
    // if you need to do this frequently then consider enforcing
    // COM object identity before adding/comparing elements
    int32_t IndexOfObject(T* aObject) const {
        return nsCOMArray_base::IndexOfObject(static_cast<nsISupports*>(aObject));
    }

    // inserts aObject at aIndex, shifting the objects at aIndex and
    // later to make space
    bool InsertObjectAt(T* aObject, int32_t aIndex) {
        return nsCOMArray_base::InsertObjectAt(static_cast<nsISupports*>(aObject), aIndex);
    }

    // inserts the objects from aObject at aIndex, shifting the
    // objects at aIndex and later to make space
    bool InsertObjectsAt(const nsCOMArray<T>& aObjects, int32_t aIndex) {
        return nsCOMArray_base::InsertObjectsAt(aObjects, aIndex);
    }

    // replaces an existing element. Warning: if the array grows,
    // the newly created entries will all be null
    bool ReplaceObjectAt(T* aObject, int32_t aIndex) {
        return nsCOMArray_base::ReplaceObjectAt(static_cast<nsISupports*>(aObject), aIndex);
    }

    // override nsVoidArray stuff so that they can be accessed by
    // other methods

    // elements in the array (including null elements!)
    int32_t Count() const {
        return nsCOMArray_base::Count();
    }

    // remove all elements in the array, and call NS_RELEASE on each one
    void Clear() {
        nsCOMArray_base::Clear();
    }

    // Enumerator callback function. Return false to stop
    // Here's a more readable form:
    // bool enumerate(T* aElement, void* aData)
    typedef bool (* nsCOMArrayEnumFunc)
        (T* aElement, void *aData);
    
    // enumerate through the array with a callback. 
    bool EnumerateForwards(nsCOMArrayEnumFunc aFunc, void* aData) {
        return nsCOMArray_base::EnumerateForwards(nsVoidArrayEnumFunc(aFunc),
                                                  aData);
    }

    bool EnumerateBackwards(nsCOMArrayEnumFunc aFunc, void* aData) {
        return nsCOMArray_base::EnumerateBackwards(nsVoidArrayEnumFunc(aFunc),
                                                  aData);
    }
    
    typedef int (* nsCOMArrayComparatorFunc)
        (T* aElement1, T* aElement2, void* aData);
        
    void Sort(nsCOMArrayComparatorFunc aFunc, void* aData) {
        nsCOMArray_base::Sort(nsVoidArrayComparatorFunc(aFunc), aData);
    }

    // append an object, growing the array as necessary
    bool AppendObject(T *aObject) {
        return nsCOMArray_base::AppendObject(static_cast<nsISupports*>(aObject));
    }

    // append objects, growing the array as necessary
    bool AppendObjects(const nsCOMArray<T>& aObjects) {
        return nsCOMArray_base::AppendObjects(aObjects);
    }
    
    // remove the first instance of the given object and shrink the
    // array as necessary
    // Warning: if you pass null here, it will remove the first null element
    bool RemoveObject(T *aObject) {
        return nsCOMArray_base::RemoveObject(static_cast<nsISupports*>(aObject));
    }

    // remove an element at a specific position, shrinking the array
    // as necessary
    bool RemoveObjectAt(int32_t aIndex) {
        return nsCOMArray_base::RemoveObjectAt(aIndex);
    }

    // remove a range of elements at a specific position, shrinking the array
    // as necessary
    bool RemoveObjectsAt(int32_t aIndex, int32_t aCount) {
        return nsCOMArray_base::RemoveObjectsAt(aIndex, aCount);
    }

    // Each element in an nsCOMArray<T> is actually a T*, so this function is
    // "IncludingThis" rather than "ExcludingThis" because it needs to measure
    // the memory taken by the T itself as well as anything it points to.
    typedef size_t (* nsCOMArraySizeOfElementIncludingThisFunc)
        (T* aElement, nsMallocSizeOfFun aMallocSizeOf, void *aData);
    
    size_t SizeOfExcludingThis(
             nsCOMArraySizeOfElementIncludingThisFunc aSizeOfElementIncludingThis, 
             nsMallocSizeOfFun aMallocSizeOf, void *aData = NULL) const {
        return nsCOMArray_base::SizeOfExcludingThis(
                 nsVoidArraySizeOfElementIncludingThisFunc(aSizeOfElementIncludingThis),
                 aMallocSizeOf, aData);
    }

private:

    // don't implement these!
    nsCOMArray<T>& operator=(const nsCOMArray<T>& other) MOZ_DELETE;
};

template <typename T>
inline void
ImplCycleCollectionUnlink(nsCOMArray<T>& aField)
{
    aField.Clear();
}

template <typename E>
inline void
ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& aCallback,
                            nsCOMArray<E>& aField,
                            const char* aName,
                            uint32_t aFlags = 0)
{
    aFlags |= CycleCollectionEdgeNameArrayFlag;
    size_t length = aField.Count();
    for (size_t i = 0; i < length; ++i) {
        CycleCollectionNoteChild(aCallback, aField[i], aName, aFlags);
    }
}

#endif
