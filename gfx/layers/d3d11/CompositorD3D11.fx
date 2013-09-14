/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

typedef float4 rect;

float4x4 mLayerTransform : register(vs, c0);
float4x4 mProjection : register(vs, c4);
float4 vRenderTargetOffset : register(vs, c8);
rect vTextureCoords : register(vs, c9);
rect vLayerQuad : register(vs, c10);
rect vMaskQuad : register(vs, c11);

float4 fLayerColor : register(ps, c0);
float fLayerOpacity : register(ps, c1);

sampler sSampler : register(ps, s0);

BlendState NoBlendDual
{
  AlphaToCoverageEnable = FALSE;
  BlendEnable[0] = FALSE;
  BlendEnable[1] = FALSE;
  RenderTargetWriteMask[0] = 0x0F; // All
  RenderTargetWriteMask[1] = 0x0F; // All
};

BlendState ComponentAlphaBlend
{
  AlphaToCoverageEnable = FALSE;
  BlendEnable[0] = TRUE;
  SrcBlend = One;
  DestBlend = Inv_Src1_Color;
  BlendOp = Add;
  SrcBlendAlpha = One;
  DestBlendAlpha = Inv_Src_Alpha;
  BlendOpAlpha = Add;
  RenderTargetWriteMask[0] = 0x0F; // All
};

Texture2D tRGB;
Texture2D tY;
Texture2D tCb;
Texture2D tCr;
Texture2D tRGBWhite;
// Always bind this to slot 3 since this is always available!
Texture2D tMask : register(ps, t3);

struct VS_INPUT {
  float2 vPosition : POSITION;
};

struct VS_OUTPUT {
  float4 vPosition : SV_Position;
  float2 vTexCoords : TEXCOORD0;
};

struct VS_MASK_OUTPUT {
  float4 vPosition : SV_Position;
  float2 vTexCoords : TEXCOORD0;
  float2 vMaskCoords : TEXCOORD1;
};

struct VS_MASK_3D_OUTPUT {
  float4 vPosition : SV_Position;
  float2 vTexCoords : TEXCOORD0;
  float3 vMaskCoords : TEXCOORD1;
};

struct PS_OUTPUT {
  float4 vSrc;
  float4 vAlpha;
};

struct PS_DUAL_OUTPUT {
  float4 vOutput1 : SV_Target0;
  float4 vOutput2 : SV_Target1;
};

float2 TexCoords(const float2 aPosition)
{
  float2 result;
  const float2 size = vTextureCoords.zw;
  result.x = vTextureCoords.x + aPosition.x * size.x;
  result.y = vTextureCoords.y + aPosition.y * size.y;

  return result;
}

SamplerState LayerTextureSamplerLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

float4 TransformedPosition(float2 aInPosition)
{
  // the current vertex's position on the quad
  float4 position = float4(0, 0, 0, 1);

  // We use 4 component floats to uniquely describe a rectangle, by the structure
  // of x, y, width, height. This allows us to easily generate the 4 corners
  // of any rectangle from the 4 corners of the 0,0-1,1 quad that we use as the
  // stream source for our LayerQuad vertex shader. We do this by doing:
  // Xout = x + Xin * width
  // Yout = y + Yin * height
  float2 size = vLayerQuad.zw;
  position.x = vLayerQuad.x + aInPosition.x * size.x;
  position.y = vLayerQuad.y + aInPosition.y * size.y;

  position = mul(mLayerTransform, position);

  return position;
}

float4 VertexPosition(float4 aTransformedPosition)
{
  float4 result;
  result.w = aTransformedPosition.w;
  result.xyz = aTransformedPosition.xyz / aTransformedPosition.w;
  result -= vRenderTargetOffset;
  result.xyz *= result.w;

  result = mul(mProjection, result);

  return result;
}

VS_OUTPUT LayerQuadVS(const VS_INPUT aVertex)
{
  VS_OUTPUT outp;
  float4 position = TransformedPosition(aVertex.vPosition);

  outp.vPosition = VertexPosition(position);
  outp.vTexCoords = TexCoords(aVertex.vPosition.xy);

  return outp;
}

VS_MASK_OUTPUT LayerQuadMaskVS(const VS_INPUT aVertex)
{
  VS_MASK_OUTPUT outp;
  float4 position = TransformedPosition(aVertex.vPosition);

  outp.vPosition = VertexPosition(position);

  // calculate the position on the mask texture
  outp.vMaskCoords.x = (position.x - vMaskQuad.x) / vMaskQuad.z;
  outp.vMaskCoords.y = (position.y - vMaskQuad.y) / vMaskQuad.w;

  outp.vTexCoords = TexCoords(aVertex.vPosition.xy);

  return outp;
}

VS_MASK_3D_OUTPUT LayerQuadMask3DVS(const VS_INPUT aVertex)
{
  VS_MASK_3D_OUTPUT outp;
  float4 position = TransformedPosition(aVertex.vPosition);

  outp.vPosition = VertexPosition(position);

  // calculate the position on the mask texture
  position.xyz /= position.w;
  outp.vMaskCoords.x = (position.x - vMaskQuad.x) / vMaskQuad.z;
  outp.vMaskCoords.y = (position.y - vMaskQuad.y) / vMaskQuad.w;
  // We use the w coord to do non-perspective correct interpolation:
  // the quad might be transformed in 3D, in which case it will have some
  // perspective. The graphics card will do perspective-correct interpolation
  // of the texture, but our mask is already transformed and so we require
  // linear interpolation. Therefore, we must correct the interpolation
  // ourselves, we do this by multiplying all coords by w here, and dividing by
  // w in the pixel shader (post-interpolation), we pass w in outp.vMaskCoords.z.
  // See http://en.wikipedia.org/wiki/Texture_mapping#Perspective_correctness
  outp.vMaskCoords.z = 1;
  outp.vMaskCoords *= position.w;

  outp.vTexCoords = TexCoords(aVertex.vPosition.xy);

  return outp;
}

