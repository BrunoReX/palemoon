/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Initial Developer of the Original Code is
 *   Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com> (original author)
 *   Mark Steele <mwsteele@gmail.com>
 *   Cedric Vivier <cedricv@neonux.com>
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

#include "WebGLContext.h"

#include "nsString.h"

#include "gfxImageSurface.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
//#include "nsIDOMHTMLCanvasElement.h"

#include "nsContentUtils.h"
#include "nsDOMError.h"
#include "nsLayoutUtils.h"

#include "CanvasUtils.h"

#include "jstypedarray.h"

#if defined(USE_ANGLE)
// shader translator
#include "angle/ShaderLang.h"
#endif

#include "WebGLTexelConversions.h"

using namespace mozilla;

static PRBool BaseTypeAndSizeFromUniformType(WebGLenum uType, WebGLenum *baseType, WebGLint *unitSize);

/* Helper macros for when we're just wrapping a gl method, so that
 * we can avoid having to type this 500 times.  Note that these MUST
 * NOT BE USED if we need to check any of the parameters.
 */

#define GL_SAME_METHOD_0(glname, name)                          \
NS_IMETHODIMP WebGLContext::name() {                            \
    MakeContextCurrent(); gl->f##glname(); return NS_OK;        \
}

#define GL_SAME_METHOD_1(glname, name, t1)          \
NS_IMETHODIMP WebGLContext::name(t1 a1) {           \
    MakeContextCurrent(); gl->f##glname(a1); return NS_OK;  \
}

#define GL_SAME_METHOD_2(glname, name, t1, t2)        \
NS_IMETHODIMP WebGLContext::name(t1 a1, t2 a2) {      \
    MakeContextCurrent(); gl->f##glname(a1,a2); return NS_OK;           \
}

#define GL_SAME_METHOD_3(glname, name, t1, t2, t3)      \
NS_IMETHODIMP WebGLContext::name(t1 a1, t2 a2, t3 a3) { \
    MakeContextCurrent(); gl->f##glname(a1,a2,a3); return NS_OK;        \
}

#define GL_SAME_METHOD_4(glname, name, t1, t2, t3, t4)         \
NS_IMETHODIMP WebGLContext::name(t1 a1, t2 a2, t3 a3, t4 a4) { \
    MakeContextCurrent(); gl->f##glname(a1,a2,a3,a4); return NS_OK;     \
}

#define GL_SAME_METHOD_5(glname, name, t1, t2, t3, t4, t5)            \
NS_IMETHODIMP WebGLContext::name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5) { \
    MakeContextCurrent(); gl->f##glname(a1,a2,a3,a4,a5); return NS_OK;  \
}

#define GL_SAME_METHOD_6(glname, name, t1, t2, t3, t4, t5, t6)          \
NS_IMETHODIMP WebGLContext::name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, t6 a6) { \
    MakeContextCurrent(); gl->f##glname(a1,a2,a3,a4,a5,a6); return NS_OK; \
}

already_AddRefed<WebGLUniformLocation>
WebGLProgram::GetUniformLocationObject(GLint glLocation)
{
    WebGLUniformLocation *existingLocationObject;
    if (mMapUniformLocations.Get(glLocation, &existingLocationObject)) {
        return existingLocationObject;
    }

    if (glLocation < 0) {
        return nsnull;
    }

    nsRefPtr<WebGLUniformLocation> loc = new WebGLUniformLocation(mContext, this, glLocation);
    mMapUniformLocations.Put(glLocation, loc);
    return loc.forget();
}

//
//  WebGL API
//


/* void GlActiveTexture (in GLenum texture); */
NS_IMETHODIMP
WebGLContext::ActiveTexture(WebGLenum texture)
{
    if (texture < LOCAL_GL_TEXTURE0 || texture >= LOCAL_GL_TEXTURE0+mBound2DTextures.Length())
        return ErrorInvalidEnum("ActiveTexture: texture unit %d out of range (0..%d)",
                                texture, mBound2DTextures.Length()-1);

    MakeContextCurrent();
    mActiveTexture = texture - LOCAL_GL_TEXTURE0;
    gl->fActiveTexture(texture);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::AttachShader(nsIWebGLProgram *pobj, nsIWebGLShader *shobj)
{
    // if pobj or shobj are null/not specified, it's an error
    if (pobj == nsnull || shobj == nsnull)
        return ErrorInvalidValue("attachShader");

    WebGLuint progname, shadername;
    WebGLProgram *program;
    WebGLShader *shader;
    if (!GetConcreteObjectAndGLName("attachShader: program", pobj, &program, &progname) ||
        !GetConcreteObjectAndGLName("attachShader: shader", shobj, &shader, &shadername))
        return NS_OK;

    // Per GLSL ES 2.0, we can only have one of each type of shader
    // attached.  This renders the next test somewhat moot, but we'll
    // leave it for when we support more than one shader of each type.
    if (program->HasAttachedShaderOfType(shader->ShaderType()))
        return ErrorInvalidOperation("AttachShader: only one of each type of shader may be attached to a program");

    if (!program->AttachShader(shader))
        return ErrorInvalidOperation("AttachShader: shader is already attached");

    MakeContextCurrent();

    gl->fAttachShader(progname, shadername);

    return NS_OK;
}


NS_IMETHODIMP
WebGLContext::BindAttribLocation(nsIWebGLProgram *pobj, WebGLuint location, const nsAString& name)
{
    WebGLuint progname;
    if (!GetGLName<WebGLProgram>("bindAttribLocation: program", pobj, &progname))
        return NS_OK;

    if (name.IsEmpty())
        return ErrorInvalidValue("BindAttribLocation: name can't be null or empty");

    if (!ValidateAttribIndex(location, "bindAttribLocation"))
        return NS_OK;

    MakeContextCurrent();

    gl->fBindAttribLocation(progname, location, NS_LossyConvertUTF16toASCII(name).get());

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BindBuffer(WebGLenum target, nsIWebGLBuffer *bobj)
{
    WebGLuint bufname;
    WebGLBuffer* buf;
    PRBool isNull;
    if (!GetConcreteObjectAndGLName("bindBuffer", bobj, &buf, &bufname, &isNull))
        return NS_OK;

    if (target != LOCAL_GL_ARRAY_BUFFER &&
        target != LOCAL_GL_ELEMENT_ARRAY_BUFFER)
    {
        return ErrorInvalidEnumInfo("bindBuffer: target", target);
    }

    if (!isNull) {
        if ((buf->Target() != LOCAL_GL_NONE) && (target != buf->Target()))
            return ErrorInvalidOperation("BindBuffer: buffer already bound to a different target");
        buf->SetTarget(target);
        buf->SetHasEverBeenBound(PR_TRUE);
    }

    // we really want to do this AFTER all the validation is done, otherwise our bookkeeping could get confused.
    // see bug 656752
    if (target == LOCAL_GL_ARRAY_BUFFER) {
        mBoundArrayBuffer = buf;
    } else if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
        mBoundElementArrayBuffer = buf;
    }

    MakeContextCurrent();

    gl->fBindBuffer(target, bufname);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BindFramebuffer(WebGLenum target, nsIWebGLFramebuffer *fbobj)
{
    WebGLuint framebuffername;
    PRBool isNull;
    WebGLFramebuffer *wfb;

    if (target != LOCAL_GL_FRAMEBUFFER)
        return ErrorInvalidEnum("BindFramebuffer: target must be GL_FRAMEBUFFER");

    if (!GetConcreteObjectAndGLName("bindFramebuffer", fbobj, &wfb, &framebuffername, &isNull))
        return NS_OK;

    MakeContextCurrent();

    if (isNull) {
        gl->fBindFramebuffer(target, gl->GetOffscreenFBO());
    } else {
        gl->fBindFramebuffer(target, framebuffername);
        wfb->SetHasEverBeenBound(PR_TRUE);
    }

    mBoundFramebuffer = wfb;

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BindRenderbuffer(WebGLenum target, nsIWebGLRenderbuffer *rbobj)
{
    WebGLuint renderbuffername;
    PRBool isNull;
    WebGLRenderbuffer *wrb;

    if (target != LOCAL_GL_RENDERBUFFER)
        return ErrorInvalidEnumInfo("bindRenderbuffer: target", target);

    if (!GetConcreteObjectAndGLName("bindRenderBuffer", rbobj, &wrb, &renderbuffername, &isNull))
        return NS_OK;

    if (!isNull)
        wrb->SetHasEverBeenBound(PR_TRUE);

    MakeContextCurrent();

    gl->fBindRenderbuffer(target, renderbuffername);

    mBoundRenderbuffer = wrb;

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BindTexture(WebGLenum target, nsIWebGLTexture *tobj)
{
    WebGLuint texturename;
    WebGLTexture *tex;
    PRBool isNull; // allow null object
    if (!GetConcreteObjectAndGLName("bindTexture", tobj, &tex, &texturename, &isNull))
        return NS_OK;

    if (target == LOCAL_GL_TEXTURE_2D) {
        mBound2DTextures[mActiveTexture] = tex;
    } else if (target == LOCAL_GL_TEXTURE_CUBE_MAP) {
        mBoundCubeMapTextures[mActiveTexture] = tex;
    } else {
        return ErrorInvalidEnumInfo("bindTexture: target", target);
    }

    MakeContextCurrent();

    if (tex)
        tex->Bind(target);
    else
        gl->fBindTexture(target, 0 /* == texturename */);

    return NS_OK;
}

GL_SAME_METHOD_4(BlendColor, BlendColor, WebGLfloat, WebGLfloat, WebGLfloat, WebGLfloat)

NS_IMETHODIMP WebGLContext::BlendEquation(WebGLenum mode)
{
    if (!ValidateBlendEquationEnum(mode, "blendEquation: mode"))
        return NS_OK;

    MakeContextCurrent();
    gl->fBlendEquation(mode);
    return NS_OK;
}

NS_IMETHODIMP WebGLContext::BlendEquationSeparate(WebGLenum modeRGB, WebGLenum modeAlpha)
{
    if (!ValidateBlendEquationEnum(modeRGB, "blendEquationSeparate: modeRGB") ||
        !ValidateBlendEquationEnum(modeAlpha, "blendEquationSeparate: modeAlpha"))
        return NS_OK;

    MakeContextCurrent();
    gl->fBlendEquationSeparate(modeRGB, modeAlpha);
    return NS_OK;
}

NS_IMETHODIMP WebGLContext::BlendFunc(WebGLenum sfactor, WebGLenum dfactor)
{
    if (!ValidateBlendFuncSrcEnum(sfactor, "blendFunc: sfactor") ||
        !ValidateBlendFuncDstEnum(dfactor, "blendFunc: dfactor"))
        return NS_OK;

    if (!ValidateBlendFuncEnumsCompatibility(sfactor, dfactor, "blendFuncSeparate: srcRGB and dstRGB"))
        return NS_OK;

    MakeContextCurrent();
    gl->fBlendFunc(sfactor, dfactor);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BlendFuncSeparate(WebGLenum srcRGB, WebGLenum dstRGB,
                                WebGLenum srcAlpha, WebGLenum dstAlpha)
{
    if (!ValidateBlendFuncSrcEnum(srcRGB, "blendFuncSeparate: srcRGB") ||
        !ValidateBlendFuncSrcEnum(srcAlpha, "blendFuncSeparate: srcAlpha") ||
        !ValidateBlendFuncDstEnum(dstRGB, "blendFuncSeparate: dstRGB") ||
        !ValidateBlendFuncDstEnum(dstAlpha, "blendFuncSeparate: dstAlpha"))
        return NS_OK;

    // note that we only check compatibity for the RGB enums, no need to for the Alpha enums, see
    // "Section 6.8 forgetting to mention alpha factors?" thread on the public_webgl mailing list
    if (!ValidateBlendFuncEnumsCompatibility(srcRGB, dstRGB, "blendFuncSeparate: srcRGB and dstRGB"))
        return NS_OK;

    MakeContextCurrent();
    gl->fBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BufferData(PRInt32 dummy)
{
    // this should never be called
    LogMessageIfVerbose("BufferData");
    return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
WebGLContext::BufferData_null()
{
    // see http://www.khronos.org/bugzilla/show_bug.cgi?id=386
    return ErrorInvalidValue("bufferData: null object passed");
}


NS_IMETHODIMP
WebGLContext::BufferData_size(WebGLenum target, WebGLsizei size, WebGLenum usage)
{
    WebGLBuffer *boundBuffer = NULL;

    if (target == LOCAL_GL_ARRAY_BUFFER) {
        boundBuffer = mBoundArrayBuffer;
    } else if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
        boundBuffer = mBoundElementArrayBuffer;
    } else {
        return ErrorInvalidEnumInfo("bufferData: target", target);
    }

    if (size < 0)
        return ErrorInvalidValue("bufferData: negative size");

    if (!ValidateBufferUsageEnum(usage, "bufferData: usage"))
        return NS_OK;

    if (!boundBuffer)
        return ErrorInvalidOperation("BufferData: no buffer bound!");

    MakeContextCurrent();

    boundBuffer->SetByteLength(size);
    if (!boundBuffer->ZeroDataIfElementArray())
        return ErrorOutOfMemory("bufferData: out of memory");
    boundBuffer->InvalidateCachedMaxElements();

    gl->fBufferData(target, size, 0, usage);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BufferData_buf(WebGLenum target, js::ArrayBuffer *wb, WebGLenum usage)
{
    WebGLBuffer *boundBuffer = NULL;

    if (target == LOCAL_GL_ARRAY_BUFFER) {
        boundBuffer = mBoundArrayBuffer;
    } else if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
        boundBuffer = mBoundElementArrayBuffer;
    } else {
        return ErrorInvalidEnumInfo("bufferData: target", target);
    }

    if (!ValidateBufferUsageEnum(usage, "bufferData: usage"))
        return NS_OK;

    if (!boundBuffer)
        return ErrorInvalidOperation("BufferData: no buffer bound!");

    MakeContextCurrent();

    boundBuffer->SetByteLength(wb->byteLength);
    if (!boundBuffer->CopyDataIfElementArray(wb->data))
        return ErrorOutOfMemory("bufferData: out of memory");
    boundBuffer->InvalidateCachedMaxElements();

    gl->fBufferData(target, wb->byteLength, wb->data, usage);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BufferData_array(WebGLenum target, js::TypedArray *wa, WebGLenum usage)
{
    WebGLBuffer *boundBuffer = NULL;

    if (target == LOCAL_GL_ARRAY_BUFFER) {
        boundBuffer = mBoundArrayBuffer;
    } else if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
        boundBuffer = mBoundElementArrayBuffer;
    } else {
        return ErrorInvalidEnumInfo("bufferData: target", target);
    }

    if (!ValidateBufferUsageEnum(usage, "bufferData: usage"))
        return NS_OK;

    if (!boundBuffer)
        return ErrorInvalidOperation("BufferData: no buffer bound!");

    MakeContextCurrent();

    boundBuffer->SetByteLength(wa->byteLength);
    if (!boundBuffer->CopyDataIfElementArray(wa->data))
        return ErrorOutOfMemory("bufferData: out of memory");
    boundBuffer->InvalidateCachedMaxElements();

    gl->fBufferData(target, wa->byteLength, wa->data, usage);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BufferSubData(PRInt32 dummy)
{
    return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
WebGLContext::BufferSubData_null()
{
    return NS_OK; // see http://www.khronos.org/bugzilla/show_bug.cgi?id=386
}

NS_IMETHODIMP
WebGLContext::BufferSubData_buf(GLenum target, WebGLsizei byteOffset, js::ArrayBuffer *wb)
{
    WebGLBuffer *boundBuffer = NULL;

    if (target == LOCAL_GL_ARRAY_BUFFER) {
        boundBuffer = mBoundArrayBuffer;
    } else if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
        boundBuffer = mBoundElementArrayBuffer;
    } else {
        return ErrorInvalidEnumInfo("bufferSubData: target", target);
    }

    if (byteOffset < 0)
        return ErrorInvalidValue("bufferSubData: negative offset");

    if (!boundBuffer)
        return ErrorInvalidOperation("BufferData: no buffer bound!");

    CheckedUint32 checked_neededByteLength = CheckedUint32(byteOffset) + wb->byteLength;
    if (!checked_neededByteLength.valid())
        return ErrorInvalidOperation("bufferSubData: integer overflow computing the needed byte length");

    if (checked_neededByteLength.value() > boundBuffer->ByteLength())
        return ErrorInvalidOperation("BufferSubData: not enough data - operation requires %d bytes, but buffer only has %d bytes",
                                     byteOffset, wb->byteLength, boundBuffer->ByteLength());

    MakeContextCurrent();

    boundBuffer->CopySubDataIfElementArray(byteOffset, wb->byteLength, wb->data);
    boundBuffer->InvalidateCachedMaxElements();

    gl->fBufferSubData(target, byteOffset, wb->byteLength, wb->data);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::BufferSubData_array(WebGLenum target, WebGLsizei byteOffset, js::TypedArray *wa)
{
    WebGLBuffer *boundBuffer = NULL;

    if (target == LOCAL_GL_ARRAY_BUFFER) {
        boundBuffer = mBoundArrayBuffer;
    } else if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
        boundBuffer = mBoundElementArrayBuffer;
    } else {
        return ErrorInvalidEnumInfo("bufferSubData: target", target);
    }

    if (byteOffset < 0)
        return ErrorInvalidValue("bufferSubData: negative offset");

    if (!boundBuffer)
        return ErrorInvalidOperation("BufferData: no buffer bound!");

    CheckedUint32 checked_neededByteLength = CheckedUint32(byteOffset) + wa->byteLength;
    if (!checked_neededByteLength.valid())
        return ErrorInvalidOperation("bufferSubData: integer overflow computing the needed byte length");

    if (checked_neededByteLength.value() > boundBuffer->ByteLength())
        return ErrorInvalidOperation("BufferSubData: not enough data -- operation requires %d bytes, but buffer only has %d bytes",
                                     byteOffset, wa->byteLength, boundBuffer->ByteLength());

    MakeContextCurrent();

    boundBuffer->CopySubDataIfElementArray(byteOffset, wa->byteLength, wa->data);
    boundBuffer->InvalidateCachedMaxElements();

    gl->fBufferSubData(target, byteOffset, wa->byteLength, wa->data);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::CheckFramebufferStatus(WebGLenum target, WebGLenum *retval)
{
    *retval = 0;

    MakeContextCurrent();
    if (target != LOCAL_GL_FRAMEBUFFER)
        return ErrorInvalidEnum("checkFramebufferStatus: target must be FRAMEBUFFER");

    if (mBoundFramebuffer && mBoundFramebuffer->HasBadAttachments())
        *retval = LOCAL_GL_FRAMEBUFFER_UNSUPPORTED;
    else
        *retval = gl->fCheckFramebufferStatus(target);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::Clear(PRUint32 mask)
{
    MakeContextCurrent();

    if (mBoundFramebuffer && !mBoundFramebuffer->CheckAndInitializeRenderbuffers())
        return NS_OK;

    PRUint32 m = mask & (LOCAL_GL_COLOR_BUFFER_BIT | LOCAL_GL_DEPTH_BUFFER_BIT | LOCAL_GL_STENCIL_BUFFER_BIT);
    if (mask != m) {
        return ErrorInvalidValue("clear: invalid mask bits");
    }

    gl->fClear(mask);
    Invalidate();

    return NS_OK;
}

GL_SAME_METHOD_4(ClearColor, ClearColor, WebGLfloat, WebGLfloat, WebGLfloat, WebGLfloat)

GL_SAME_METHOD_1(ClearDepth, ClearDepth, WebGLfloat)

GL_SAME_METHOD_1(ClearStencil, ClearStencil, WebGLint)

GL_SAME_METHOD_4(ColorMask, ColorMask, WebGLboolean, WebGLboolean, WebGLboolean, WebGLboolean)

nsresult
WebGLContext::CopyTexSubImage2D_base(WebGLenum target,
                                     WebGLint level,
                                     WebGLenum internalformat,
                                     WebGLint xoffset,
                                     WebGLint yoffset,
                                     WebGLint x,
                                     WebGLint y,
                                     WebGLsizei width,
                                     WebGLsizei height,
                                     bool sub)
{

    WebGLsizei framebufferWidth =  mBoundFramebuffer ? mBoundFramebuffer->width() : mWidth;
    WebGLsizei framebufferHeight = mBoundFramebuffer ? mBoundFramebuffer->height() : mHeight;

    const char *info = sub ? "copyTexSubImage2D" : "copyTexImage2D";

    MakeContextCurrent();

    if (CanvasUtils::CheckSaneSubrectSize(x, y, width, height, framebufferWidth, framebufferHeight)) {
        if (sub)
          gl->fCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
        else
          gl->fCopyTexImage2D(target, level, internalformat, x, y, width, height, 0);
    } else {

        // the rect doesn't fit in the framebuffer

        /*** first, we initialize the texture as black ***/

        // first, compute the size of the buffer we should allocate to initialize the texture as black

        PRUint32 texelSize = 0;
        if (!ValidateTexFormatAndType(internalformat, LOCAL_GL_UNSIGNED_BYTE, &texelSize, info))
            return NS_OK;

        CheckedUint32 checked_plainRowSize = CheckedUint32(width) * texelSize;

        PRUint32 unpackAlignment = mPixelStoreUnpackAlignment;

        // alignedRowSize = row size rounded up to next multiple of packAlignment
        CheckedUint32 checked_alignedRowSize
            = ((checked_plainRowSize + unpackAlignment-1) / unpackAlignment) * unpackAlignment;

        CheckedUint32 checked_neededByteLength
            = (height-1) * checked_alignedRowSize + checked_plainRowSize;

        if (!checked_neededByteLength.valid())
            return ErrorInvalidOperation("%s: integer overflow computing the needed buffer size", info);

        PRUint32 bytesNeeded = checked_neededByteLength.value();

        // now that the size is known, create the buffer

        // We need some zero pages, because GL doesn't guarantee the
        // contents of a texture allocated with NULL data.
        // Hopefully calloc will just mmap zero pages here.
        void *tempZeroData = calloc(1, bytesNeeded);
        if (!tempZeroData)
            return ErrorOutOfMemory("%s: could not allocate %d bytes (for zero fill)", info, bytesNeeded);

        // now initialize the texture as black

        if (sub)
          gl->fTexSubImage2D(target, level, 0, 0, width, height, internalformat, LOCAL_GL_UNSIGNED_BYTE, tempZeroData);
        else
          gl->fTexImage2D(target, level, internalformat, width, height, 0, internalformat, LOCAL_GL_UNSIGNED_BYTE, tempZeroData);

        free(tempZeroData);

        // if we are completely outside of the framebuffer, we can exit now with our black texture
        if (   x >= framebufferWidth
            || x+width <= 0
            || y >= framebufferHeight
            || y+height <= 0)
        {
            // we are completely outside of range, can exit now with buffer filled with zeros
            return NS_OK;
        }

        GLint   actual_x             = PR_MIN(framebufferWidth, PR_MAX(0, x));
        GLint   actual_x_plus_width  = PR_MIN(framebufferWidth, PR_MAX(0, x + width));
        GLsizei actual_width   = actual_x_plus_width  - actual_x;
        GLint   actual_xoffset = xoffset + actual_x - x;

        GLint   actual_y             = PR_MIN(framebufferHeight, PR_MAX(0, y));
        GLint   actual_y_plus_height = PR_MIN(framebufferHeight, PR_MAX(0, y + height));
        GLsizei actual_height  = actual_y_plus_height - actual_y;
        GLint   actual_yoffset = yoffset + actual_y - y;

        gl->fCopyTexSubImage2D(target, level, actual_xoffset, actual_yoffset, actual_x, actual_y, actual_width, actual_height);
    }

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::CopyTexImage2D(WebGLenum target,
                             WebGLint level,
                             WebGLenum internalformat,
                             WebGLint x,
                             WebGLint y,
                             WebGLsizei width,
                             WebGLsizei height,
                             WebGLint border)
{
    switch (target) {
        case LOCAL_GL_TEXTURE_2D:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            break;
        default:
            return ErrorInvalidEnumInfo("copyTexImage2D: target", target);
    }


    switch (internalformat) {
        case LOCAL_GL_RGB:
        case LOCAL_GL_LUMINANCE:
        case LOCAL_GL_RGBA:
        case LOCAL_GL_ALPHA:
        case LOCAL_GL_LUMINANCE_ALPHA:
            break;
        default:
            return ErrorInvalidEnumInfo("CopyTexImage2D: internal format", internalformat);
    }

    if (border != 0)
        return ErrorInvalidValue("copyTexImage2D: border must be 0");

    if (width < 0 || height < 0)
        return ErrorInvalidValue("copyTexImage2D: width and height may not be negative");

    if (level < 0)
        return ErrorInvalidValue("copyTexImage2D: level may not be negative");

    WebGLsizei maxTextureSize = MaxTextureSizeForTarget(target);
    if (!(maxTextureSize >> level))
        return ErrorInvalidValue("copyTexImage2D: 2^level exceeds maximum texture size");

    if (level >= 1) {
        if (!(is_pot_assuming_nonnegative(width) &&
              is_pot_assuming_nonnegative(height)))
            return ErrorInvalidValue("copyTexImage2D: with level > 0, width and height must be powers of two");
    }

    PRBool texFormatRequiresAlpha = internalformat == LOCAL_GL_RGBA ||
                                    internalformat == LOCAL_GL_ALPHA ||
                                    internalformat == LOCAL_GL_LUMINANCE_ALPHA;
    PRBool fboFormatHasAlpha = mBoundFramebuffer ? mBoundFramebuffer->ColorAttachment().HasAlpha()
                                                 : PRBool(gl->ActualFormat().alpha > 0);
    if (texFormatRequiresAlpha && !fboFormatHasAlpha)
        return ErrorInvalidOperation("copyTexImage2D: texture format requires an alpha channel "
                                     "but the framebuffer doesn't have one");

    if (mBoundFramebuffer && !mBoundFramebuffer->CheckAndInitializeRenderbuffers())
        return NS_OK;

    WebGLTexture *tex = activeBoundTextureForTarget(target);
    if (!tex)
        return ErrorInvalidOperation("copyTexImage2D: no texture bound to this target");

    tex->SetImageInfo(target, level, width, height);

    return CopyTexSubImage2D_base(target, level, internalformat, 0, 0, x, y, width, height, false);
}

NS_IMETHODIMP
WebGLContext::CopyTexSubImage2D(WebGLenum target,
                                WebGLint level,
                                WebGLint xoffset,
                                WebGLint yoffset,
                                WebGLint x,
                                WebGLint y,
                                WebGLsizei width,
                                WebGLsizei height)
{
    switch (target) {
        case LOCAL_GL_TEXTURE_2D:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            break;
        default:
            return ErrorInvalidEnumInfo("CopyTexSubImage2D: target", target);
    }

    if (level < 0)
        return ErrorInvalidValue("copyTexSubImage2D: level may not be negative");

    WebGLsizei maxTextureSize = MaxTextureSizeForTarget(target);
    if (!(maxTextureSize >> level))
        return ErrorInvalidValue("copyTexSubImage2D: 2^level exceeds maximum texture size");

    if (width < 0 || height < 0)
        return ErrorInvalidValue("copyTexSubImage2D: width and height may not be negative");

    if (xoffset < 0 || yoffset < 0)
        return ErrorInvalidValue("copyTexSubImage2D: xoffset and yoffset may not be negative");

    WebGLTexture *tex = activeBoundTextureForTarget(target);
    if (!tex)
        return ErrorInvalidOperation("copyTexSubImage2D: no texture bound to this target");

    WebGLint face = WebGLTexture::FaceForTarget(target);
    if (!tex->HasImageInfoAt(level, face))
        return ErrorInvalidOperation("copyTexSubImage2D: to texture image previously defined for this level and face");

    WebGLsizei texWidth = tex->ImageInfoAt(level, face).mWidth;
    WebGLsizei texHeight = tex->ImageInfoAt(level, face).mHeight;

    if (xoffset + width > texWidth || xoffset + width < 0)
      return ErrorInvalidValue("copyTexSubImage2D: xoffset+width is too large");

    if (yoffset + height > texHeight || yoffset + height < 0)
      return ErrorInvalidValue("copyTexSubImage2D: yoffset+height is too large");

    WebGLenum format = tex->ImageInfoAt(level, face).mFormat;
    PRBool texFormatRequiresAlpha = format == LOCAL_GL_RGBA ||
                                    format == LOCAL_GL_ALPHA ||
                                    format == LOCAL_GL_LUMINANCE_ALPHA;
    PRBool fboFormatHasAlpha = mBoundFramebuffer ? mBoundFramebuffer->ColorAttachment().HasAlpha()
                                                 : PRBool(gl->ActualFormat().alpha > 0);

    if (texFormatRequiresAlpha && !fboFormatHasAlpha)
        return ErrorInvalidOperation("copyTexSubImage2D: texture format requires an alpha channel "
                                     "but the framebuffer doesn't have one");

    if (mBoundFramebuffer && !mBoundFramebuffer->CheckAndInitializeRenderbuffers())
        return NS_OK;

    return CopyTexSubImage2D_base(target, level, format, xoffset, yoffset, x, y, width, height, true);
}


NS_IMETHODIMP
WebGLContext::CreateProgram(nsIWebGLProgram **retval)
{
    *retval = nsnull;

    MakeContextCurrent();

    WebGLuint name = gl->fCreateProgram();

    WebGLProgram *prog = new WebGLProgram(this, name);
    NS_ADDREF(*retval = prog);
    mMapPrograms.Put(name, prog);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::CreateShader(WebGLenum type, nsIWebGLShader **retval)
{
    *retval = nsnull;

    if (type != LOCAL_GL_VERTEX_SHADER &&
        type != LOCAL_GL_FRAGMENT_SHADER)
    {
        return ErrorInvalidEnumInfo("createShader: type", type);
    }

    MakeContextCurrent();

    WebGLuint name = gl->fCreateShader(type);

    WebGLShader *shader = new WebGLShader(this, name, type);
    NS_ADDREF(*retval = shader);
    mMapShaders.Put(name, shader);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::CullFace(WebGLenum face)
{
    if (!ValidateFaceEnum(face, "cullFace"))
        return NS_OK;

    MakeContextCurrent();
    gl->fCullFace(face);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DeleteBuffer(nsIWebGLBuffer *bobj)
{
    WebGLuint bufname;
    WebGLBuffer *buf;
    PRBool isNull, isDeleted;
    if (!GetConcreteObjectAndGLName("deleteBuffer", bobj, &buf, &bufname, &isNull, &isDeleted))
        return NS_OK;

    if (isNull || isDeleted)
        return NS_OK;

    MakeContextCurrent();

    gl->fDeleteBuffers(1, &bufname);
    buf->Delete();
    mMapBuffers.Remove(bufname);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DeleteFramebuffer(nsIWebGLFramebuffer *fbobj)
{
    WebGLuint fbufname;
    WebGLFramebuffer *fbuf;
    PRBool isNull, isDeleted;
    if (!GetConcreteObjectAndGLName("deleteFramebuffer", fbobj, &fbuf, &fbufname, &isNull, &isDeleted))
        return NS_OK;

    if (isNull || isDeleted)
        return NS_OK;

    MakeContextCurrent();

    gl->fDeleteFramebuffers(1, &fbufname);
    fbuf->Delete();
    mMapFramebuffers.Remove(fbufname);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DeleteRenderbuffer(nsIWebGLRenderbuffer *rbobj)
{
    WebGLuint rbufname;
    WebGLRenderbuffer *rbuf;
    PRBool isNull, isDeleted;
    if (!GetConcreteObjectAndGLName("deleteRenderbuffer", rbobj, &rbuf, &rbufname, &isNull, &isDeleted))
        return NS_OK;

    if (isNull || isDeleted)
        return NS_OK;

    MakeContextCurrent();

    // XXX we need to track renderbuffer attachments; from glDeleteRenderbuffers man page:

    /*
            If a renderbuffer object that is currently bound is deleted, the binding reverts
            to 0 (the absence of any renderbuffer object). Additionally, special care
            must be taken when deleting a renderbuffer object if the image of the renderbuffer
            is attached to a framebuffer object. In this case, if the deleted renderbuffer object is
            attached to the currently bound framebuffer object, it is 
            automatically detached.  However, attachments to any other framebuffer objects are the
            responsibility of the application.
    */

    gl->fDeleteRenderbuffers(1, &rbufname);

    rbuf->Delete();
    mMapRenderbuffers.Remove(rbufname);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DeleteTexture(nsIWebGLTexture *tobj)
{
    WebGLuint texname;
    WebGLTexture *tex;
    PRBool isNull, isDeleted;
    if (!GetConcreteObjectAndGLName("deleteTexture", tobj, &tex, &texname, &isNull, &isDeleted))
        return NS_OK;

    if (isNull || isDeleted)
        return NS_OK;

    MakeContextCurrent();

    gl->fDeleteTextures(1, &texname);
    tex->Delete();
    mMapTextures.Remove(texname);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DeleteProgram(nsIWebGLProgram *pobj)
{
    WebGLuint progname;
    WebGLProgram *prog;
    PRBool isNull, isDeleted;
    if (!GetConcreteObjectAndGLName("deleteProgram", pobj, &prog, &progname, &isNull, &isDeleted))
        return NS_OK;

    if (isNull || isDeleted)
        return NS_OK;

    MakeContextCurrent();

    gl->fDeleteProgram(progname);

    if (prog == mCurrentProgram) {
        prog->SetDeletePending();
    } else {
        prog->DetachShaders();
    }

    prog->Delete();
    mMapPrograms.Remove(progname);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DeleteShader(nsIWebGLShader *sobj)
{
    WebGLuint shadername;
    WebGLShader *shader;
    PRBool isNull, isDeleted;
    if (!GetConcreteObjectAndGLName("deleteShader", sobj, &shader, &shadername, &isNull, &isDeleted))
        return NS_OK;

    if (isNull || isDeleted)
        return NS_OK;

    MakeContextCurrent();

    gl->fDeleteShader(shadername);
    shader->Delete();
    mMapShaders.Remove(shadername);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DetachShader(nsIWebGLProgram *pobj, nsIWebGLShader *shobj)
{
    WebGLuint progname, shadername;
    WebGLProgram *program;
    WebGLShader *shader;
    PRBool shaderDeleted;
    if (!GetConcreteObjectAndGLName("detachShader: program", pobj, &program, &progname) ||
        !GetConcreteObjectAndGLName("detachShader: shader", shobj, &shader, &shadername, nsnull, &shaderDeleted))
        return NS_OK;

    // shaderDeleted is ignored -- it's valid to attempt to detach a
    // deleted shader, since it's still a shader
    if (!program->DetachShader(shader))
        return ErrorInvalidOperation("DetachShader: shader is not attached");

    MakeContextCurrent();

    gl->fDetachShader(progname, shadername);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DepthFunc(WebGLenum func)
{
    if (!ValidateComparisonEnum(func, "depthFunc"))
        return NS_OK;

    MakeContextCurrent();
    gl->fDepthFunc(func);
    return NS_OK;
}

GL_SAME_METHOD_1(DepthMask, DepthMask, WebGLboolean)

NS_IMETHODIMP
WebGLContext::DepthRange(WebGLfloat zNear, WebGLfloat zFar)
{
    if (zNear > zFar)
        return ErrorInvalidOperation("depthRange: the near value is greater than the far value!");

    MakeContextCurrent();
    gl->fDepthRange(zNear, zFar);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DisableVertexAttribArray(WebGLuint index)
{
    if (!ValidateAttribIndex(index, "disableVertexAttribArray"))
        return NS_OK;

    MakeContextCurrent();

    if (index || gl->IsGLES2())
        gl->fDisableVertexAttribArray(index);

    mAttribBuffers[index].enabled = PR_FALSE;

    return NS_OK;
}

int
WebGLContext::WhatDoesVertexAttrib0Need()
{
  // here we may assume that mCurrentProgram != null

    // work around Mac OSX crash, see bug 631420
#ifdef XP_MACOSX
    if (mAttribBuffers[0].enabled &&
        !mCurrentProgram->IsAttribInUse(0))
        return VertexAttrib0Status::EmulatedUninitializedArray;
#endif

    return (gl->IsGLES2() || mAttribBuffers[0].enabled) ? VertexAttrib0Status::Default
         : mCurrentProgram->IsAttribInUse(0)            ? VertexAttrib0Status::EmulatedInitializedArray
                                                        : VertexAttrib0Status::EmulatedUninitializedArray;
}

void
WebGLContext::DoFakeVertexAttrib0(WebGLuint vertexCount)
{
    int whatDoesAttrib0Need = WhatDoesVertexAttrib0Need();

    if (whatDoesAttrib0Need == VertexAttrib0Status::Default)
        return;

    WebGLuint dataSize = sizeof(WebGLfloat) * 4 * vertexCount;

    if (!mFakeVertexAttrib0BufferObject) {
        gl->fGenBuffers(1, &mFakeVertexAttrib0BufferObject);
    }

    // if the VBO status is already exactly what we need, or if the only difference is that it's initialized and
    // we don't need it to be, then consider it OK
    PRBool vertexAttrib0BufferStatusOK =
        mFakeVertexAttrib0BufferStatus == whatDoesAttrib0Need ||
        (mFakeVertexAttrib0BufferStatus == VertexAttrib0Status::EmulatedInitializedArray &&
         whatDoesAttrib0Need == VertexAttrib0Status::EmulatedUninitializedArray);

    if (!vertexAttrib0BufferStatusOK ||
        mFakeVertexAttrib0BufferObjectSize < dataSize ||
        mFakeVertexAttrib0BufferObjectVector[0] != mVertexAttrib0Vector[0] ||
        mFakeVertexAttrib0BufferObjectVector[1] != mVertexAttrib0Vector[1] ||
        mFakeVertexAttrib0BufferObjectVector[2] != mVertexAttrib0Vector[2] ||
        mFakeVertexAttrib0BufferObjectVector[3] != mVertexAttrib0Vector[3])
    {
        mFakeVertexAttrib0BufferStatus = whatDoesAttrib0Need;
        mFakeVertexAttrib0BufferObjectSize = dataSize;
        mFakeVertexAttrib0BufferObjectVector[0] = mVertexAttrib0Vector[0];
        mFakeVertexAttrib0BufferObjectVector[1] = mVertexAttrib0Vector[1];
        mFakeVertexAttrib0BufferObjectVector[2] = mVertexAttrib0Vector[2];
        mFakeVertexAttrib0BufferObjectVector[3] = mVertexAttrib0Vector[3];

        gl->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mFakeVertexAttrib0BufferObject);

        WebGLuint dataSize = sizeof(WebGLfloat) * 4 * vertexCount;

        if (mFakeVertexAttrib0BufferStatus == VertexAttrib0Status::EmulatedInitializedArray) {
            nsAutoArrayPtr<WebGLfloat> array(new WebGLfloat[4 * vertexCount]);
            for(size_t i = 0; i < vertexCount; ++i) {
                array[4 * i + 0] = mVertexAttrib0Vector[0];
                array[4 * i + 1] = mVertexAttrib0Vector[1];
                array[4 * i + 2] = mVertexAttrib0Vector[2];
                array[4 * i + 3] = mVertexAttrib0Vector[3];
            }
            gl->fBufferData(LOCAL_GL_ARRAY_BUFFER, dataSize, array, LOCAL_GL_DYNAMIC_DRAW);
        } else {
            gl->fBufferData(LOCAL_GL_ARRAY_BUFFER, dataSize, nsnull, LOCAL_GL_DYNAMIC_DRAW);
        }

        gl->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mBoundArrayBuffer ? mBoundArrayBuffer->GLName() : 0);
    }

    gl->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mFakeVertexAttrib0BufferObject);
    gl->fVertexAttribPointer(0, 4, LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0, 0);
}

void
WebGLContext::UndoFakeVertexAttrib0()
{
    int whatDoesAttrib0Need = WhatDoesVertexAttrib0Need();

    if (whatDoesAttrib0Need == VertexAttrib0Status::Default)
        return;

    gl->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mAttribBuffers[0].buf ? mAttribBuffers[0].buf->GLName() : 0);
    gl->fVertexAttribPointer(0,
                             mAttribBuffers[0].size,
                             mAttribBuffers[0].type,
                             mAttribBuffers[0].normalized,
                             mAttribBuffers[0].stride,
                             (const GLvoid *) mAttribBuffers[0].byteOffset);

    gl->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mBoundArrayBuffer ? mBoundArrayBuffer->GLName() : 0);
}

PRBool
WebGLContext::NeedFakeBlack()
{
    // handle this case first, it's the generic case
    if (mFakeBlackStatus == DoNotNeedFakeBlack)
        return PR_FALSE;

    if (mFakeBlackStatus == DontKnowIfNeedFakeBlack) {
        for (PRInt32 i = 0; i < mGLMaxTextureUnits; ++i) {
            if ((mBound2DTextures[i] && mBound2DTextures[i]->NeedFakeBlack()) ||
                (mBoundCubeMapTextures[i] && mBoundCubeMapTextures[i]->NeedFakeBlack()))
            {
                mFakeBlackStatus = DoNeedFakeBlack;
                break;
            }
        }

        // we have exhausted all cases where we do need fakeblack, so if the status is still unknown,
        // that means that we do NOT need it.
        if (mFakeBlackStatus == DontKnowIfNeedFakeBlack)
            mFakeBlackStatus = DoNotNeedFakeBlack;
    }

    return mFakeBlackStatus == DoNeedFakeBlack;
}

void
WebGLContext::BindFakeBlackTextures()
{
    // this is the generic case: try to return early
    if (!NeedFakeBlack())
        return;

    if (!mBlackTexturesAreInitialized) {
        const PRUint8 black[] = {0, 0, 0, 255};

        gl->fGenTextures(1, &mBlackTexture2D);
        gl->fBindTexture(LOCAL_GL_TEXTURE_2D, mBlackTexture2D);
        gl->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA, 1, 1,
                        0, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE, &black);

        gl->fGenTextures(1, &mBlackTextureCubeMap);
        gl->fBindTexture(LOCAL_GL_TEXTURE_CUBE_MAP, mBlackTextureCubeMap);
        for (WebGLuint i = 0; i < 6; ++i) {
            gl->fTexImage2D(LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, LOCAL_GL_RGBA, 1, 1,
                            0, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE, &black);
        }

        // return the texture bindings to the 0 texture to prevent the user from modifying our black textures
        gl->fBindTexture(LOCAL_GL_TEXTURE_2D, 0);
        gl->fBindTexture(LOCAL_GL_TEXTURE_CUBE_MAP, 0);

        mBlackTexturesAreInitialized = PR_TRUE;
    }

    for (PRInt32 i = 0; i < mGLMaxTextureUnits; ++i) {
        if (mBound2DTextures[i] && mBound2DTextures[i]->NeedFakeBlack()) {
            gl->fActiveTexture(LOCAL_GL_TEXTURE0 + i);
            gl->fBindTexture(LOCAL_GL_TEXTURE_2D, mBlackTexture2D);
        }
        if (mBoundCubeMapTextures[i] && mBoundCubeMapTextures[i]->NeedFakeBlack()) {
            gl->fActiveTexture(LOCAL_GL_TEXTURE0 + i);
            gl->fBindTexture(LOCAL_GL_TEXTURE_CUBE_MAP, mBlackTextureCubeMap);
        }
    }
}

void
WebGLContext::UnbindFakeBlackTextures()
{
    // this is the generic case: try to return early
    if (!NeedFakeBlack())
        return;

    for (PRInt32 i = 0; i < mGLMaxTextureUnits; ++i) {
        if (mBound2DTextures[i] && mBound2DTextures[i]->NeedFakeBlack()) {
            gl->fActiveTexture(LOCAL_GL_TEXTURE0 + i);
            gl->fBindTexture(LOCAL_GL_TEXTURE_2D, mBound2DTextures[i]->GLName());
        }
        if (mBoundCubeMapTextures[i] && mBoundCubeMapTextures[i]->NeedFakeBlack()) {
            gl->fActiveTexture(LOCAL_GL_TEXTURE0 + i);
            gl->fBindTexture(LOCAL_GL_TEXTURE_CUBE_MAP, mBoundCubeMapTextures[i]->GLName());
        }
    }

    gl->fActiveTexture(LOCAL_GL_TEXTURE0 + mActiveTexture);
}

NS_IMETHODIMP
WebGLContext::DrawArrays(GLenum mode, WebGLint first, WebGLsizei count)
{
    if (!ValidateDrawModeEnum(mode, "drawArrays: mode"))
        return NS_OK;

    if (first < 0 || count < 0)
        return ErrorInvalidValue("DrawArrays: negative first or count");

    // If count is 0, there's nothing to do.
    if (count == 0)
        return NS_OK;

    // If there is no current program, this is silently ignored.
    // Any checks below this depend on a program being available.
    if (!mCurrentProgram)
        return NS_OK;

    PRInt32 maxAllowedCount = 0;
    if (!ValidateBuffers(&maxAllowedCount, "drawArrays"))
        return NS_OK;

    CheckedInt32 checked_firstPlusCount = CheckedInt32(first) + count;

    if (!checked_firstPlusCount.valid())
        return ErrorInvalidOperation("drawArrays: overflow in first+count");

    if (checked_firstPlusCount.value() > maxAllowedCount)
        return ErrorInvalidOperation("drawArrays: bound vertex attribute buffers do not have sufficient size for given first and count");

    MakeContextCurrent();

    if (mBoundFramebuffer && !mBoundFramebuffer->CheckAndInitializeRenderbuffers())
        return NS_OK;

    BindFakeBlackTextures();
    DoFakeVertexAttrib0(checked_firstPlusCount.value());

    gl->fDrawArrays(mode, first, count);

    UndoFakeVertexAttrib0();
    UnbindFakeBlackTextures();

    Invalidate();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::DrawElements(WebGLenum mode, WebGLsizei count, WebGLenum type, WebGLint byteOffset)
{
    if (!ValidateDrawModeEnum(mode, "drawElements: mode"))
        return NS_OK;

    if (count < 0 || byteOffset < 0)
        return ErrorInvalidValue("DrawElements: negative count or offset");

    // If count is 0, there's nothing to do.
    if (count == 0)
        return NS_OK;

    CheckedUint32 checked_byteCount;

    if (type == LOCAL_GL_UNSIGNED_SHORT) {
        checked_byteCount = 2 * CheckedUint32(count);
        if (byteOffset % 2 != 0)
            return ErrorInvalidOperation("DrawElements: invalid byteOffset for UNSIGNED_SHORT (must be a multiple of 2)");
    } else if (type == LOCAL_GL_UNSIGNED_BYTE) {
        checked_byteCount = count;
    } else {
        return ErrorInvalidEnum("DrawElements: type must be UNSIGNED_SHORT or UNSIGNED_BYTE");
    }

    if (!checked_byteCount.valid())
        return ErrorInvalidValue("DrawElements: overflow in byteCount");

    // If there is no current program, this is silently ignored.
    // Any checks below this depend on a program being available.
    if (!mCurrentProgram)
        return NS_OK;

    if (!mBoundElementArrayBuffer)
        return ErrorInvalidOperation("DrawElements: must have element array buffer binding");

    if (!mBoundElementArrayBuffer->Data())
        return ErrorInvalidOperation("drawElements: bound element array buffer doesn't have any data");

    CheckedUint32 checked_neededByteCount = checked_byteCount + byteOffset;

    if (!checked_neededByteCount.valid())
        return ErrorInvalidOperation("DrawElements: overflow in byteOffset+byteCount");

    if (checked_neededByteCount.value() > mBoundElementArrayBuffer->ByteLength())
        return ErrorInvalidOperation("DrawElements: bound element array buffer is too small for given count and offset");

    PRInt32 maxAllowedCount = 0;
    if (!ValidateBuffers(&maxAllowedCount, "drawElements"))
      return NS_OK;

    PRInt32 maxIndex
      = type == LOCAL_GL_UNSIGNED_SHORT
        ? mBoundElementArrayBuffer->FindMaxUshortElement()
        : mBoundElementArrayBuffer->FindMaxUbyteElement();

    CheckedInt32 checked_maxIndexPlusOne = CheckedInt32(maxIndex) + 1;

    if (!checked_maxIndexPlusOne.valid() ||
        checked_maxIndexPlusOne.value() > maxAllowedCount)
    {
        // the index array contains invalid indices for the current drawing state, but they
        // might not be used by the present drawElements call, depending on first and count.

        PRInt32 maxIndexInSubArray
          = type == LOCAL_GL_UNSIGNED_SHORT
            ? mBoundElementArrayBuffer->FindMaxElementInSubArray<GLushort>(count, byteOffset)
            : mBoundElementArrayBuffer->FindMaxElementInSubArray<GLubyte>(count, byteOffset);

        CheckedInt32 checked_maxIndexInSubArrayPlusOne = CheckedInt32(maxIndexInSubArray) + 1;

        if (!checked_maxIndexInSubArrayPlusOne.valid() ||
            checked_maxIndexInSubArrayPlusOne.value() > maxAllowedCount)
        {
            return ErrorInvalidOperation(
                "DrawElements: bound vertex attribute buffers do not have sufficient "
                "size for given indices from the bound element array");
        }
    }

    MakeContextCurrent();

    if (mBoundFramebuffer && !mBoundFramebuffer->CheckAndInitializeRenderbuffers())
        return NS_OK;

    BindFakeBlackTextures();
    DoFakeVertexAttrib0(checked_maxIndexPlusOne.value());

    gl->fDrawElements(mode, count, type, (GLvoid*) (byteOffset));

    UndoFakeVertexAttrib0();
    UnbindFakeBlackTextures();

    Invalidate();

    return NS_OK;
}

NS_IMETHODIMP WebGLContext::Enable(WebGLenum cap)
{
    if (!ValidateCapabilityEnum(cap, "enable"))
        return NS_OK;

    MakeContextCurrent();
    gl->fEnable(cap);
    return NS_OK;
}

NS_IMETHODIMP WebGLContext::Disable(WebGLenum cap)
{
    if (!ValidateCapabilityEnum(cap, "disable"))
        return NS_OK;

    MakeContextCurrent();
    gl->fDisable(cap);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::EnableVertexAttribArray(WebGLuint index)
{
    if (!ValidateAttribIndex(index, "enableVertexAttribArray"))
        return NS_OK;

    MakeContextCurrent();

    gl->fEnableVertexAttribArray(index);
    mAttribBuffers[index].enabled = PR_TRUE;

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::FramebufferRenderbuffer(WebGLenum target, WebGLenum attachment, WebGLenum rbtarget, nsIWebGLRenderbuffer *rbobj)
{
    if (!mBoundFramebuffer)
        return ErrorInvalidOperation("framebufferRenderbuffer: cannot modify framebuffer 0");

    return mBoundFramebuffer->FramebufferRenderbuffer(target, attachment, rbtarget, rbobj);
}

NS_IMETHODIMP
WebGLContext::FramebufferTexture2D(WebGLenum target,
                                   WebGLenum attachment,
                                   WebGLenum textarget,
                                   nsIWebGLTexture *tobj,
                                   WebGLint level)
{
    if (mBoundFramebuffer)
        return mBoundFramebuffer->FramebufferTexture2D(target, attachment, textarget, tobj, level);
    else
        return ErrorInvalidOperation("framebufferTexture2D: cannot modify framebuffer 0");
}

GL_SAME_METHOD_0(Flush, Flush)

GL_SAME_METHOD_0(Finish, Finish)

NS_IMETHODIMP
WebGLContext::FrontFace(WebGLenum mode)
{
    switch (mode) {
        case LOCAL_GL_CW:
        case LOCAL_GL_CCW:
            break;
        default:
            return ErrorInvalidEnumInfo("frontFace: mode", mode);
    }

    MakeContextCurrent();
    gl->fFrontFace(mode);
    return NS_OK;
}

// returns an object: { size: ..., type: ..., name: ... }
NS_IMETHODIMP
WebGLContext::GetActiveAttrib(nsIWebGLProgram *pobj, PRUint32 index, nsIWebGLActiveInfo **retval)
{
    *retval = nsnull;

    WebGLuint progname;
    if (!GetGLName<WebGLProgram>("getActiveAttrib: program", pobj, &progname))
        return NS_OK;

    MakeContextCurrent();

    GLint len = 0;
    gl->fGetProgramiv(progname, LOCAL_GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &len);
    if (len == 0)
        return NS_OK;

    nsAutoArrayPtr<char> name(new char[len]);
    PRInt32 attrsize = 0;
    PRUint32 attrtype = 0;

    gl->fGetActiveAttrib(progname, index, len, &len, (GLint*) &attrsize, (WebGLuint*) &attrtype, name);
    if (attrsize == 0 || attrtype == 0) {
        *retval = nsnull;
        return NS_OK;
    }

    WebGLActiveInfo *retActiveInfo = new WebGLActiveInfo(attrsize, attrtype, name.get(), len);
    NS_ADDREF(*retval = retActiveInfo);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GenerateMipmap(WebGLenum target)
{
    if (!ValidateTextureTargetEnum(target, "generateMipmap"))
        return NS_OK;

    WebGLTexture *tex = activeBoundTextureForTarget(target);

    if (!tex)
        return ErrorInvalidOperation("generateMipmap: no texture is bound to this target");

    if (!tex->IsFirstImagePowerOfTwo()) {
        return ErrorInvalidOperation("generateMipmap: the width or height of this texture is not a power of two");
    }

    if (!tex->AreAllLevel0ImageInfosEqual()) {
        return ErrorInvalidOperation("generateMipmap: the six faces of this cube map have different dimensions, format, or type.");
    }

    tex->SetGeneratedMipmap();

    MakeContextCurrent();
    gl->fGenerateMipmap(target);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetActiveUniform(nsIWebGLProgram *pobj, PRUint32 index, nsIWebGLActiveInfo **retval)
{
    *retval = nsnull;

    WebGLuint progname;
    if (!GetGLName<WebGLProgram>("getActiveUniform: program", pobj, &progname))
        return NS_OK;

    MakeContextCurrent();

    GLint len = 0;
    gl->fGetProgramiv(progname, LOCAL_GL_ACTIVE_UNIFORM_MAX_LENGTH, &len);
    if (len == 0)
        *retval = nsnull;

    nsAutoArrayPtr<char> name(new char[len + 3]); // +3 because we might have to append "[0]", see below

    PRInt32 attrsize = 0;
    PRUint32 attrtype = 0;

    gl->fGetActiveUniform(progname, index, len, &len, (GLint*) &attrsize, (WebGLenum*) &attrtype, name);
    if (len == 0 || attrsize == 0 || attrtype == 0) {
        *retval = nsnull;
        return NS_OK;
    }

    // OpenGL ES 2.0 specifies that if foo is a uniform array, GetActiveUniform returns its name as "foo[0]".
    // See section 2.10 page 35 in the OpenGL ES 2.0.24 specification:
    //
    // > If the active uniform is an array, the uniform name returned in name will always
    // > be the name of the uniform array appended with "[0]".
    //
    // There is no such requirement in the OpenGL (non-ES) spec and indeed we have OpenGL implementations returning
    // "foo" instead of "foo[0]". So, when implementing WebGL on top of desktop OpenGL, we must check if the
    // returned name ends in [0], and if it doesn't, append that.
    //
    // In principle we don't need to do that on OpenGL ES, but this is such a tricky difference between the ES and non-ES
    // specs that it seems probable that some ES implementers will overlook it. Since the work-around is quite cheap,
    // we do it unconditionally.
    if (attrsize > 1 && name[len-1] != ']') {
        name[len++] = '[';
        name[len++] = '0';
        name[len++] = ']';
    }

    WebGLActiveInfo *retActiveInfo = new WebGLActiveInfo(attrsize, attrtype, name.get(), len);

    NS_ADDREF(*retval = retActiveInfo);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetAttachedShaders(nsIWebGLProgram *pobj, nsIVariant **retval)
{
    *retval = nsnull;

    WebGLProgram *prog;
    PRBool isNull;
    if (!GetConcreteObject("getAttachedShaders", pobj, &prog, &isNull)) 
        return NS_OK;

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    MakeContextCurrent();

    if (isNull) {
        wrval->SetAsEmpty();
        // note no return, we still want to return the variant
        ErrorInvalidValue("getAttachedShaders: invalid program");
    } else if (prog->AttachedShaders().Length() == 0) {
        wrval->SetAsEmptyArray();
    } else {
        wrval->SetAsArray(nsIDataType::VTYPE_INTERFACE,
                        &NS_GET_IID(nsIWebGLShader),
                        prog->AttachedShaders().Length(),
                        const_cast<void*>( // @#$% SetAsArray doesn't accept a const void*
                            static_cast<const void*>(
                                prog->AttachedShaders().Elements()
                            )
                        )
                        );
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetAttribLocation(nsIWebGLProgram *pobj,
                                const nsAString& name,
                                PRInt32 *retval)
{
    *retval = 0;

    WebGLuint progname;
    if (!GetGLName<WebGLProgram>("getAttribLocation: program", pobj, &progname))
        return NS_OK;

    MakeContextCurrent();
    *retval = gl->fGetAttribLocation(progname, NS_LossyConvertUTF16toASCII(name).get());
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetParameter(PRUint32 pname, nsIVariant **retval)
{
    *retval = nsnull;

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    MakeContextCurrent();

    switch (pname) {
        //
        // String params
        //

        case LOCAL_GL_VENDOR:
            wrval->SetAsDOMString(NS_LITERAL_STRING("Mozilla"));
            break;
        case LOCAL_GL_RENDERER:
            wrval->SetAsDOMString(NS_LITERAL_STRING("Mozilla"));
            break;
        case LOCAL_GL_VERSION:
            wrval->SetAsDOMString(NS_LITERAL_STRING("WebGL 1.0"));
            break;
        case LOCAL_GL_SHADING_LANGUAGE_VERSION:
            wrval->SetAsDOMString(NS_LITERAL_STRING("WebGL GLSL ES 1.0"));
            break;

        //
        // Single-value params
        //

// int
        case LOCAL_GL_CULL_FACE_MODE:
        case LOCAL_GL_FRONT_FACE:
        case LOCAL_GL_ACTIVE_TEXTURE:
        case LOCAL_GL_STENCIL_CLEAR_VALUE:
        case LOCAL_GL_STENCIL_FUNC:
        case LOCAL_GL_STENCIL_REF:
        case LOCAL_GL_STENCIL_FAIL:
        case LOCAL_GL_STENCIL_PASS_DEPTH_FAIL:
        case LOCAL_GL_STENCIL_PASS_DEPTH_PASS:
        case LOCAL_GL_STENCIL_BACK_FUNC:
        case LOCAL_GL_STENCIL_BACK_REF:
        case LOCAL_GL_STENCIL_BACK_FAIL:
        case LOCAL_GL_STENCIL_BACK_PASS_DEPTH_FAIL:
        case LOCAL_GL_STENCIL_BACK_PASS_DEPTH_PASS:
        case LOCAL_GL_DEPTH_FUNC:
        case LOCAL_GL_BLEND_SRC_RGB:
        case LOCAL_GL_BLEND_SRC_ALPHA:
        case LOCAL_GL_BLEND_DST_RGB:
        case LOCAL_GL_BLEND_DST_ALPHA:
        case LOCAL_GL_BLEND_EQUATION_RGB:
        case LOCAL_GL_BLEND_EQUATION_ALPHA:
        case LOCAL_GL_UNPACK_ALIGNMENT:
        case LOCAL_GL_PACK_ALIGNMENT:
        case LOCAL_GL_GENERATE_MIPMAP_HINT:
        case LOCAL_GL_SUBPIXEL_BITS:
        case LOCAL_GL_MAX_TEXTURE_SIZE:
        case LOCAL_GL_MAX_CUBE_MAP_TEXTURE_SIZE:
        case LOCAL_GL_SAMPLE_BUFFERS:
        case LOCAL_GL_SAMPLES:
        case LOCAL_GL_MAX_VERTEX_ATTRIBS:
        case LOCAL_GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        case LOCAL_GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
        case LOCAL_GL_MAX_TEXTURE_IMAGE_UNITS:
        case LOCAL_GL_MAX_RENDERBUFFER_SIZE:
        case LOCAL_GL_RED_BITS:
        case LOCAL_GL_GREEN_BITS:
        case LOCAL_GL_BLUE_BITS:
        case LOCAL_GL_ALPHA_BITS:
        case LOCAL_GL_DEPTH_BITS:
        case LOCAL_GL_STENCIL_BITS:
        {
            GLint i = 0;
            gl->fGetIntegerv(pname, &i);
            wrval->SetAsInt32(i);
        }
            break;

        case LOCAL_GL_MAX_VERTEX_UNIFORM_VECTORS:
            wrval->SetAsInt32(mGLMaxVertexUniformVectors);
            break;

        case LOCAL_GL_MAX_FRAGMENT_UNIFORM_VECTORS:
            wrval->SetAsInt32(mGLMaxFragmentUniformVectors);
            break;

        case LOCAL_GL_MAX_VARYING_VECTORS:
            wrval->SetAsInt32(mGLMaxVaryingVectors);
            break;

        case LOCAL_GL_NUM_COMPRESSED_TEXTURE_FORMATS:
            wrval->SetAsInt32(0);
            break;
        case LOCAL_GL_COMPRESSED_TEXTURE_FORMATS:
            wrval->SetAsEmpty(); // the spec says we must return null
            break;

// unsigned int. here we may have to return very large values like 2^32-1 that can't be represented as
// javascript integer values. We just return them as doubles and javascript doesn't care.
        case LOCAL_GL_STENCIL_BACK_VALUE_MASK:
        case LOCAL_GL_STENCIL_BACK_WRITEMASK:
        case LOCAL_GL_STENCIL_VALUE_MASK:
        case LOCAL_GL_STENCIL_WRITEMASK:
        {
            GLint i = 0; // the GL api (glGetIntegerv) only does signed ints
            gl->fGetIntegerv(pname, &i);
            GLuint i_unsigned(i); // this is where -1 becomes 2^32-1
            double i_double(i_unsigned); // pass as FP value to allow large values such as 2^32-1.
            wrval->SetAsDouble(i_double);
        }
            break;

// float
        case LOCAL_GL_DEPTH_CLEAR_VALUE:
        case LOCAL_GL_LINE_WIDTH:
        case LOCAL_GL_POLYGON_OFFSET_FACTOR:
        case LOCAL_GL_POLYGON_OFFSET_UNITS:
        case LOCAL_GL_SAMPLE_COVERAGE_VALUE:
        {
            GLfloat f = 0.f;
            gl->fGetFloatv(pname, &f);
            wrval->SetAsFloat(f);
        }
            break;

// bool
        case LOCAL_GL_BLEND:
        case LOCAL_GL_DEPTH_TEST:
        case LOCAL_GL_STENCIL_TEST:
        case LOCAL_GL_CULL_FACE:
        case LOCAL_GL_DITHER:
        case LOCAL_GL_POLYGON_OFFSET_FILL:
        case LOCAL_GL_SCISSOR_TEST:
        case LOCAL_GL_SAMPLE_COVERAGE_INVERT:
        case LOCAL_GL_DEPTH_WRITEMASK:
        {
            realGLboolean b = 0;
            gl->fGetBooleanv(pname, &b);
            wrval->SetAsBool(PRBool(b));
        }
            break;

// bool, WebGL-specific
        case UNPACK_FLIP_Y_WEBGL:
            wrval->SetAsBool(mPixelStoreFlipY);
            break;
        case UNPACK_PREMULTIPLY_ALPHA_WEBGL:
            wrval->SetAsBool(mPixelStorePremultiplyAlpha);
            break;

// uint, WebGL-specific
        case UNPACK_COLORSPACE_CONVERSION_WEBGL:
            wrval->SetAsUint32(mPixelStoreColorspaceConversion);
            break;

        //
        // Complex values
        //
        case LOCAL_GL_DEPTH_RANGE: // 2 floats
        case LOCAL_GL_ALIASED_POINT_SIZE_RANGE: // 2 floats
        case LOCAL_GL_ALIASED_LINE_WIDTH_RANGE: // 2 floats
        {
            GLfloat fv[2] = { 0 };
            gl->fGetFloatv(pname, fv);
            wrval->SetAsArray(nsIDataType::VTYPE_FLOAT, nsnull,
                              2, static_cast<void*>(fv));
        }
            break;
        
        case LOCAL_GL_COLOR_CLEAR_VALUE: // 4 floats
        case LOCAL_GL_BLEND_COLOR: // 4 floats
        {
            GLfloat fv[4] = { 0 };
            gl->fGetFloatv(pname, fv);
            wrval->SetAsArray(nsIDataType::VTYPE_FLOAT, nsnull,
                              4, static_cast<void*>(fv));
        }
            break;

        case LOCAL_GL_MAX_VIEWPORT_DIMS: // 2 ints
        {
            GLint iv[2] = { 0 };
            gl->fGetIntegerv(pname, iv);
            wrval->SetAsArray(nsIDataType::VTYPE_INT32, nsnull,
                              2, static_cast<void*>(iv));
        }
            break;

        case LOCAL_GL_SCISSOR_BOX: // 4 ints
        case LOCAL_GL_VIEWPORT: // 4 ints
        {
            GLint iv[4] = { 0 };
            gl->fGetIntegerv(pname, iv);
            wrval->SetAsArray(nsIDataType::VTYPE_INT32, nsnull,
                              4, static_cast<void*>(iv));
        }
            break;

        case LOCAL_GL_COLOR_WRITEMASK: // 4 bools
        {
            realGLboolean gl_bv[4] = { 0 };
            gl->fGetBooleanv(pname, gl_bv);
            PRBool pr_bv[4] = { gl_bv[0], gl_bv[1], gl_bv[2], gl_bv[3] };
            wrval->SetAsArray(nsIDataType::VTYPE_BOOL, nsnull,
                              4, static_cast<void*>(pr_bv));
        }
            break;

        case LOCAL_GL_ARRAY_BUFFER_BINDING:
            wrval->SetAsISupports(mBoundArrayBuffer);
            break;

        case LOCAL_GL_ELEMENT_ARRAY_BUFFER_BINDING:
            wrval->SetAsISupports(mBoundElementArrayBuffer);
            break;

        case LOCAL_GL_RENDERBUFFER_BINDING:
            wrval->SetAsISupports(mBoundRenderbuffer);
            break;

        case LOCAL_GL_FRAMEBUFFER_BINDING:
            wrval->SetAsISupports(mBoundFramebuffer);
            break;

        case LOCAL_GL_CURRENT_PROGRAM:
            wrval->SetAsISupports(mCurrentProgram);
            break;

        case LOCAL_GL_TEXTURE_BINDING_2D:
            wrval->SetAsISupports(mBound2DTextures[mActiveTexture]);
            break;

        case LOCAL_GL_TEXTURE_BINDING_CUBE_MAP:
            wrval->SetAsISupports(mBoundCubeMapTextures[mActiveTexture]);
            break;

        default:
            return ErrorInvalidEnumInfo("getParameter: parameter", pname);
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetBufferParameter(WebGLenum target, WebGLenum pname, nsIVariant **retval)
{
    *retval = nsnull;

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    if (target != LOCAL_GL_ARRAY_BUFFER && target != LOCAL_GL_ELEMENT_ARRAY_BUFFER)
        return ErrorInvalidEnumInfo("getBufferParameter: target", target);

    MakeContextCurrent();

    switch (pname) {
        case LOCAL_GL_BUFFER_SIZE:
        case LOCAL_GL_BUFFER_USAGE:
        case LOCAL_GL_BUFFER_ACCESS:
        case LOCAL_GL_BUFFER_MAPPED:
        {
            GLint i = 0;
            gl->fGetBufferParameteriv(target, pname, &i);
            wrval->SetAsInt32(i);
        }
            break;

        default:
            return ErrorInvalidEnumInfo("getBufferParameter: parameter", pname);
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetFramebufferAttachmentParameter(WebGLenum target, WebGLenum attachment, WebGLenum pname, nsIVariant **retval)
{
    *retval = nsnull;

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    if (target != LOCAL_GL_FRAMEBUFFER)
        return ErrorInvalidEnumInfo("getFramebufferAttachmentParameter: target", target);

    switch (attachment) {
        case LOCAL_GL_COLOR_ATTACHMENT0:
        case LOCAL_GL_DEPTH_ATTACHMENT:
        case LOCAL_GL_STENCIL_ATTACHMENT:
        case LOCAL_GL_DEPTH_STENCIL_ATTACHMENT:
            break;
        default:
            return ErrorInvalidEnumInfo("GetFramebufferAttachmentParameter: attachment", attachment);
    }

    if (!mBoundFramebuffer)
        return ErrorInvalidOperation("GetFramebufferAttachmentParameter: cannot query framebuffer 0");

    MakeContextCurrent();

    const WebGLFramebufferAttachment& fba = mBoundFramebuffer->GetAttachment(attachment);

    if (fba.Renderbuffer()) {
        switch (pname) {
            case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
                wrval->SetAsInt32(LOCAL_GL_RENDERBUFFER);
                break;

            case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
                wrval->SetAsISupports(fba.Renderbuffer());
                break;

            default:
                return ErrorInvalidEnumInfo("GetFramebufferAttachmentParameter: pname", pname);
        }
    } else if (fba.Texture()) {
        switch (pname) {
            case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
                wrval->SetAsInt32(LOCAL_GL_TEXTURE);
                break;

            case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
                wrval->SetAsISupports(fba.Texture());
                break;

            case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
                wrval->SetAsInt32(fba.TextureLevel());
                break;

            case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
                wrval->SetAsInt32(fba.TextureCubeMapFace());
                break;

            default:
                return ErrorInvalidEnumInfo("GetFramebufferAttachmentParameter: pname", pname);
        }
    } else {
        switch (pname) {
            case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
                wrval->SetAsInt32(LOCAL_GL_NONE);
                break;

            case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
                wrval->SetAsEmpty();
                break;

            default:
                return ErrorInvalidEnumInfo("GetFramebufferAttachmentParameter: pname", pname);
        }
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetRenderbufferParameter(WebGLenum target, WebGLenum pname, nsIVariant **retval)
{
    *retval = nsnull;

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    if (target != LOCAL_GL_RENDERBUFFER)
        return ErrorInvalidEnumInfo("GetRenderbufferParameter: target", target);

    MakeContextCurrent();

    switch (pname) {
        case LOCAL_GL_RENDERBUFFER_WIDTH:
        case LOCAL_GL_RENDERBUFFER_HEIGHT:
        case LOCAL_GL_RENDERBUFFER_RED_SIZE:
        case LOCAL_GL_RENDERBUFFER_GREEN_SIZE:
        case LOCAL_GL_RENDERBUFFER_BLUE_SIZE:
        case LOCAL_GL_RENDERBUFFER_ALPHA_SIZE:
        case LOCAL_GL_RENDERBUFFER_DEPTH_SIZE:
        case LOCAL_GL_RENDERBUFFER_STENCIL_SIZE:
        {
            GLint i = 0;
            gl->fGetRenderbufferParameteriv(target, pname, &i);
            wrval->SetAsInt32(i);
        }
            break;
        case LOCAL_GL_RENDERBUFFER_INTERNAL_FORMAT:
        {
            GLint i = 0;
            gl->fGetRenderbufferParameteriv(target, pname, &i);
            if (i == LOCAL_GL_DEPTH24_STENCIL8)
            {
                i = LOCAL_GL_DEPTH_STENCIL;
            }
            wrval->SetAsInt32(i);
        }
            break;
        default:
            return ErrorInvalidEnumInfo("GetRenderbufferParameter: parameter", pname);
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::CreateBuffer(nsIWebGLBuffer **retval)
{
    *retval = nsnull;

    MakeContextCurrent();

    WebGLuint name;
    gl->fGenBuffers(1, &name);

    WebGLBuffer *globj = new WebGLBuffer(this, name);
    NS_ADDREF(*retval = globj);
    mMapBuffers.Put(name, globj);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::CreateTexture(nsIWebGLTexture **retval)
{
    *retval = nsnull;

    MakeContextCurrent();

    WebGLuint name;
    gl->fGenTextures(1, &name);

    WebGLTexture *globj = new WebGLTexture(this, name);
    NS_ADDREF(*retval = globj);
    mMapTextures.Put(name, globj);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetError(WebGLenum *_retval)
{
    MakeContextCurrent();

    // Always call glGetError to clear any pending
    // real GL error.
    WebGLenum err = gl->fGetError();

    // mSynthesizedGLError has the first error that occurred,
    // whether synthesized or real; if it's not NO_ERROR, use it.
    if (mSynthesizedGLError != LOCAL_GL_NO_ERROR) {
        err = mSynthesizedGLError;
        mSynthesizedGLError = LOCAL_GL_NO_ERROR;
    }

    *_retval = err;

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetProgramParameter(nsIWebGLProgram *pobj, PRUint32 pname, nsIVariant **retval)
{
    *retval = nsnull;

    WebGLuint progname;
    PRBool isDeleted;
    if (!GetGLName<WebGLProgram>("getProgramParameter: program", pobj, &progname, nsnull, &isDeleted))
        return NS_OK;

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    MakeContextCurrent();

    switch (pname) {
        case LOCAL_GL_CURRENT_PROGRAM:
        case LOCAL_GL_ATTACHED_SHADERS:
        case LOCAL_GL_INFO_LOG_LENGTH:
        case LOCAL_GL_ACTIVE_UNIFORMS:
        case LOCAL_GL_ACTIVE_UNIFORM_MAX_LENGTH:
        case LOCAL_GL_ACTIVE_ATTRIBUTES:
        case LOCAL_GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
        {
            GLint i = 0;
            gl->fGetProgramiv(progname, pname, &i);
            wrval->SetAsInt32(i);
        }
            break;
        case LOCAL_GL_DELETE_STATUS:
        case LOCAL_GL_LINK_STATUS:
        case LOCAL_GL_VALIDATE_STATUS:
        {
            GLint i = 0;
#ifdef XP_MACOSX
            if (pname == LOCAL_GL_VALIDATE_STATUS &&
                gl->Vendor() == gl::GLContext::VendorNVIDIA)
            {
                // See comment in ValidateProgram below.
                i = 1;
            } else {
                gl->fGetProgramiv(progname, pname, &i);
            }
#else
            gl->fGetProgramiv(progname, pname, &i);
#endif
            wrval->SetAsBool(PRBool(i));
        }
            break;

        default:
            return ErrorInvalidEnumInfo("GetProgramParameter: parameter", pname);
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetProgramInfoLog(nsIWebGLProgram *pobj, nsAString& retval)
{
    WebGLuint progname;
    if (!GetGLName<WebGLProgram>("getProgramInfoLog: program", pobj, &progname))
        return NS_OK;

    MakeContextCurrent();

    PRInt32 k = -1;
    gl->fGetProgramiv(progname, LOCAL_GL_INFO_LOG_LENGTH, (GLint*) &k);
    if (k == -1)
        return NS_ERROR_FAILURE; // XXX GL error? shouldn't happen!

    if (k == 0) {
        retval.Truncate();
        return NS_OK;
    }

    nsCAutoString log;
    log.SetCapacity(k);

    gl->fGetProgramInfoLog(progname, k, (GLint*) &k, (char*) log.BeginWriting());

    log.SetLength(k);

    CopyASCIItoUTF16(log, retval);

    return NS_OK;
}

// here we have to support all pnames with both int and float params.
// See this discussion:
//  https://www.khronos.org/webgl/public-mailing-list/archives/1008/msg00014.html
nsresult WebGLContext::TexParameter_base(WebGLenum target, WebGLenum pname,
                                         WebGLint *intParamPtr, WebGLfloat *floatParamPtr)
{
    NS_ENSURE_TRUE(intParamPtr || floatParamPtr, NS_ERROR_FAILURE);

    WebGLint intParam = intParamPtr ? *intParamPtr : WebGLint(*floatParamPtr);
    WebGLfloat floatParam = floatParamPtr ? *floatParamPtr : WebGLfloat(*intParamPtr);

    if (!ValidateTextureTargetEnum(target, "texParameter: target"))
        return NS_OK;

    WebGLTexture *tex = activeBoundTextureForTarget(target);
    if (!tex)
        return ErrorInvalidOperation("texParameter: no texture is bound to this target");

    PRBool pnameAndParamAreIncompatible = PR_FALSE;

    switch (pname) {
        case LOCAL_GL_TEXTURE_MIN_FILTER:
            switch (intParam) {
                case LOCAL_GL_NEAREST:
                case LOCAL_GL_LINEAR:
                case LOCAL_GL_NEAREST_MIPMAP_NEAREST:
                case LOCAL_GL_LINEAR_MIPMAP_NEAREST:
                case LOCAL_GL_NEAREST_MIPMAP_LINEAR:
                case LOCAL_GL_LINEAR_MIPMAP_LINEAR:
                    tex->SetMinFilter(intParam);
                    break;
                default:
                    pnameAndParamAreIncompatible = PR_TRUE;
            }
            break;
        case LOCAL_GL_TEXTURE_MAG_FILTER:
            switch (intParam) {
                case LOCAL_GL_NEAREST:
                case LOCAL_GL_LINEAR:
                    tex->SetMagFilter(intParam);
                    break;
                default:
                    pnameAndParamAreIncompatible = PR_TRUE;
            }
            break;
        case LOCAL_GL_TEXTURE_WRAP_S:
            switch (intParam) {
                case LOCAL_GL_CLAMP_TO_EDGE:
                case LOCAL_GL_MIRRORED_REPEAT:
                case LOCAL_GL_REPEAT:
                    tex->SetWrapS(intParam);
                    break;
                default:
                    pnameAndParamAreIncompatible = PR_TRUE;
            }
            break;
        case LOCAL_GL_TEXTURE_WRAP_T:
            switch (intParam) {
                case LOCAL_GL_CLAMP_TO_EDGE:
                case LOCAL_GL_MIRRORED_REPEAT:
                case LOCAL_GL_REPEAT:
                    tex->SetWrapT(intParam);
                    break;
                default:
                    pnameAndParamAreIncompatible = PR_TRUE;
            }
            break;
        default:
            return ErrorInvalidEnumInfo("texParameter: pname", pname);
    }

    if (pnameAndParamAreIncompatible) {
        // note that currently all params are enums, and the tex-input-validation test wants INVALID_ENUM errors
        // even for texParameterf. why not.
        if (intParamPtr)
            return ErrorInvalidEnum("texParameteri: pname %x and param %x (decimal %d) are mutually incompatible",
                                    pname, intParam, intParam);
        else
            return ErrorInvalidEnum("texParameterf: pname %x and floating-point param %e are mutually incompatible",
                                    pname, floatParam);
    }

    MakeContextCurrent();
    if (intParamPtr)
        gl->fTexParameteri(target, pname, intParam);
    else
        gl->fTexParameterf(target, pname, floatParam);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::TexParameterf(WebGLenum target, WebGLenum pname, WebGLfloat param)
{
    return TexParameter_base(target, pname, nsnull, &param);
}

NS_IMETHODIMP
WebGLContext::TexParameteri(WebGLenum target, WebGLenum pname, WebGLint param)
{
    return TexParameter_base(target, pname, &param, nsnull);
}

NS_IMETHODIMP
WebGLContext::GetTexParameter(WebGLenum target, WebGLenum pname, nsIVariant **retval)
{
    *retval = nsnull;

    MakeContextCurrent();

    if (!ValidateTextureTargetEnum(target, "getTexParameter: target"))
        return NS_OK;

    if (!activeBoundTextureForTarget(target))
        return ErrorInvalidOperation("getTexParameter: no texture bound");

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    switch (pname) {
        case LOCAL_GL_TEXTURE_MIN_FILTER:
        case LOCAL_GL_TEXTURE_MAG_FILTER:
        case LOCAL_GL_TEXTURE_WRAP_S:
        case LOCAL_GL_TEXTURE_WRAP_T:
        {
            GLint i = 0;
            gl->fGetTexParameteriv(target, pname, &i);
            wrval->SetAsInt32(i);
        }
            break;

        default:
            return ErrorInvalidEnumInfo("getTexParameter: parameter", pname);
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

/* any getUniform(in WebGLProgram program, in WebGLUniformLocation location) raises(DOMException); */
NS_IMETHODIMP
WebGLContext::GetUniform(nsIWebGLProgram *pobj, nsIWebGLUniformLocation *ploc, nsIVariant **retval)
{
    *retval = nsnull;

    WebGLuint progname;
    WebGLProgram *prog;
    if (!GetConcreteObjectAndGLName("getUniform: program", pobj, &prog, &progname))
        return NS_OK;

    WebGLUniformLocation *location;
    if (!GetConcreteObject("getUniform: location", ploc, &location))
        return NS_OK;

    if (location->Program() != prog)
        return ErrorInvalidValue("GetUniform: this uniform location corresponds to another program");

    if (location->ProgramGeneration() != prog->Generation())
        return ErrorInvalidOperation("GetUniform: this uniform location is obsolete since the program has been relinked");

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    MakeContextCurrent();

    GLint uniforms = 0;
    GLint uniformNameMaxLength = 0;
    gl->fGetProgramiv(progname, LOCAL_GL_ACTIVE_UNIFORMS, &uniforms);
    gl->fGetProgramiv(progname, LOCAL_GL_ACTIVE_UNIFORM_MAX_LENGTH, &uniformNameMaxLength);

    // we now need the type info to switch between fGetUniformfv and fGetUniformiv
    // the only way to get that is to iterate through all active uniforms by index until
    // one matches the given uniform location.
    GLenum uniformType = 0;
    nsAutoArrayPtr<GLchar> uniformName(new GLchar[uniformNameMaxLength]);
    // this buffer has 16 more bytes to be able to store [index] at the end.
    nsAutoArrayPtr<GLchar> uniformNameBracketIndex(new GLchar[uniformNameMaxLength + 16]);

    GLint index;
    for (index = 0; index < uniforms; ++index) {
        GLsizei length;
        GLint size;
        gl->fGetActiveUniform(progname, index, uniformNameMaxLength, &length,
                              &size, &uniformType, uniformName);
        if (gl->fGetUniformLocation(progname, uniformName) == location->Location())
            break;

        // now we handle the case of array uniforms. In that case, fGetActiveUniform returned as 'size'
        // the biggest index used plus one, so we need to loop over that. The 0 index has already been handled above,
        // so we can start at one. For each index, we construct the string uniformName + "[" + index + "]".
        if (size > 1) {
            bool found_it = false;
            if (uniformName[length - 1] == ']') { // if uniformName ends in [0]
                // remove the [0] at the end
                length -= 3;
                uniformName[length] = 0;
            }
            for (GLint arrayIndex = 1; arrayIndex < size; arrayIndex++) {
                sprintf(uniformNameBracketIndex.get(), "%s[%d]", uniformName.get(), arrayIndex);
                if (gl->fGetUniformLocation(progname, uniformNameBracketIndex) == location->Location()) {
                    found_it = true;
                    break;
                }
            }
            if (found_it) break;
        }
    }

    if (index == uniforms)
        return NS_ERROR_FAILURE; // XXX GL error? shouldn't happen.

    GLenum baseType;
    GLint unitSize;
    if (!BaseTypeAndSizeFromUniformType(uniformType, &baseType, &unitSize))
        return NS_ERROR_FAILURE;

    // this should never happen
    if (unitSize > 16)
        return NS_ERROR_FAILURE;

    if (baseType == LOCAL_GL_FLOAT) {
        GLfloat fv[16] = { GLfloat(0) };
        gl->fGetUniformfv(progname, location->Location(), fv);
        if (unitSize == 1) {
            wrval->SetAsFloat(fv[0]);
        } else {
            wrval->SetAsArray(nsIDataType::VTYPE_FLOAT, nsnull,
                              unitSize, static_cast<void*>(fv));
        }
    } else if (baseType == LOCAL_GL_INT) {
        GLint iv[16] = { 0 };
        gl->fGetUniformiv(progname, location->Location(), iv);
        if (unitSize == 1) {
            wrval->SetAsInt32(iv[0]);
        } else {
            wrval->SetAsArray(nsIDataType::VTYPE_INT32, nsnull,
                              unitSize, static_cast<void*>(iv));
        }
    } else if (baseType == LOCAL_GL_BOOL) {
        GLint iv[16] = { 0 };
        gl->fGetUniformiv(progname, location->Location(), iv);
        if (unitSize == 1) {
            wrval->SetAsBool(iv[0] ? PR_TRUE : PR_FALSE);
        } else {
            PRBool uv[16] = { 0 };
            for (int k = 0; k < unitSize; k++)
                uv[k] = iv[k] ? PR_TRUE : PR_FALSE;
            wrval->SetAsArray(nsIDataType::VTYPE_BOOL, nsnull,
                              unitSize, static_cast<void*>(uv));
        }
    } else {
        wrval->SetAsVoid();
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetUniformLocation(nsIWebGLProgram *pobj, const nsAString& name, nsIWebGLUniformLocation **retval)
{
    *retval = nsnull;

    WebGLuint progname;
    WebGLProgram *prog;
    if (!GetConcreteObjectAndGLName("getUniformLocation: program", pobj, &prog, &progname))
        return NS_OK;

    MakeContextCurrent();

    GLint intlocation = gl->fGetUniformLocation(progname, NS_LossyConvertUTF16toASCII(name).get());

    nsRefPtr<nsIWebGLUniformLocation> loc = prog->GetUniformLocationObject(intlocation);
    *retval = loc.forget().get();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetVertexAttrib(WebGLuint index, WebGLenum pname, nsIVariant **retval)
{
    *retval = nsnull;

    if (!ValidateAttribIndex(index, "getVertexAttrib"))
        return NS_OK;

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    MakeContextCurrent();

    switch (pname) {
        case LOCAL_GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
            wrval->SetAsISupports(mAttribBuffers[index].buf);
            break;

        case LOCAL_GL_VERTEX_ATTRIB_ARRAY_SIZE:
        case LOCAL_GL_VERTEX_ATTRIB_ARRAY_STRIDE:
        case LOCAL_GL_VERTEX_ATTRIB_ARRAY_TYPE:
        {
            PRInt32 i = 0;
            gl->fGetVertexAttribiv(index, pname, (GLint*) &i);
            wrval->SetAsInt32(i);
        }
            break;

        case LOCAL_GL_CURRENT_VERTEX_ATTRIB:
        {
            WebGLfloat vec[4] = {0, 0, 0, 1};
            if (index) {
                gl->fGetVertexAttribfv(index, LOCAL_GL_CURRENT_VERTEX_ATTRIB, &vec[0]);
            } else {
                vec[0] = mVertexAttrib0Vector[0];
                vec[1] = mVertexAttrib0Vector[1];
                vec[2] = mVertexAttrib0Vector[2];
                vec[3] = mVertexAttrib0Vector[3];
            }
            wrval->SetAsArray(nsIDataType::VTYPE_FLOAT, nsnull,
                              4, vec);
        }
            break;

        case LOCAL_GL_VERTEX_ATTRIB_ARRAY_ENABLED:
        case LOCAL_GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
        {
            PRInt32 i = 0;
            gl->fGetVertexAttribiv(index, pname, (GLint*) &i);
            wrval->SetAsBool(PRBool(i));
        }
            break;

        case LOCAL_GL_VERTEX_ATTRIB_ARRAY_POINTER:
            wrval->SetAsUint32(mAttribBuffers[index].byteOffset);
            break;

        default:
            return ErrorInvalidEnumInfo("getVertexAttrib: parameter", pname);
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

/* GLuint getVertexAttribOffset (in GLuint index, in GLenum pname); */
NS_IMETHODIMP
WebGLContext::GetVertexAttribOffset(WebGLuint index, WebGLenum pname, WebGLuint *retval)
{
    *retval = 0;

    if (!ValidateAttribIndex(index, "getVertexAttribOffset"))
        return NS_OK;

    if (pname != LOCAL_GL_VERTEX_ATTRIB_ARRAY_POINTER)
        return ErrorInvalidEnum("getVertexAttribOffset: bad parameter");

    *retval = mAttribBuffers[index].byteOffset;
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::Hint(WebGLenum target, WebGLenum mode)
{
    return ErrorInvalidEnum("hint: invalid hint");
}

NS_IMETHODIMP
WebGLContext::IsBuffer(nsIWebGLBuffer *bobj, WebGLboolean *retval)
{
    PRBool isDeleted;
    WebGLuint buffername;
    WebGLBuffer *buffer;
    PRBool ok = GetConcreteObjectAndGLName("isBuffer", bobj, &buffer, &buffername, nsnull, &isDeleted) && 
                !isDeleted &&
                buffer->HasEverBeenBound();
    if (ok) {
        MakeContextCurrent();
        ok = gl->fIsBuffer(buffername);
    }

    *retval = ok;
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::IsFramebuffer(nsIWebGLFramebuffer *fbobj, WebGLboolean *retval)
{
    PRBool isDeleted;
    WebGLuint fbname;
    WebGLFramebuffer *fb;
    PRBool ok = GetConcreteObjectAndGLName("isFramebuffer", fbobj, &fb, &fbname, nsnull, &isDeleted) &&
                !isDeleted &&
                fb->HasEverBeenBound();
    if (ok) {
        MakeContextCurrent();
        ok = gl->fIsFramebuffer(fbname);
    }

    *retval = ok;
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::IsProgram(nsIWebGLProgram *pobj, WebGLboolean *retval)
{
    PRBool isDeleted;
    WebGLProgram *prog = nsnull;
    PRBool ok = GetConcreteObject("isProgram", pobj, &prog, nsnull, &isDeleted, PR_FALSE) &&
                !isDeleted;

    *retval = ok;
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::IsRenderbuffer(nsIWebGLRenderbuffer *rbobj, WebGLboolean *retval)
{
    PRBool isDeleted;
    WebGLuint rbname;
    WebGLRenderbuffer *rb;
    PRBool ok = GetConcreteObjectAndGLName("isRenderBuffer", rbobj, &rb, &rbname, nsnull, &isDeleted) &&
                !isDeleted &&
                rb->HasEverBeenBound();
    if (ok) {
        MakeContextCurrent();
        ok = gl->fIsRenderbuffer(rbname);
    }

    *retval = ok;
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::IsShader(nsIWebGLShader *sobj, WebGLboolean *retval)
{
    PRBool isDeleted;
    WebGLShader *shader = nsnull;
    PRBool ok = GetConcreteObject("isShader", sobj, &shader, nsnull, &isDeleted, PR_FALSE) &&
                !isDeleted;

    *retval = ok;
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::IsTexture(nsIWebGLTexture *tobj, WebGLboolean *retval)
{
    PRBool isDeleted;
    WebGLuint texname;
    WebGLTexture *tex;
    PRBool ok = GetConcreteObjectAndGLName("isTexture", tobj, &tex, &texname, nsnull, &isDeleted) &&
                !isDeleted &&
                tex->HasEverBeenBound();
    if (ok) {
        MakeContextCurrent();
        ok = gl->fIsTexture(texname);
    }

    *retval = ok;
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::IsEnabled(WebGLenum cap, WebGLboolean *retval)
{
    *retval = 0;

    if (!ValidateCapabilityEnum(cap, "isEnabled"))
        return NS_OK;

    MakeContextCurrent();
    *retval = gl->fIsEnabled(cap);
    return NS_OK;
}

GL_SAME_METHOD_1(LineWidth, LineWidth, WebGLfloat)

NS_IMETHODIMP
WebGLContext::LinkProgram(nsIWebGLProgram *pobj)
{
    GLuint progname;
    WebGLProgram *program;
    if (!GetConcreteObjectAndGLName("linkProgram", pobj, &program, &progname))
        return NS_OK;

    if (!program->NextGeneration())
        return NS_ERROR_FAILURE;

    if (!program->HasBothShaderTypesAttached()) {
        program->SetLinkStatus(PR_FALSE);
        return NS_OK;
    }

    MakeContextCurrent();

    gl->fLinkProgram(progname);

    GLint ok;
    gl->fGetProgramiv(progname, LOCAL_GL_LINK_STATUS, &ok);
    if (ok) {
        program->SetLinkStatus(PR_TRUE);
        program->UpdateInfo(gl);
    } else {
        program->SetLinkStatus(PR_FALSE);
    }

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::PixelStorei(WebGLenum pname, WebGLint param)
{
    switch (pname) {
        case UNPACK_FLIP_Y_WEBGL:
            mPixelStoreFlipY = (param != 0);
            break;
        case UNPACK_PREMULTIPLY_ALPHA_WEBGL:
            mPixelStorePremultiplyAlpha = (param != 0);
            break;
        case UNPACK_COLORSPACE_CONVERSION_WEBGL:
            if (param == LOCAL_GL_NONE || param == BROWSER_DEFAULT_WEBGL)
                mPixelStoreColorspaceConversion = param;
            else
                return ErrorInvalidEnumInfo("pixelStorei: colorspace conversion parameter", param);
            break;
        case LOCAL_GL_PACK_ALIGNMENT:
        case LOCAL_GL_UNPACK_ALIGNMENT:
            if (param != 1 &&
                param != 2 &&
                param != 4 &&
                param != 8)
                return ErrorInvalidValue("PixelStorei: invalid pack/unpack alignment value");
            if (pname == LOCAL_GL_PACK_ALIGNMENT)
                mPixelStorePackAlignment = param;
            else if (pname == LOCAL_GL_UNPACK_ALIGNMENT)
                mPixelStoreUnpackAlignment = param;
            MakeContextCurrent();
            gl->fPixelStorei(pname, param);
            break;
        default:
            return ErrorInvalidEnumInfo("PixelStorei: parameter", pname);
    }

    return NS_OK;
}


GL_SAME_METHOD_2(PolygonOffset, PolygonOffset, WebGLfloat, WebGLfloat)

NS_IMETHODIMP
WebGLContext::ReadPixels(PRInt32 dummy)
{
    return NS_ERROR_FAILURE;
}

nsresult
WebGLContext::ReadPixels_base(WebGLint x, WebGLint y, WebGLsizei width, WebGLsizei height,
                              WebGLenum format, WebGLenum type, void *data, PRUint32 byteLength)
{
    if (HTMLCanvasElement()->IsWriteOnly() && !nsContentUtils::IsCallerTrustedForRead()) {
        LogMessageIfVerbose("ReadPixels: Not allowed");
        return NS_ERROR_DOM_SECURITY_ERR;
    }

    if (width < 0 || height < 0)
        return ErrorInvalidValue("ReadPixels: negative size passed");

    // there's nothing to do in this case, since we won't read any pixels
    if (width == 0 || height == 0)
        return NS_OK;

    WebGLsizei boundWidth = mBoundFramebuffer ? mBoundFramebuffer->width() : mWidth;
    WebGLsizei boundHeight = mBoundFramebuffer ? mBoundFramebuffer->height() : mHeight;

    PRUint32 size = 0;
    bool badFormat = false, badType = false;
    switch (format) {
    case LOCAL_GL_RGBA:
        size = 4;
        break;
    default:
        badFormat = true;
        break;
    }

    switch (type) {
    case LOCAL_GL_UNSIGNED_BYTE:
        break;
    default:
        badType = true;
        break;
    }

    if (badFormat && badType)
        return ErrorInvalidOperation("readPixels: bad format and type");
    if (badFormat)
        return ErrorInvalidEnumInfo("readPixels: format", format);
    if (badType)
        return ErrorInvalidEnumInfo("ReadPixels: type", type);

    CheckedUint32 checked_plainRowSize = CheckedUint32(width) * size;

    PRUint32 packAlignment = mPixelStorePackAlignment;

    // alignedRowSize = row size rounded up to next multiple of packAlignment
    CheckedUint32 checked_alignedRowSize
        = ((checked_plainRowSize + packAlignment-1) / packAlignment) * packAlignment;

    CheckedUint32 checked_neededByteLength
        = (height-1) * checked_alignedRowSize + checked_plainRowSize;

    if (!checked_neededByteLength.valid())
        return ErrorInvalidOperation("ReadPixels: integer overflow computing the needed buffer size");

    if (checked_neededByteLength.value() > byteLength)
        return ErrorInvalidOperation("ReadPixels: buffer too small");

    MakeContextCurrent();

    // prevent readback of arbitrary video memory through uninitialized renderbuffers!
    if (mBoundFramebuffer && !mBoundFramebuffer->CheckAndInitializeRenderbuffers())
        return NS_OK;

    if (CanvasUtils::CheckSaneSubrectSize(x, y, width, height, boundWidth, boundHeight)) {
        // the easy case: we're not reading out-of-range pixels
        gl->fReadPixels(x, y, width, height, format, type, data);
    } else {
        // the rectangle doesn't fit entirely in the bound buffer. We then have to set to zero the part
        // of the buffer that correspond to out-of-range pixels. We don't want to rely on system OpenGL
        // to do that for us, because passing out of range parameters to a buggy OpenGL implementation
        // could conceivably allow to read memory we shouldn't be allowed to read. So we manually initialize
        // the buffer to zero and compute the parameters to pass to OpenGL. We have to use an intermediate buffer
        // to accomodate the potentially different strides (widths).

        // zero the whole destination buffer. Too bad for the part that's going to be overwritten, we're not
        // 100% efficient here, but in practice this is a quite rare case anyway.
        memset(data, 0, byteLength);

        if (   x >= boundWidth
            || x+width <= 0
            || y >= boundHeight
            || y+height <= 0)
        {
            // we are completely outside of range, can exit now with buffer filled with zeros
            return NS_OK;
        }

        // compute the parameters of the subrect we're actually going to call glReadPixels on
        GLint   subrect_x      = PR_MAX(x, 0);
        GLint   subrect_end_x  = PR_MIN(x+width, boundWidth);
        GLsizei subrect_width  = subrect_end_x - subrect_x;

        GLint   subrect_y      = PR_MAX(y, 0);
        GLint   subrect_end_y  = PR_MIN(y+height, boundHeight);
        GLsizei subrect_height = subrect_end_y - subrect_y;

        if (subrect_width < 0 || subrect_height < 0 ||
            subrect_width > width || subrect_height > height)
            return ErrorInvalidOperation("ReadPixels: integer overflow computing clipped rect size");

        // now we know that subrect_width is in the [0..width] interval, and same for heights.

        // now, same computation as above to find the size of the intermediate buffer to allocate for the subrect
        // no need to check again for integer overflow here, since we already know the sizes aren't greater than before
        PRUint32 subrect_plainRowSize = subrect_width * size;
        PRUint32 subrect_alignedRowSize = (subrect_plainRowSize + packAlignment-1) &
            ~PRUint32(packAlignment-1);
        PRUint32 subrect_byteLength = (subrect_height-1)*subrect_alignedRowSize + subrect_plainRowSize;

        // create subrect buffer, call glReadPixels, copy pixels into destination buffer, delete subrect buffer
        GLubyte *subrect_data = new GLubyte[subrect_byteLength];
        gl->fReadPixels(subrect_x, subrect_y, subrect_width, subrect_height, format, type, subrect_data);

        // notice that this for loop terminates because we already checked that subrect_height is at most height
        for (GLint y_inside_subrect = 0; y_inside_subrect < subrect_height; ++y_inside_subrect) {
            GLint subrect_x_in_dest_buffer = subrect_x - x;
            GLint subrect_y_in_dest_buffer = subrect_y - y;
            memcpy(static_cast<GLubyte*>(data)
                     + checked_alignedRowSize.value() * (subrect_y_in_dest_buffer + y_inside_subrect)
                     + size * subrect_x_in_dest_buffer, // destination
                   subrect_data + subrect_alignedRowSize * y_inside_subrect, // source
                   subrect_plainRowSize); // size
        }
        delete [] subrect_data;
    }

    // if we're reading alpha, we may need to do fixup.  Note that we don't allow
    // GL_ALPHA to readpixels currently, but we had the code written for it already.
    if (format == LOCAL_GL_ALPHA ||
        format == LOCAL_GL_RGBA)
    {
        PRBool needAlphaFixup;
        if (mBoundFramebuffer) {
            needAlphaFixup = !mBoundFramebuffer->ColorAttachment().HasAlpha();
        } else {
            needAlphaFixup = gl->ActualFormat().alpha == 0;
        }

        if (needAlphaFixup) {
            if (format == LOCAL_GL_ALPHA && type == LOCAL_GL_UNSIGNED_BYTE) {
                // this is easy; it's an 0xff memset per row
                PRUint8 *row = (PRUint8*)data;
                for (GLint j = 0; j < height; ++j) {
                    memset(row, 0xff, checked_plainRowSize.value());
                    row += checked_alignedRowSize.value();
                }
            } else if (format == LOCAL_GL_RGBA && type == LOCAL_GL_UNSIGNED_BYTE) {
                // this is harder, we need to just set the alpha byte here
                PRUint8 *row = (PRUint8*)data;
                for (GLint j = 0; j < height; ++j) {
                    PRUint8 *rowp = row;
#ifdef IS_LITTLE_ENDIAN
                    // offset to get the alpha byte; we're always going to
                    // move by 4 bytes
                    rowp += 3;
#endif
                    PRUint8 *endrowp = rowp + 4 * width;
                    while (rowp != endrowp) {
                        *rowp = 0xff;
                        rowp += 4;
                    }

                    row += checked_alignedRowSize.value();
                }
            } else {
                NS_WARNING("Unhandled case, how'd we get here?");
                return NS_ERROR_FAILURE;
            }
        }            
    }

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::ReadPixels_array(WebGLint x, WebGLint y, WebGLsizei width, WebGLsizei height,
                               WebGLenum format, WebGLenum type, js::TypedArray *pixels)
{
    return ReadPixels_base(x, y, width, height, format, type,
                           pixels ? pixels->data : 0,
                           pixels ? pixels->byteLength : 0);
}

NS_IMETHODIMP
WebGLContext::ReadPixels_buf(WebGLint x, WebGLint y, WebGLsizei width, WebGLsizei height,
                             WebGLenum format, WebGLenum type, js::ArrayBuffer *pixels)
{
    return ReadPixels_base(x, y, width, height, format, type,
                           pixels ? pixels->data : 0,
                           pixels ? pixels->byteLength : 0);
}

NS_IMETHODIMP
WebGLContext::RenderbufferStorage(WebGLenum target, WebGLenum internalformat, WebGLsizei width, WebGLsizei height)
{

    if (!mBoundRenderbuffer || !mBoundRenderbuffer->GLName())
        return ErrorInvalidOperation("renderbufferStorage called on renderbuffer 0");

    if (target != LOCAL_GL_RENDERBUFFER)
        return ErrorInvalidEnumInfo("renderbufferStorage: target", target);

    if (width <= 0 || height <= 0)
        return ErrorInvalidValue("renderbufferStorage: width and height must be > 0");

    if (!mBoundRenderbuffer || !mBoundRenderbuffer->GLName())
        return ErrorInvalidOperation("renderbufferStorage called on renderbuffer 0");

    MakeContextCurrent();

    // certain OpenGL ES renderbuffer formats may not exist on desktop OpenGL
    WebGLenum internalformatForGL = internalformat;

    switch (internalformat) {
    case LOCAL_GL_RGBA4:
    case LOCAL_GL_RGB5_A1:
        // 16-bit RGBA formats are not supported on desktop GL
        if (!gl->IsGLES2()) internalformatForGL = LOCAL_GL_RGBA8;
        break;
    case LOCAL_GL_RGB565:
        // the RGB565 format is not supported on desktop GL
        if (!gl->IsGLES2()) internalformatForGL = LOCAL_GL_RGB8;
        break;
    case LOCAL_GL_DEPTH_COMPONENT16:
        if (!gl->IsGLES2() || gl->IsExtensionSupported(gl::GLContext::OES_depth24))
            internalformatForGL = LOCAL_GL_DEPTH_COMPONENT24;
        else if (gl->IsExtensionSupported(gl::GLContext::OES_packed_depth_stencil))
            internalformatForGL = LOCAL_GL_DEPTH24_STENCIL8;
        break;
    case LOCAL_GL_STENCIL_INDEX8:
        break;
    case LOCAL_GL_DEPTH_STENCIL:
        // this one is available in newer OpenGL (at least since 3.1); will probably become available
        // in OpenGL ES 3 (at least it will have some DEPTH_STENCIL) and is the same value that
        // is otherwise provided by EXT_packed_depth_stencil and OES_packed_depth_stencil extensions
        // which means it's supported on most GL and GL ES systems already.
        //
        // So we just use it hoping that it's available (perhaps as an extension) and if it's not available,
        // we just let the GL generate an error and don't do anything about it ourselves.
        internalformatForGL = LOCAL_GL_DEPTH24_STENCIL8;
        break;
    default:
        return ErrorInvalidEnumInfo("renderbufferStorage: internalformat", internalformat);
    }

    gl->fRenderbufferStorage(target, internalformatForGL, width, height);

    mBoundRenderbuffer->SetInternalFormat(internalformat);
    mBoundRenderbuffer->setDimensions(width, height);
    mBoundRenderbuffer->SetInitialized(PR_FALSE);

    return NS_OK;
}

GL_SAME_METHOD_2(SampleCoverage, SampleCoverage, WebGLfloat, WebGLboolean)

NS_IMETHODIMP
WebGLContext::Scissor(WebGLint x, WebGLint y, WebGLsizei width, WebGLsizei height)
{
    if (width < 0 || height < 0)
        return ErrorInvalidValue("Scissor: negative size");

    MakeContextCurrent();
    gl->fScissor(x, y, width, height);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::StencilFunc(WebGLenum func, WebGLint ref, WebGLuint mask)
{
    if (!ValidateComparisonEnum(func, "stencilFunc: func"))
        return NS_OK;

    mStencilRef = ref;
    mStencilValueMask = mask;

    MakeContextCurrent();
    gl->fStencilFunc(func, ref, mask);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::StencilFuncSeparate(WebGLenum face, WebGLenum func, WebGLint ref, WebGLuint mask)
{
    if (!ValidateFaceEnum(face, "stencilFuncSeparate: face") ||
        !ValidateComparisonEnum(func, "stencilFuncSeparate: func"))
        return NS_OK;

    if (face != LOCAL_GL_FRONT_AND_BACK && (ref != mStencilRef || mask != mStencilValueMask))
        return ErrorInvalidOperation("stencilFuncSeparate: WebGL doesn't currently allow specifying "
                                     "different values for front and back.");

    mStencilRef = ref;
    mStencilValueMask = mask;

    MakeContextCurrent();
    gl->fStencilFuncSeparate(face, func, ref, mask);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::StencilMask(WebGLuint mask)
{
    mStencilWriteMask = mask;

    MakeContextCurrent();
    gl->fStencilMask(mask);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::StencilMaskSeparate(WebGLenum face, WebGLuint mask)
{
    if (!ValidateFaceEnum(face, "stencilMaskSeparate: face"))
        return NS_OK;

    if (face != LOCAL_GL_FRONT_AND_BACK && mask != mStencilWriteMask)
        return ErrorInvalidOperation("stencilMaskSeparate: WebGL doesn't currently allow specifying "
                                     "different values for front and back.");
    mStencilWriteMask = mask;

    MakeContextCurrent();
    gl->fStencilMaskSeparate(face, mask);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::StencilOp(WebGLenum sfail, WebGLenum dpfail, WebGLenum dppass)
{
    if (!ValidateStencilOpEnum(sfail, "stencilOp: sfail") ||
        !ValidateStencilOpEnum(dpfail, "stencilOp: dpfail") ||
        !ValidateStencilOpEnum(dppass, "stencilOp: dppass"))
        return NS_OK;

    MakeContextCurrent();
    gl->fStencilOp(sfail, dpfail, dppass);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::StencilOpSeparate(WebGLenum face, WebGLenum sfail, WebGLenum dpfail, WebGLenum dppass)
{
    if (!ValidateFaceEnum(face, "stencilOpSeparate: face") ||
        !ValidateStencilOpEnum(sfail, "stencilOpSeparate: sfail") ||
        !ValidateStencilOpEnum(dpfail, "stencilOpSeparate: dpfail") ||
        !ValidateStencilOpEnum(dppass, "stencilOpSeparate: dppass"))
        return NS_OK;

    MakeContextCurrent();
    gl->fStencilOpSeparate(face, sfail, dpfail, dppass);
    return NS_OK;
}

struct WebGLImageConverter
{
    PRBool flip;
    size_t width, height, srcStride, dstStride, srcTexelSize, dstTexelSize;
    const PRUint8 *src;
    PRUint8 *dst;

    WebGLImageConverter()
    {
        memset(this, 0, sizeof(WebGLImageConverter));
    }

    template<typename SrcType, typename DstType,
         void unpackingFunc(const SrcType*, PRUint8*),
         void packingFunc(const PRUint8*, DstType*)>
    void run()
    {
        for (size_t src_row = 0; src_row < height; ++src_row) {
            size_t dst_row = flip ? (height - 1 - src_row) : src_row;
            PRUint8 *dst_row_ptr = dst + dst_row * dstStride;
            const PRUint8 *src_row_ptr = src + src_row * srcStride;
            const PRUint8 *src_row_end = src_row_ptr + width * srcTexelSize; // != src_row_ptr + byteStride
            while (src_row_ptr != src_row_end) {
                PRUint8 tmp[4];
                unpackingFunc(reinterpret_cast<const SrcType*>(src_row_ptr), tmp);
                packingFunc(tmp, reinterpret_cast<DstType*>(dst_row_ptr));
                src_row_ptr += srcTexelSize;
                dst_row_ptr += dstTexelSize;
            }
        }
    }
};

void
WebGLContext::ConvertImage(size_t width, size_t height, size_t srcStride, size_t dstStride,
                           const PRUint8*src, PRUint8 *dst,
                           int srcFormat, PRBool srcPremultiplied,
                           int dstFormat, PRBool dstPremultiplied,
                           size_t dstTexelSize)
{
    if (width <= 0 || height <= 0)
        return;

    if (srcFormat == dstFormat &&
        srcPremultiplied == dstPremultiplied)
    {
        // fast exit path: we just have to memcpy all the rows.
        //
        // The case where absolutely nothing needs to be done is supposed to have
        // been handled earlier (in TexImage2D_base, etc).
        //
        // So the case we're handling here is when even though no format conversion is needed,
        // we still might have to flip vertically and/or to adjust to a different stride.

        size_t row_size = width * dstTexelSize; // doesn't matter, src and dst formats agree
        const PRUint8* src_row = src;
        const PRUint8* src_end = src + height * srcStride;

        PRUint8* dst_row = mPixelStoreFlipY ? dst + (height-1) * dstStride : dst;
        ptrdiff_t dstStrideSigned(dstStride);
        ptrdiff_t dst_delta = mPixelStoreFlipY ? -dstStrideSigned : dstStrideSigned;

        while(src_row != src_end) {
            memcpy(dst_row, src_row, row_size);
            src_row += srcStride;
            dst_row += dst_delta;
        }
        return;
    }

    WebGLImageConverter converter;
    converter.flip = mPixelStoreFlipY;
    converter.width = width;
    converter.height = height;
    converter.srcStride = srcStride;
    converter.dstStride = dstStride;
    converter.dstTexelSize = dstTexelSize;
    converter.src = src;
    converter.dst = dst;

    int premultiplicationOp = (!srcPremultiplied && dstPremultiplied) ? WebGLTexelPremultiplicationOp::Premultiply
                            : (srcPremultiplied && !dstPremultiplied) ? WebGLTexelPremultiplicationOp::Unmultiply
                            : WebGLTexelPremultiplicationOp::None;

#define HANDLE_DSTFORMAT(format, SrcType, DstType, unpackFunc, packFunc) \
        case WebGLTexelFormat::format: \
            switch (premultiplicationOp) { \
                case WebGLTexelPremultiplicationOp::Premultiply: \
                    converter.run<SrcType, DstType, \
                                  WebGLTexelConversions::unpackFunc, \
                                  WebGLTexelConversions::packFunc##Premultiply>(); \
                break; \
                case WebGLTexelPremultiplicationOp::Unmultiply: \
                    converter.run<SrcType, DstType, \
                                  WebGLTexelConversions::unpackFunc, \
                                  WebGLTexelConversions::packFunc##Unmultiply>(); \
                break; \
                default: \
                    converter.run<SrcType, DstType, \
                                  WebGLTexelConversions::unpackFunc, \
                                  WebGLTexelConversions::packFunc>(); \
                break; \
            } \
            break;

#define HANDLE_SRCFORMAT(format, size, SrcType, unpackFunc) \
        case WebGLTexelFormat::format: \
            converter.srcTexelSize = size; \
            switch (dstFormat) { \
                HANDLE_DSTFORMAT(RGBA8,    SrcType, PRUint8,  unpackFunc, packRGBA8ToRGBA8) \
                HANDLE_DSTFORMAT(RGB8,     SrcType, PRUint8,  unpackFunc, packRGBA8ToRGB8) \
                HANDLE_DSTFORMAT(R8,       SrcType, PRUint8,  unpackFunc, packRGBA8ToR8) \
                HANDLE_DSTFORMAT(RA8,      SrcType, PRUint8,  unpackFunc, packRGBA8ToRA8) \
                HANDLE_DSTFORMAT(RGBA5551, SrcType, PRUint16, unpackFunc, packRGBA8ToUnsignedShort5551) \
                HANDLE_DSTFORMAT(RGBA4444, SrcType, PRUint16, unpackFunc, packRGBA8ToUnsignedShort4444) \
                HANDLE_DSTFORMAT(RGB565,   SrcType, PRUint16, unpackFunc, packRGBA8ToUnsignedShort565) \
                /* A8 needs to be special-cased as it doesn't have color channels to premultiply */ \
                case WebGLTexelFormat::A8: \
                    converter.run<SrcType, PRUint8, \
                                  WebGLTexelConversions::unpackFunc, \
                                  WebGLTexelConversions::packRGBA8ToA8>(); \
                    break; \
                default: \
                    NS_ASSERTION(PR_FALSE, "Coding error?! Should never reach this point."); \
                    return; \
            } \
            break;

    switch (srcFormat) {
        HANDLE_SRCFORMAT(RGBA8,    4, PRUint8,  unpackRGBA8ToRGBA8)
        HANDLE_SRCFORMAT(RGBX8,    4, PRUint8,  unpackRGB8ToRGBA8)
        HANDLE_SRCFORMAT(RGB8,     3, PRUint8,  unpackRGB8ToRGBA8)
        HANDLE_SRCFORMAT(BGRA8,    4, PRUint8,  unpackBGRA8ToRGBA8)
        HANDLE_SRCFORMAT(BGRX8,    4, PRUint8,  unpackBGR8ToRGBA8)
        HANDLE_SRCFORMAT(BGR8,     3, PRUint8,  unpackBGR8ToRGBA8)
        HANDLE_SRCFORMAT(R8,       1, PRUint8,  unpackR8ToRGBA8)
        HANDLE_SRCFORMAT(A8,       1, PRUint8,  unpackA8ToRGBA8)
        HANDLE_SRCFORMAT(RA8,      2, PRUint8,  unpackRA8ToRGBA8)
        HANDLE_SRCFORMAT(RGBA5551, 2, PRUint16, unpackRGBA5551ToRGBA8)
        HANDLE_SRCFORMAT(RGBA4444, 2, PRUint16, unpackRGBA4444ToRGBA8)
        HANDLE_SRCFORMAT(RGB565,   2, PRUint16, unpackRGB565ToRGBA8)
        default:
            NS_ASSERTION(PR_FALSE, "Coding error?! Should never reach this point.");
            return;
    }
}

nsresult
WebGLContext::DOMElementToImageSurface(nsIDOMElement *imageOrCanvas,
                                       gfxImageSurface **imageOut, int *format)
{
    gfxImageSurface *surf = nsnull;

    PRUint32 flags =
        nsLayoutUtils::SFE_WANT_NEW_SURFACE |
        nsLayoutUtils::SFE_WANT_IMAGE_SURFACE;

    if (mPixelStoreColorspaceConversion == LOCAL_GL_NONE)
        flags |= nsLayoutUtils::SFE_NO_COLORSPACE_CONVERSION;
    if (!mPixelStorePremultiplyAlpha)
        flags |= nsLayoutUtils::SFE_NO_PREMULTIPLY_ALPHA;

    nsLayoutUtils::SurfaceFromElementResult res =
        nsLayoutUtils::SurfaceFromElement(imageOrCanvas, flags);
    if (!res.mSurface)
        return NS_ERROR_FAILURE;
    if (res.mSurface->GetType() != gfxASurface::SurfaceTypeImage) {
        // SurfaceFromElement lied!
        return NS_ERROR_FAILURE;
    }

    // Bug 656277 - Prevent loading WebGL textures from cross-domain images
    //
    // We disallow loading cross-domain images as WebGL textures. The reason for doing that
    // is that timing attacks on WebGL shaders are able to retrieve approximations of the pixel values
    // in WebGL textures, see bug 655987.
    //
    // To prevent a loophole where a Canvas2D would be used as a proxy to load cross-domain textures,
    // we also disallow loading textures from write-only Canvas2D's.

    // part 1: check that the DOM element is same-origin.
    // if res.mPrincipal == null, no need for the origin check. See DoDrawImageSecurityCheck.
    // this case happens in the mochitest for images served from mochi.test:8888
    if (res.mPrincipal) {
        PRBool subsumes;
        nsresult rv = HTMLCanvasElement()->NodePrincipal()->Subsumes(res.mPrincipal, &subsumes);
        if (NS_FAILED(rv) || !subsumes) {
            LogMessageIfVerbose("It is forbidden to load a WebGL texture from a cross-domain element. "
                                "See https://developer.mozilla.org/en/WebGL/Cross-Domain_Textures");
            return NS_ERROR_DOM_SECURITY_ERR;
        }
    }

    // part 2: if the DOM element is a canvas, check that it's not write-only. That would indicate a tainted canvas,
    // i.e. a canvas that could contain cross-domain image data.
    nsCOMPtr<nsIContent> maybeDOMCanvas = do_QueryInterface(imageOrCanvas);
    if (maybeDOMCanvas && maybeDOMCanvas->IsHTML(nsGkAtoms::canvas)) {
        nsHTMLCanvasElement *canvas = static_cast<nsHTMLCanvasElement*>(maybeDOMCanvas.get());
        if (canvas->IsWriteOnly()) {
            LogMessageIfVerbose("The canvas used as source for texImage2D here is tainted (write-only). It is forbidden "
                                "to load a WebGL texture from a tainted canvas. A Canvas becomes tainted for example "
                                "when a cross-domain image is drawn on it. "
                                "See https://developer.mozilla.org/en/WebGL/Cross-Domain_Textures");
            return NS_ERROR_DOM_SECURITY_ERR;
        }
    }

    // End of security checks, now we should be safe regarding cross-domain images
    // Notice that there is never a need to mark the WebGL canvas as write-only, since we reject write-only/cross-domain
    // texture sources in the first place.

    surf = static_cast<gfxImageSurface*>(res.mSurface.get());

    res.mSurface.forget();
    *imageOut = surf;

    switch (surf->Format()) {
        case gfxASurface::ImageFormatARGB32:
            *format = WebGLTexelFormat::BGRA8; // careful, our ARGB means BGRA
            break;
        case gfxASurface::ImageFormatRGB24:
            *format = WebGLTexelFormat::BGRX8; // careful, our RGB24 is not tightly packed. Whence BGRX8.
            break;
        case gfxASurface::ImageFormatA8:
            *format = WebGLTexelFormat::A8;
            break;
        case gfxASurface::ImageFormatRGB16_565:
            *format = WebGLTexelFormat::RGB565;
            break;
        default:
            NS_ASSERTION(PR_FALSE, "Unsupported image format. Unimplemented.");
            return NS_ERROR_NOT_IMPLEMENTED;
    }

    return NS_OK;
}

#define OBTAIN_UNIFORM_LOCATION(info)                                   \
    WebGLUniformLocation *location_object;                              \
    PRBool isNull;                                                      \
    if (!GetConcreteObject(info, ploc, &location_object, &isNull))      \
        return NS_OK;                                                   \
    if (isNull)                                                         \
        return NS_OK;                                                   \
    if (mCurrentProgram != location_object->Program())                  \
        return ErrorInvalidOperation("%s: this uniform location doesn't correspond to the current program", info); \
    if (mCurrentProgram->Generation() != location_object->ProgramGeneration())            \
        return ErrorInvalidOperation("%s: This uniform location is obsolete since the program has been relinked", info); \
    GLint location = location_object->Location();

#define SIMPLE_ARRAY_METHOD_UNIFORM(name, cnt, arrayType, ptrType)      \
NS_IMETHODIMP                                                           \
WebGLContext::name(PRInt32 dummy) {                                     \
     return NS_ERROR_NOT_IMPLEMENTED;                                   \
}                                                                       \
NS_IMETHODIMP                                                           \
WebGLContext::name##_array(nsIWebGLUniformLocation *ploc, js::TypedArray *wa) \
{                                                                       \
    OBTAIN_UNIFORM_LOCATION(#name ": location")                         \
    if (!wa || wa->type != js::TypedArray::arrayType)                   \
        return ErrorInvalidOperation(#name ": array must be " #arrayType);      \
    if (wa->length == 0 || wa->length % cnt != 0)                       \
        return ErrorInvalidValue(#name ": array must be > 0 elements and have a length multiple of %d", cnt); \
    MakeContextCurrent();                                               \
    gl->f##name(location, wa->length / cnt, (ptrType *)wa->data);            \
    return NS_OK;                                                       \
}

#define SIMPLE_MATRIX_METHOD_UNIFORM(name, dim, arrayType, ptrType)     \
NS_IMETHODIMP                                                           \
WebGLContext::name(PRInt32 dummy) {                                     \
     return NS_ERROR_NOT_IMPLEMENTED;                                   \
}                                                                       \
NS_IMETHODIMP                                                           \
WebGLContext::name##_array(nsIWebGLUniformLocation *ploc, WebGLboolean transpose, js::TypedArray *wa)  \
{                                                                       \
    OBTAIN_UNIFORM_LOCATION(#name ": location")                         \
    if (!wa || wa->type != js::TypedArray::arrayType)                   \
        return ErrorInvalidValue(#name ": array must be " #arrayType);      \
    if (wa->length == 0 || wa->length % (dim*dim) != 0)                 \
        return ErrorInvalidValue(#name ": array length must be >0 and multiple of %d", dim*dim); \
    if (transpose)                                                      \
        return ErrorInvalidValue(#name ": transpose must be FALSE as per the OpenGL ES 2.0 spec"); \
    MakeContextCurrent();                                               \
    gl->f##name(location, wa->length / (dim*dim), transpose, (ptrType *)wa->data); \
    return NS_OK;                                                       \
}

#define SIMPLE_METHOD_UNIFORM_1(glname, name, t1)        \
NS_IMETHODIMP WebGLContext::name(nsIWebGLUniformLocation *ploc, t1 a1) {      \
    OBTAIN_UNIFORM_LOCATION(#name ": location") \
    MakeContextCurrent(); gl->f##glname(location, a1); return NS_OK; \
}

#define SIMPLE_METHOD_UNIFORM_2(glname, name, t1, t2)        \
NS_IMETHODIMP WebGLContext::name(nsIWebGLUniformLocation *ploc, t1 a1, t2 a2) {      \
    OBTAIN_UNIFORM_LOCATION(#name ": location") \
    MakeContextCurrent(); gl->f##glname(location, a1, a2); return NS_OK; \
}

#define SIMPLE_METHOD_UNIFORM_3(glname, name, t1, t2, t3)        \
NS_IMETHODIMP WebGLContext::name(nsIWebGLUniformLocation *ploc, t1 a1, t2 a2, t3 a3) {      \
    OBTAIN_UNIFORM_LOCATION(#name ": location") \
    MakeContextCurrent(); gl->f##glname(location, a1, a2, a3); return NS_OK; \
}

#define SIMPLE_METHOD_UNIFORM_4(glname, name, t1, t2, t3, t4)        \
NS_IMETHODIMP WebGLContext::name(nsIWebGLUniformLocation *ploc, t1 a1, t2 a2, t3 a3, t4 a4) {      \
    OBTAIN_UNIFORM_LOCATION(#name ": location") \
    MakeContextCurrent(); gl->f##glname(location, a1, a2, a3, a4); return NS_OK; \
}

SIMPLE_METHOD_UNIFORM_1(Uniform1i, Uniform1i, WebGLint)
SIMPLE_METHOD_UNIFORM_2(Uniform2i, Uniform2i, WebGLint, WebGLint)
SIMPLE_METHOD_UNIFORM_3(Uniform3i, Uniform3i, WebGLint, WebGLint, WebGLint)
SIMPLE_METHOD_UNIFORM_4(Uniform4i, Uniform4i, WebGLint, WebGLint, WebGLint, WebGLint)

SIMPLE_METHOD_UNIFORM_1(Uniform1f, Uniform1f, WebGLfloat)
SIMPLE_METHOD_UNIFORM_2(Uniform2f, Uniform2f, WebGLfloat, WebGLfloat)
SIMPLE_METHOD_UNIFORM_3(Uniform3f, Uniform3f, WebGLfloat, WebGLfloat, WebGLfloat)
SIMPLE_METHOD_UNIFORM_4(Uniform4f, Uniform4f, WebGLfloat, WebGLfloat, WebGLfloat, WebGLfloat)

SIMPLE_ARRAY_METHOD_UNIFORM(Uniform1iv, 1, TYPE_INT32, WebGLint)
SIMPLE_ARRAY_METHOD_UNIFORM(Uniform2iv, 2, TYPE_INT32, WebGLint)
SIMPLE_ARRAY_METHOD_UNIFORM(Uniform3iv, 3, TYPE_INT32, WebGLint)
SIMPLE_ARRAY_METHOD_UNIFORM(Uniform4iv, 4, TYPE_INT32, WebGLint)

SIMPLE_ARRAY_METHOD_UNIFORM(Uniform1fv, 1, TYPE_FLOAT32, WebGLfloat)
SIMPLE_ARRAY_METHOD_UNIFORM(Uniform2fv, 2, TYPE_FLOAT32, WebGLfloat)
SIMPLE_ARRAY_METHOD_UNIFORM(Uniform3fv, 3, TYPE_FLOAT32, WebGLfloat)
SIMPLE_ARRAY_METHOD_UNIFORM(Uniform4fv, 4, TYPE_FLOAT32, WebGLfloat)

SIMPLE_MATRIX_METHOD_UNIFORM(UniformMatrix2fv, 2, TYPE_FLOAT32, WebGLfloat)
SIMPLE_MATRIX_METHOD_UNIFORM(UniformMatrix3fv, 3, TYPE_FLOAT32, WebGLfloat)
SIMPLE_MATRIX_METHOD_UNIFORM(UniformMatrix4fv, 4, TYPE_FLOAT32, WebGLfloat)

NS_IMETHODIMP
WebGLContext::VertexAttrib1f(PRUint32 index, WebGLfloat x0)
{
    MakeContextCurrent();

    if (index) {
        gl->fVertexAttrib1f(index, x0);
    } else {
        mVertexAttrib0Vector[0] = x0;
        mVertexAttrib0Vector[1] = 0;
        mVertexAttrib0Vector[2] = 0;
        mVertexAttrib0Vector[3] = 1;
        if (gl->IsGLES2())
            gl->fVertexAttrib1f(index, x0);
    }

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::VertexAttrib2f(PRUint32 index, WebGLfloat x0, WebGLfloat x1)
{
    MakeContextCurrent();

    if (index) {
        gl->fVertexAttrib2f(index, x0, x1);
    } else {
        mVertexAttrib0Vector[0] = x0;
        mVertexAttrib0Vector[1] = x1;
        mVertexAttrib0Vector[2] = 0;
        mVertexAttrib0Vector[3] = 1;
        if (gl->IsGLES2())
            gl->fVertexAttrib2f(index, x0, x1);
    }

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::VertexAttrib3f(PRUint32 index, WebGLfloat x0, WebGLfloat x1, WebGLfloat x2)
{
    MakeContextCurrent();

    if (index) {
        gl->fVertexAttrib3f(index, x0, x1, x2);
    } else {
        mVertexAttrib0Vector[0] = x0;
        mVertexAttrib0Vector[1] = x1;
        mVertexAttrib0Vector[2] = x2;
        mVertexAttrib0Vector[3] = 1;
        if (gl->IsGLES2())
            gl->fVertexAttrib3f(index, x0, x1, x2);
    }

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::VertexAttrib4f(PRUint32 index, WebGLfloat x0, WebGLfloat x1,
                                             WebGLfloat x2, WebGLfloat x3)
{
    MakeContextCurrent();

    if (index) {
        gl->fVertexAttrib4f(index, x0, x1, x2, x3);
    } else {
        mVertexAttrib0Vector[0] = x0;
        mVertexAttrib0Vector[1] = x1;
        mVertexAttrib0Vector[2] = x2;
        mVertexAttrib0Vector[3] = x3;
        if (gl->IsGLES2())
            gl->fVertexAttrib4f(index, x0, x1, x2, x3);
    }

    return NS_OK;
}

#define SIMPLE_ARRAY_METHOD_NO_COUNT(name, cnt, arrayType, ptrType)  \
NS_IMETHODIMP                                                           \
WebGLContext::name(PRInt32 dummy) {                                     \
     return NS_ERROR_NOT_IMPLEMENTED;                                   \
}                                                                       \
NS_IMETHODIMP                                                           \
WebGLContext::name##_array(WebGLuint idx, js::TypedArray *wa)           \
{                                                                       \
    if (!wa || wa->type != js::TypedArray::arrayType)                   \
        return ErrorInvalidOperation(#name ": array must be " #arrayType); \
    if (wa->length < cnt)                                               \
        return ErrorInvalidOperation(#name ": array must be >= %d elements", cnt); \
    MakeContextCurrent();                                               \
    ptrType *ptr = (ptrType *)wa->data;                                  \
    if (idx) {                                                        \
        gl->f##name(idx, ptr);                                          \
    } else {                                                            \
        mVertexAttrib0Vector[0] = ptr[0];                               \
        mVertexAttrib0Vector[1] = cnt > 1 ? ptr[1] : ptrType(0);        \
        mVertexAttrib0Vector[2] = cnt > 2 ? ptr[2] : ptrType(0);        \
        mVertexAttrib0Vector[3] = cnt > 3 ? ptr[3] : ptrType(1);        \
        if (gl->IsGLES2())                                              \
            gl->f##name(idx, ptr);                                      \
    }                                                                   \
    return NS_OK;                                                       \
}

SIMPLE_ARRAY_METHOD_NO_COUNT(VertexAttrib1fv, 1, TYPE_FLOAT32, WebGLfloat)
SIMPLE_ARRAY_METHOD_NO_COUNT(VertexAttrib2fv, 2, TYPE_FLOAT32, WebGLfloat)
SIMPLE_ARRAY_METHOD_NO_COUNT(VertexAttrib3fv, 3, TYPE_FLOAT32, WebGLfloat)
SIMPLE_ARRAY_METHOD_NO_COUNT(VertexAttrib4fv, 4, TYPE_FLOAT32, WebGLfloat)

NS_IMETHODIMP
WebGLContext::UseProgram(nsIWebGLProgram *pobj)
{
    WebGLProgram *prog;
    WebGLuint progname;
    PRBool isNull;
    if (!GetConcreteObjectAndGLName("useProgram", pobj, &prog, &progname, &isNull))
        return NS_OK;

    MakeContextCurrent();

    if (prog && !prog->LinkStatus())
        return ErrorInvalidOperation("UseProgram: program was not linked successfully");

    gl->fUseProgram(progname);

    if (mCurrentProgram && mCurrentProgram->HasDeletePending()) {
        mCurrentProgram->DetachShaders();
        mCurrentProgram->ClearDeletePending();
    }

    mCurrentProgram = prog;

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::ValidateProgram(nsIWebGLProgram *pobj)
{
    WebGLuint progname;
    if (!GetGLName<WebGLProgram>("validateProgram", pobj, &progname))
        return NS_OK;

    MakeContextCurrent();

#ifdef XP_MACOSX
    // see bug 593867 for NVIDIA and bug 657201 for ATI. The latter is confirmed with Mac OS 10.6.7
    LogMessageIfVerbose("validateProgram: implemented as a no-operation on Mac to work around crashes");
    return NS_OK;
#endif

    gl->fValidateProgram(progname);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::CreateFramebuffer(nsIWebGLFramebuffer **retval)
{
    *retval = 0;

    MakeContextCurrent();

    GLuint name;
    gl->fGenFramebuffers(1, &name);

    WebGLFramebuffer *globj = new WebGLFramebuffer(this, name);
    NS_ADDREF(*retval = globj);
    mMapFramebuffers.Put(name, globj);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::CreateRenderbuffer(nsIWebGLRenderbuffer **retval)
{
    *retval = 0;

    MakeContextCurrent();

    GLuint name;
    gl->fGenRenderbuffers(1, &name);

    WebGLRenderbuffer *globj = new WebGLRenderbuffer(this, name);
    NS_ADDREF(*retval = globj);
    mMapRenderbuffers.Put(name, globj);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::Viewport(WebGLint x, WebGLint y, WebGLsizei width, WebGLsizei height)
{
    if (width < 0 || height < 0)
        return ErrorInvalidValue("Viewport: negative size");

    MakeContextCurrent();
    gl->fViewport(x, y, width, height);
    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::CompileShader(nsIWebGLShader *sobj)
{
    WebGLShader *shader;
    WebGLuint shadername;
    if (!GetConcreteObjectAndGLName("compileShader", sobj, &shader, &shadername))
        return NS_OK;

    MakeContextCurrent();

#if defined(USE_ANGLE)
    if (shader->NeedsTranslation() && mShaderValidation) {
        ShHandle compiler = 0;
        ShBuiltInResources resources;
        memset(&resources, 0, sizeof(ShBuiltInResources));

        resources.MaxVertexAttribs = mGLMaxVertexAttribs;
        resources.MaxVertexUniformVectors = mGLMaxVertexUniformVectors;
        resources.MaxVaryingVectors = mGLMaxVaryingVectors;
        resources.MaxVertexTextureImageUnits = mGLMaxVertexTextureImageUnits;
        resources.MaxCombinedTextureImageUnits = mGLMaxTextureUnits;
        resources.MaxTextureImageUnits = mGLMaxTextureImageUnits;
        resources.MaxFragmentUniformVectors = mGLMaxFragmentUniformVectors;
        resources.MaxDrawBuffers = 1;

        compiler = ShConstructCompiler((ShShaderType) shader->ShaderType(),
                                       SH_WEBGL_SPEC,
                                       &resources);

        nsPromiseFlatCString src(shader->Source());
        const char *s = src.get();

        if (!ShCompile(compiler, &s, 1, SH_OBJECT_CODE)) {
            int len = 0;
            ShGetInfo(compiler, SH_INFO_LOG_LENGTH, &len);

            if (len) {
                nsCAutoString info;
                info.SetLength(len);
                ShGetInfoLog(compiler, info.BeginWriting());
                shader->SetTranslationFailure(info);
            } else {
                shader->SetTranslationFailure(NS_LITERAL_CSTRING("Internal error: failed to get shader info log"));
            }
            ShDestruct(compiler);
            return NS_OK;
        }

        /* If the GL context is really GLES2, we want to use the original provided code,
         * since it's actually GLES2.  We still need to validate it however, which is
         * why we ran it through the above, but we don't want the desktop GLSL.
         */
        if (!gl->IsGLES2()) {
            int len = 0;
            ShGetInfo(compiler, SH_OBJECT_CODE_LENGTH, &len);

            nsCAutoString translatedSrc;
            translatedSrc.SetLength(len);
            ShGetObjectCode(compiler, translatedSrc.BeginWriting());

            nsPromiseFlatCString translatedSrc2(translatedSrc);
            const char *ts = translatedSrc2.get();

            gl->fShaderSource(shadername, 1, &ts, NULL);
        } else {
            gl->fShaderSource(shadername, 1, &s, NULL);
        }

        shader->SetTranslationSuccess();

        ShDestruct(compiler);
    } else
#endif
    {
        const char *s = nsDependentCString(shader->Source()).get();
        gl->fShaderSource(shadername, 1, &s, NULL);
        shader->SetTranslationSuccess();
    }

    gl->fCompileShader(shadername);

    return NS_OK;
}


NS_IMETHODIMP
WebGLContext::GetShaderParameter(nsIWebGLShader *sobj, WebGLenum pname, nsIVariant **retval)
{
    *retval = nsnull;

    WebGLShader *shader;
    WebGLuint shadername;
    if (!GetConcreteObjectAndGLName("getShaderParameter: shader", sobj, &shader, &shadername))
        return NS_OK;

    nsCOMPtr<nsIWritableVariant> wrval = do_CreateInstance("@mozilla.org/variant;1");
    NS_ENSURE_TRUE(wrval, NS_ERROR_FAILURE);

    MakeContextCurrent();

    switch (pname) {
        case LOCAL_GL_SHADER_TYPE:
        case LOCAL_GL_INFO_LOG_LENGTH:
        {
            GLint i = 0;
            gl->fGetShaderiv(shadername, pname, &i);
            wrval->SetAsInt32(i);
        }
            break;
        case LOCAL_GL_SHADER_SOURCE_LENGTH:
        {
            wrval->SetAsInt32(PRInt32(shader->Source().Length()) + 1);
        }
            break;
        case LOCAL_GL_DELETE_STATUS:
        case LOCAL_GL_COMPILE_STATUS:
        {
            GLint i = 0;
            gl->fGetShaderiv(shadername, pname, &i);
            wrval->SetAsBool(PRBool(i));
        }
            break;

        default:
            return NS_ERROR_NOT_IMPLEMENTED;
    }

    *retval = wrval.forget().get();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetShaderInfoLog(nsIWebGLShader *sobj, nsAString& retval)
{
    WebGLShader *shader;
    WebGLuint shadername;
    if (!GetConcreteObjectAndGLName("getShaderInfoLog: shader", sobj, &shader, &shadername))
        return NS_OK;

    const nsCString& tlog = shader->TranslationLog();
    if (!tlog.IsVoid()) {
        CopyASCIItoUTF16(tlog, retval);
        return NS_OK;
    }

    MakeContextCurrent();

    PRInt32 k = -1;
    gl->fGetShaderiv(shadername, LOCAL_GL_INFO_LOG_LENGTH, (GLint*) &k);
    if (k == -1)
        return NS_ERROR_FAILURE; // XXX GL Error? should never happen.

    if (k == 0) {
        retval.Truncate();
        return NS_OK;
    }

    nsCAutoString log;
    log.SetCapacity(k);

    gl->fGetShaderInfoLog(shadername, k, (GLint*) &k, (char*) log.BeginWriting());

    log.SetLength(k);

    CopyASCIItoUTF16(log, retval);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::GetShaderSource(nsIWebGLShader *sobj, nsAString& retval)
{
    WebGLShader *shader;
    WebGLuint shadername;
    if (!GetConcreteObjectAndGLName("getShaderSource: shader", sobj, &shader, &shadername))
        return NS_OK;

    CopyASCIItoUTF16(shader->Source(), retval);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::ShaderSource(nsIWebGLShader *sobj, const nsAString& source)
{
    WebGLShader *shader;
    WebGLuint shadername;
    if (!GetConcreteObjectAndGLName("shaderSource: shader", sobj, &shader, &shadername))
        return NS_OK;

    if (!NS_IsAscii(nsPromiseFlatString(source).get()))
        return ErrorInvalidValue("shaderSource: non-ascii characters found in source");

    shader->SetSource(NS_LossyConvertUTF16toASCII(source));

    shader->SetNeedsTranslation();

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::VertexAttribPointer(WebGLuint index, WebGLint size, WebGLenum type,
                                  WebGLboolean normalized, WebGLsizei stride,
                                  WebGLsizeiptr byteOffset)
{
    if (mBoundArrayBuffer == nsnull)
        return ErrorInvalidOperation("VertexAttribPointer: must have valid GL_ARRAY_BUFFER binding");

    WebGLsizei requiredAlignment = 1;
    switch (type) {
      case LOCAL_GL_BYTE:
      case LOCAL_GL_UNSIGNED_BYTE:
          requiredAlignment = 1;
          break;
      case LOCAL_GL_SHORT:
      case LOCAL_GL_UNSIGNED_SHORT:
          requiredAlignment = 2;
          break;
      // XXX case LOCAL_GL_FIXED:
      case LOCAL_GL_FLOAT:
          requiredAlignment = 4;
          break;
      default:
          return ErrorInvalidEnumInfo("VertexAttribPointer: type", type);
    }

    // requiredAlignment should always be a power of two.
    WebGLsizei requiredAlignmentMask = requiredAlignment - 1;

    if (!ValidateAttribIndex(index, "vertexAttribPointer"))
        return NS_OK;

    if (size < 1 || size > 4)
        return ErrorInvalidValue("VertexAttribPointer: invalid element size");

    if (stride < 0 || stride > 255) // see WebGL spec section 6.6 "Vertex Attribute Data Stride"
        return ErrorInvalidValue("VertexAttribPointer: negative or too large stride");

    if (byteOffset < 0)
        return ErrorInvalidValue("VertexAttribPointer: negative offset");

    if (stride & requiredAlignmentMask) {
        return ErrorInvalidOperation("VertexAttribPointer: stride doesn't satisfy the alignment "
                                     "requirement of given type");
    }

    if (byteOffset & requiredAlignmentMask) {
        return ErrorInvalidOperation("VertexAttribPointer: byteOffset doesn't satisfy the alignment "
                                     "requirement of given type");

    }
    
    /* XXX make work with bufferSubData & heterogeneous types 
    if (type != mBoundArrayBuffer->GLType())
        return ErrorInvalidOperation("VertexAttribPointer: type must match bound VBO type: %d != %d", type, mBoundArrayBuffer->GLType());
    */

    WebGLVertexAttribData &vd = mAttribBuffers[index];

    vd.buf = mBoundArrayBuffer;
    vd.stride = stride;
    vd.size = size;
    vd.byteOffset = byteOffset;
    vd.type = type;
    vd.normalized = normalized;

    MakeContextCurrent();

    gl->fVertexAttribPointer(index, size, type, normalized,
                             stride,
                             (void*) (byteOffset));

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::TexImage2D(PRInt32 dummy)
{
    return NS_ERROR_FAILURE;
}

nsresult
WebGLContext::TexImage2D_base(WebGLenum target, WebGLint level, WebGLenum internalformat,
                              WebGLsizei width, WebGLsizei height, WebGLsizei srcStrideOrZero,
                              WebGLint border,
                              WebGLenum format, WebGLenum type,
                              void *data, PRUint32 byteLength,
                              int srcFormat, PRBool srcPremultiplied)
{
    switch (target) {
        case LOCAL_GL_TEXTURE_2D:
            break;
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            if (width != height)
                return ErrorInvalidValue("texImage2D: with cube map targets, width and height must be equal");
            break;
        default:
            return ErrorInvalidEnumInfo("texImage2D: target", target);
    }

    switch (internalformat) {
        case LOCAL_GL_RGB:
        case LOCAL_GL_RGBA:
        case LOCAL_GL_ALPHA:
        case LOCAL_GL_LUMINANCE:
        case LOCAL_GL_LUMINANCE_ALPHA:
            break;
        default:
            return ErrorInvalidEnumInfo("texImage2D: internal format", internalformat);
    }

    if (format != internalformat)
        return ErrorInvalidOperation("texImage2D: format does not match internalformat");

    WebGLsizei maxTextureSize = MaxTextureSizeForTarget(target);

    if (level < 0)
        return ErrorInvalidValue("texImage2D: level must be >= 0");

    if (!(maxTextureSize >> level))
        return ErrorInvalidValue("texImage2D: 2^level exceeds maximum texture size");

    if (width < 0 || height < 0)
        return ErrorInvalidValue("texImage2D: width and height must be >= 0");

    if (width > maxTextureSize || height > maxTextureSize)
        return ErrorInvalidValue("texImage2D: width or height exceeds maximum texture size");

    if (level >= 1) {
        if (!(is_pot_assuming_nonnegative(width) &&
              is_pot_assuming_nonnegative(height)))
            return ErrorInvalidValue("texImage2D: with level > 0, width and height must be powers of two");
    }

    if (border != 0)
        return ErrorInvalidValue("TexImage2D: border must be 0");

    PRUint32 texelSize = 0;
    if (!ValidateTexFormatAndType(format, type, &texelSize, "texImage2D"))
        return NS_OK;

    CheckedUint32 checked_plainRowSize = CheckedUint32(width) * texelSize;

    PRUint32 unpackAlignment = mPixelStoreUnpackAlignment;

    // alignedRowSize = row size rounded up to next multiple of packAlignment
    CheckedUint32 checked_alignedRowSize
        = ((checked_plainRowSize + unpackAlignment-1) / unpackAlignment) * unpackAlignment;

    CheckedUint32 checked_neededByteLength
        = (height-1) * checked_alignedRowSize + checked_plainRowSize;

    if (!checked_neededByteLength.valid())
        return ErrorInvalidOperation("texImage2D: integer overflow computing the needed buffer size");

    PRUint32 bytesNeeded = checked_neededByteLength.value();

    if (byteLength && byteLength < bytesNeeded)
        return ErrorInvalidOperation("TexImage2D: not enough data for operation (need %d, have %d)",
                                 bytesNeeded, byteLength);

    WebGLTexture *tex = activeBoundTextureForTarget(target);

    if (!tex)
        return ErrorInvalidOperation("texImage2D: no texture is bound to this target");

    tex->SetImageInfo(target, level, width, height, format, type);

    MakeContextCurrent();

    if (byteLength) {
        int dstFormat = GetWebGLTexelFormat(format, type);
        int actualSrcFormat = srcFormat == WebGLTexelFormat::Auto ? dstFormat : srcFormat;
        size_t srcStride = srcStrideOrZero ? srcStrideOrZero : checked_alignedRowSize.value();

        size_t dstPlainRowSize = texelSize * width;
        size_t unpackAlignment = mPixelStoreUnpackAlignment;
        size_t dstStride = ((dstPlainRowSize + unpackAlignment-1) / unpackAlignment) * unpackAlignment;

        if (actualSrcFormat == dstFormat &&
            srcPremultiplied == mPixelStorePremultiplyAlpha &&
            srcStride == dstStride &&
            !mPixelStoreFlipY)
        {
            // no conversion, no flipping, so we avoid copying anything and just pass the source pointer
            gl->fTexImage2D(target, level, internalformat, width, height, border, format, type, data);
        }
        else
        {
            nsAutoArrayPtr<PRUint8> convertedData(new PRUint8[bytesNeeded]);
            ConvertImage(width, height, srcStride, dstStride,
                         (PRUint8*)data, convertedData,
                         actualSrcFormat, srcPremultiplied,
                         dstFormat, mPixelStorePremultiplyAlpha, texelSize);
            gl->fTexImage2D(target, level, internalformat, width, height, border, format, type, convertedData);
        }
    } else {
        // We need some zero pages, because GL doesn't guarantee the
        // contents of a texture allocated with NULL data.
        // Hopefully calloc will just mmap zero pages here.
        void *tempZeroData = calloc(1, bytesNeeded);
        if (!tempZeroData)
            return ErrorOutOfMemory("texImage2D: could not allocate %d bytes (for zero fill)", bytesNeeded);

        gl->fTexImage2D(target, level, internalformat, width, height, border, format, type, tempZeroData);

        free(tempZeroData);
    }

    tex->setDimensions(width, height);

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::TexImage2D_buf(WebGLenum target, WebGLint level, WebGLenum internalformat,
                             WebGLsizei width, WebGLsizei height, WebGLint border,
                             WebGLenum format, WebGLenum type,
                             js::ArrayBuffer *pixels)
{
    return TexImage2D_base(target, level, internalformat, width, height, 0, border, format, type,
                           pixels ? pixels->data : 0,
                           pixels ? pixels->byteLength : 0,
                           WebGLTexelFormat::Auto, PR_FALSE);
}

NS_IMETHODIMP
WebGLContext::TexImage2D_array(WebGLenum target, WebGLint level, WebGLenum internalformat,
                               WebGLsizei width, WebGLsizei height, WebGLint border,
                               WebGLenum format, WebGLenum type,
                               js::TypedArray *pixels)
{
    return TexImage2D_base(target, level, internalformat, width, height, 0, border, format, type,
                           pixels ? pixels->data : 0,
                           pixels ? pixels->byteLength : 0,
                           WebGLTexelFormat::Auto, PR_FALSE);
}

NS_IMETHODIMP
WebGLContext::TexImage2D_imageData(WebGLenum target, WebGLint level, WebGLenum internalformat,
                               WebGLsizei width, WebGLsizei height, WebGLint border,
                               WebGLenum format, WebGLenum type,
                               js::TypedArray *pixels)
{
    return TexImage2D_base(target, level, internalformat, width, height, 4*width, border, format, type,
                           pixels ? pixels->data : 0,
                           pixels ? pixels->byteLength : 0,
                           WebGLTexelFormat::RGBA8, PR_FALSE);
}

NS_IMETHODIMP
WebGLContext::TexImage2D_dom(WebGLenum target, WebGLint level, WebGLenum internalformat,
                             WebGLenum format, GLenum type, nsIDOMElement *elt)
{
    nsRefPtr<gfxImageSurface> isurf;

    int srcFormat;
    nsresult rv = DOMElementToImageSurface(elt, getter_AddRefs(isurf), &srcFormat);
    if (NS_FAILED(rv))
        return rv;

    PRUint32 byteLength = isurf->Stride() * isurf->Height();

    return TexImage2D_base(target, level, internalformat,
                           isurf->Width(), isurf->Height(), isurf->Stride(), 0,
                           format, type,
                           isurf->Data(), byteLength,
                           srcFormat, mPixelStorePremultiplyAlpha);
}

NS_IMETHODIMP
WebGLContext::TexSubImage2D(PRInt32 dummy)
{
    return NS_ERROR_FAILURE;
}

nsresult
WebGLContext::TexSubImage2D_base(WebGLenum target, WebGLint level,
                                 WebGLint xoffset, WebGLint yoffset,
                                 WebGLsizei width, WebGLsizei height, WebGLsizei srcStrideOrZero,
                                 WebGLenum format, WebGLenum type,
                                 void *pixels, PRUint32 byteLength,
                                 int srcFormat, PRBool srcPremultiplied)
{
    switch (target) {
        case LOCAL_GL_TEXTURE_2D:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            break;
        default:
            return ErrorInvalidEnumInfo("texSubImage2D: target", target);
    }

    WebGLsizei maxTextureSize = MaxTextureSizeForTarget(target);

    if (level < 0)
        return ErrorInvalidValue("texSubImage2D: level must be >= 0");

    if (!(maxTextureSize >> level))
        return ErrorInvalidValue("texSubImage2D: 2^level exceeds maximum texture size");

    if (width < 0 || height < 0)
        return ErrorInvalidValue("texSubImage2D: width and height must be >= 0");

    if (width > maxTextureSize || height > maxTextureSize)
        return ErrorInvalidValue("texSubImage2D: width or height exceeds maximum texture size");

    if (level >= 1) {
        if (!(is_pot_assuming_nonnegative(width) &&
              is_pot_assuming_nonnegative(height)))
            return ErrorInvalidValue("texSubImage2D: with level > 0, width and height must be powers of two");
    }

    PRUint32 texelSize = 0;
    if (!ValidateTexFormatAndType(format, type, &texelSize, "texSubImage2D"))
        return NS_OK;

    if (width == 0 || height == 0)
        return NS_OK; // ES 2.0 says it has no effect, we better return right now

    CheckedUint32 checked_plainRowSize = CheckedUint32(width) * texelSize;

    PRUint32 unpackAlignment = mPixelStoreUnpackAlignment;

    // alignedRowSize = row size rounded up to next multiple of packAlignment
    CheckedUint32 checked_alignedRowSize
        = ((checked_plainRowSize + unpackAlignment-1) / unpackAlignment) * unpackAlignment;

    CheckedUint32 checked_neededByteLength
        = (height-1) * checked_alignedRowSize + checked_plainRowSize;

    if (!checked_neededByteLength.valid())
        return ErrorInvalidOperation("texSubImage2D: integer overflow computing the needed buffer size");

    PRUint32 bytesNeeded = checked_neededByteLength.value();
 
    if (byteLength < bytesNeeded)
        return ErrorInvalidOperation("texSubImage2D: not enough data for operation (need %d, have %d)", bytesNeeded, byteLength);

    WebGLTexture *tex = activeBoundTextureForTarget(target);

    if (!tex)
        return ErrorInvalidOperation("texSubImage2D: no texture is bound to this target");

    if (!CanvasUtils::CheckSaneSubrectSize(xoffset, yoffset, width, height, tex->width(), tex->height()))
        return ErrorInvalidValue("texSubImage2D: subtexture rectangle out of bounds");


    MakeContextCurrent();

    int dstFormat = GetWebGLTexelFormat(format, type);
    int actualSrcFormat = srcFormat == WebGLTexelFormat::Auto ? dstFormat : srcFormat;
    size_t srcStride = srcStrideOrZero ? srcStrideOrZero : checked_alignedRowSize.value();

    size_t dstPlainRowSize = texelSize * width;
    size_t dstStride = ((dstPlainRowSize + unpackAlignment-1) / unpackAlignment) * unpackAlignment;

    if (actualSrcFormat == dstFormat &&
        srcPremultiplied == mPixelStorePremultiplyAlpha &&
        srcStride == dstStride &&
        !mPixelStoreFlipY)
    {
        // no conversion, no flipping, so we avoid copying anything and just pass the source pointer
        gl->fTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    }
    else
    {
        nsAutoArrayPtr<PRUint8> convertedData(new PRUint8[bytesNeeded]);
        ConvertImage(width, height, srcStride, dstStride,
                    (const PRUint8*)pixels, convertedData,
                    actualSrcFormat, srcPremultiplied,
                    dstFormat, mPixelStorePremultiplyAlpha, texelSize);

        gl->fTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, convertedData);
    }

    return NS_OK;
}

NS_IMETHODIMP
WebGLContext::TexSubImage2D_buf(WebGLenum target, WebGLint level,
                                WebGLint xoffset, WebGLint yoffset,
                                WebGLsizei width, WebGLsizei height,
                                WebGLenum format, WebGLenum type,
                                js::ArrayBuffer *pixels)
{
    if (!pixels)
        return ErrorInvalidValue("TexSubImage2D: pixels must not be null!");

    return TexSubImage2D_base(target, level, xoffset, yoffset,
                              width, height, 0, format, type,
                              pixels->data, pixels->byteLength,
                              WebGLTexelFormat::Auto, PR_FALSE);
}

NS_IMETHODIMP
WebGLContext::TexSubImage2D_array(WebGLenum target, WebGLint level,
                                  WebGLint xoffset, WebGLint yoffset,
                                  WebGLsizei width, WebGLsizei height,
                                  WebGLenum format, WebGLenum type,
                                  js::TypedArray *pixels)
{
    if (!pixels)
        return ErrorInvalidValue("TexSubImage2D: pixels must not be null!");

    return TexSubImage2D_base(target, level, xoffset, yoffset,
                              width, height, 0, format, type,
                              pixels->data, pixels->byteLength,
                              WebGLTexelFormat::Auto, PR_FALSE);
}

NS_IMETHODIMP
WebGLContext::TexSubImage2D_imageData(WebGLenum target, WebGLint level,
                                      WebGLint xoffset, WebGLint yoffset,
                                      WebGLsizei width, WebGLsizei height,
                                      WebGLenum format, WebGLenum type,
                                      js::TypedArray *pixels)
{
    if (!pixels)
        return ErrorInvalidValue("TexSubImage2D: pixels must not be null!");

    return TexSubImage2D_base(target, level, xoffset, yoffset,
                              width, height, 4*width, format, type,
                              pixels->data, pixels->byteLength,
                              WebGLTexelFormat::RGBA8, PR_FALSE);
}

NS_IMETHODIMP
WebGLContext::TexSubImage2D_dom(WebGLenum target, WebGLint level,
                                WebGLint xoffset, WebGLint yoffset,
                                WebGLenum format, WebGLenum type,
                                nsIDOMElement *elt)
{
    nsRefPtr<gfxImageSurface> isurf;

    int srcFormat;
    nsresult rv = DOMElementToImageSurface(elt, getter_AddRefs(isurf), &srcFormat);
    if (NS_FAILED(rv))
        return rv;

    PRUint32 byteLength = isurf->Stride() * isurf->Height();

    return TexSubImage2D_base(target, level,
                              xoffset, yoffset,
                              isurf->Width(), isurf->Height(), isurf->Stride(),
                              format, type,
                              isurf->Data(), byteLength,
                              srcFormat, PR_TRUE);
}

PRBool
BaseTypeAndSizeFromUniformType(WebGLenum uType, WebGLenum *baseType, WebGLint *unitSize)
{
    switch (uType) {
        case LOCAL_GL_INT:
        case LOCAL_GL_INT_VEC2:
        case LOCAL_GL_INT_VEC3:
        case LOCAL_GL_INT_VEC4:
        case LOCAL_GL_SAMPLER_2D:
        case LOCAL_GL_SAMPLER_CUBE:
            *baseType = LOCAL_GL_INT;
            break;
        case LOCAL_GL_FLOAT:
        case LOCAL_GL_FLOAT_VEC2:
        case LOCAL_GL_FLOAT_VEC3:
        case LOCAL_GL_FLOAT_VEC4:
        case LOCAL_GL_FLOAT_MAT2:
        case LOCAL_GL_FLOAT_MAT3:
        case LOCAL_GL_FLOAT_MAT4:
            *baseType = LOCAL_GL_FLOAT;
            break;
        case LOCAL_GL_BOOL:
        case LOCAL_GL_BOOL_VEC2:
        case LOCAL_GL_BOOL_VEC3:
        case LOCAL_GL_BOOL_VEC4:
            *baseType = LOCAL_GL_BOOL; // pretend these are int
            break;
        default:
            return PR_FALSE;
    }

    switch (uType) {
        case LOCAL_GL_INT:
        case LOCAL_GL_FLOAT:
        case LOCAL_GL_BOOL:
        case LOCAL_GL_SAMPLER_2D:
        case LOCAL_GL_SAMPLER_CUBE:
            *unitSize = 1;
            break;
        case LOCAL_GL_INT_VEC2:
        case LOCAL_GL_FLOAT_VEC2:
        case LOCAL_GL_BOOL_VEC2:
            *unitSize = 2;
            break;
        case LOCAL_GL_INT_VEC3:
        case LOCAL_GL_FLOAT_VEC3:
        case LOCAL_GL_BOOL_VEC3:
            *unitSize = 3;
            break;
        case LOCAL_GL_INT_VEC4:
        case LOCAL_GL_FLOAT_VEC4:
        case LOCAL_GL_BOOL_VEC4:
            *unitSize = 4;
            break;
        case LOCAL_GL_FLOAT_MAT2:
            *unitSize = 4;
            break;
        case LOCAL_GL_FLOAT_MAT3:
            *unitSize = 9;
            break;
        case LOCAL_GL_FLOAT_MAT4:
            *unitSize = 16;
            break;
        default:
            return PR_FALSE;
    }

    return PR_TRUE;
}


int mozilla::GetWebGLTexelFormat(GLenum format, GLenum type)
{
    if (type == LOCAL_GL_UNSIGNED_BYTE)
    {
        switch (format) {
            case LOCAL_GL_RGBA:
                return WebGLTexelFormat::RGBA8;
            case LOCAL_GL_RGB:
                return WebGLTexelFormat::RGB8;
            case LOCAL_GL_ALPHA:
                return WebGLTexelFormat::A8;
            case LOCAL_GL_LUMINANCE:
                return WebGLTexelFormat::R8;
            case LOCAL_GL_LUMINANCE_ALPHA:
                return WebGLTexelFormat::RA8;
            default:
                NS_ASSERTION(PR_FALSE, "Coding mistake?! Should never reach this point.");
                return WebGLTexelFormat::Generic;
        }
    } else {
        switch (type) {
            case LOCAL_GL_UNSIGNED_SHORT_4_4_4_4:
                return WebGLTexelFormat::RGBA4444;
            case LOCAL_GL_UNSIGNED_SHORT_5_5_5_1:
                return WebGLTexelFormat::RGBA5551;
            case LOCAL_GL_UNSIGNED_SHORT_5_6_5:
                return WebGLTexelFormat::RGB565;
            default:
                NS_ASSERTION(PR_FALSE, "Coding mistake?! Should never reach this point.");
                return WebGLTexelFormat::Generic;
        }
    }
}