float4 RGBAShaderMask(const VS_MASK_OUTPUT aVertex) : SV_Target
{
  float2 maskCoords = aVertex.vMaskCoords;
  float mask = tMask.Sample(sSampler, maskCoords).a;
  return tRGB.Sample(sSampler, aVertex.vTexCoords) * fLayerOpacity * mask;
}

float4 RGBAShaderMask3D(const VS_MASK_3D_OUTPUT aVertex) : SV_Target
{
  float2 maskCoords = aVertex.vMaskCoords.xy / aVertex.vMaskCoords.z;
  float mask = tMask.Sample(LayerTextureSamplerLinear, maskCoords).a;
  return tRGB.Sample(sSampler, aVertex.vTexCoords) * fLayerOpacity * mask;
}

float4 RGBShaderMask(const VS_MASK_OUTPUT aVertex) : SV_Target
{
  float4 result;
  result = tRGB.Sample(sSampler, aVertex.vTexCoords) * fLayerOpacity;
  result.a = fLayerOpacity;

  float2 maskCoords = aVertex.vMaskCoords;
  float mask = tMask.Sample(sSampler, maskCoords).a;
  return result * mask;
}

float4 CalculateYCbCrColor(const float2 aTexCoords)
{
  float4 yuv;
  float4 color;

  yuv.r = tCr.Sample(sSampler, aTexCoords).r - 0.5;
  yuv.g = tY.Sample(sSampler, aTexCoords).r - 0.0625;
  yuv.b = tCb.Sample(sSampler, aTexCoords).r - 0.5;

  color.r = yuv.g * 1.164 + yuv.r * 1.596;
  color.g = yuv.g * 1.164 - 0.813 * yuv.r - 0.391 * yuv.b;
  color.b = yuv.g * 1.164 + yuv.b * 2.018;
  color.a = 1.0f;

  return color;
}

float4 YCbCrShaderMask(const VS_MASK_OUTPUT aVertex) : SV_Target
{
  float2 maskCoords = aVertex.vMaskCoords;
  float mask = tMask.Sample(sSampler, maskCoords).a;

  return CalculateYCbCrColor(aVertex.vTexCoords) * fLayerOpacity * mask;
}

PS_OUTPUT ComponentAlphaShaderMask(const VS_MASK_OUTPUT aVertex) : SV_Target
{
  PS_OUTPUT result;

  result.vSrc = tRGB.Sample(sSampler, aVertex.vTexCoords);
  result.vAlpha = 1.0 - tRGBWhite.Sample(sSampler, aVertex.vTexCoords) + result.vSrc;
  result.vSrc.a = result.vAlpha.g;

  float2 maskCoords = aVertex.vMaskCoords;
  float mask = tMask.Sample(sSampler, maskCoords).a;
  result.vSrc *= fLayerOpacity * mask;
  result.vAlpha *= fLayerOpacity * mask;

  return result;
}

float4 SolidColorShaderMask(const VS_MASK_OUTPUT aVertex) : SV_Target
{
  float2 maskCoords = aVertex.vMaskCoords;
  float mask = tMask.Sample(sSampler, maskCoords).a;
  return fLayerColor * mask;
}

/*
 *  Un-masked versions
 *************************************************************
 */
float4 RGBAShader(const VS_OUTPUT aVertex) : SV_Target
{
  return tRGB.Sample(sSampler, aVertex.vTexCoords) * fLayerOpacity;
}

float4 RGBShader(const VS_OUTPUT aVertex) : SV_Target
{
  float4 result;
  result = tRGB.Sample(sSampler, aVertex.vTexCoords) * fLayerOpacity;
  result.a = fLayerOpacity;
  return result;
}

float4 YCbCrShader(const VS_OUTPUT aVertex) : SV_Target
{
  return CalculateYCbCrColor(aVertex.vTexCoords) * fLayerOpacity;
}

PS_OUTPUT ComponentAlphaShader(const VS_OUTPUT aVertex) : SV_Target
{
  PS_OUTPUT result;

  result.vSrc = tRGB.Sample(sSampler, aVertex.vTexCoords);
  result.vAlpha = 1.0 - tRGBWhite.Sample(sSampler, aVertex.vTexCoords) + result.vSrc;
  result.vSrc.a = result.vAlpha.g;
  result.vSrc *= fLayerOpacity;
  result.vAlpha *= fLayerOpacity;
  return result;
}

float4 SolidColorShader(const VS_OUTPUT aVertex) : SV_Target
{
  return fLayerColor;
}

PS_DUAL_OUTPUT AlphaExtractionPrepareShader(const VS_OUTPUT aVertex)
{
  PS_DUAL_OUTPUT result;
  result.vOutput1 = float4(0, 0, 0, 1);
  result.vOutput2 = float4(1, 1, 1, 1);
  return result;
}
