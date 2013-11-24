/*
 * 
 * (C) 2013 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "FormatConverter.h"

#pragma warning(push)
#pragma warning(disable: 4005)
extern "C" {
	#include <ffmpeg/libavcodec/avcodec.h>
	#include <ffmpeg/libswscale/swscale.h>
	#include <ffmpeg/libavutil/pixdesc.h>
}
#pragma warning(pop)


static const SW_OUT_FMT s_sw_formats[] = {
	// YUV formats are grouped according to luma bit depth and sorted in descending order of quality.
	//  name     biCompression  subtype                                         av_pix_fmt    chroma_w chroma_h
	// YUV 8 bit
#if ENABLE_AYUV
	{_T("AYUV"),  FCC('AYUV'), &MEDIASUBTYPE_AYUV,  32, 4, 0, {1},     {1},     AV_PIX_FMT_YUV444P, 0, 0 }, // PixFmt_AYUV
#endif
	{_T("YUY2"),  FCC('YUY2'), &MEDIASUBTYPE_YUY2,  16, 2, 0, {1},     {1},     AV_PIX_FMT_YUYV422, 1, 0 }, // PixFmt_YUY2
	{_T("NV12"),  FCC('NV12'), &MEDIASUBTYPE_NV12,  12, 1, 2, {1,2},   {1,1},   AV_PIX_FMT_NV12,    1, 1 }, // PixFmt_NV12
	{_T("YV12"),  FCC('YV12'), &MEDIASUBTYPE_YV12,  12, 1, 3, {1,2,2}, {1,2,2}, AV_PIX_FMT_YUV420P, 1, 1 }, // PixFmt_YV12
	// YUV 10 bit
	// ...
	// RGB
	{_T("RGB32"), BI_RGB,      &MEDIASUBTYPE_RGB32, 32, 4, 0, {1},     {1},     AV_PIX_FMT_BGRA,    0, 0 }, // PixFmt_RGB32
	//
	// PS: AV_PIX_FMT_YUV444P not equal to AYUV, but is used as an intermediate format.
};

const SW_OUT_FMT* GetSWOF(int pixfmt)
{
	if (pixfmt < 0 && pixfmt >= PixFmt_count) {
		return NULL;
	}
	return &s_sw_formats[pixfmt];
}

MPCPixelFormat GetPixFormat(GUID& subtype)
{
	for (int i = 0; i < PixFmt_count; i++) {
		if (*s_sw_formats[i].subtype == subtype) {
			return (MPCPixelFormat)i;
		}
	}

	return PixFmt_None;
}

MPCPixelFormat GetPixFormat(AVPixelFormat av_pix_fmt)
{
	for (int i = 0; i < PixFmt_count; i++) {
		if (s_sw_formats[i].av_pix_fmt == av_pix_fmt) {
			return (MPCPixelFormat)i;
		}
	}

	return PixFmt_None;
}

MPCPixelFormat GetPixFormat(DWORD biCompression)
{
	for (int i = 0; i < PixFmt_count; i++) {
		if (s_sw_formats[i].biCompression == biCompression) {
			return (MPCPixelFormat)i;
		}
	}

	return PixFmt_None;
}

// CFormatConverter

CFormatConverter::CFormatConverter()
	: m_pSwsContext(NULL)
	, m_out_pixfmt(PixFmt_None)
	, m_swsFlags(SWS_BILINEAR | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INP)
	, m_colorspace(SWS_CS_DEFAULT)
	, m_dstRGBRange(0)
	, m_dstStride(0)
	, m_planeHeight(0)
	, m_nAlignedBufferSize(0)
	, m_pAlignedBuffer(NULL)
{
	ASSERT(PixFmt_count == _countof(s_sw_formats));

	m_FProps.avpixfmt	= AV_PIX_FMT_NONE;
	m_FProps.width		= 0;
	m_FProps.height		= 0;
	m_FProps.colorspace	= AVCOL_SPC_UNSPECIFIED;
	m_FProps.colorrange	= AVCOL_RANGE_UNSPECIFIED;
}

CFormatConverter::~CFormatConverter()
{
	Cleanup();
}

bool CFormatConverter::Init()
{
	Cleanup();
	if (m_FProps.avpixfmt == AV_PIX_FMT_NONE) {
		// check the input data, which can cause a crash.
		TRACE(_T("FormatConverter: incorrect source format\n"));
		return false;
	}

	const SW_OUT_FMT& swof = s_sw_formats[m_out_pixfmt];

	m_pSwsContext = sws_getCachedContext(
						NULL,
						m_FProps.width,
						m_FProps.height,
						m_FProps.avpixfmt,
						m_FProps.width,
						m_FProps.height,
						swof.av_pix_fmt,
						m_swsFlags | SWS_PRINT_INFO,
						NULL,
						NULL,
						NULL);

	if (m_pSwsContext == NULL) {
		TRACE(_T("FormatConverter: sws_getCachedContext failed\n"));
		return false;
	}

	return true;
}

void  CFormatConverter::UpdateDetails()
{
	if (m_pSwsContext) {
		int *inv_tbl = NULL, *tbl = NULL;
		int srcRange, dstRange, brightness, contrast, saturation;
		int ret = sws_getColorspaceDetails(m_pSwsContext, &inv_tbl, &srcRange, &tbl, &dstRange, &brightness, &contrast, &saturation);
		if (ret >= 0) {
			if (m_out_pixfmt == PixFmt_RGB32 && !(av_pix_fmt_desc_get(m_FProps.avpixfmt)->flags & AV_PIX_FMT_FLAG_RGB)) {
				dstRange = m_dstRGBRange;
			}
			ret = sws_setColorspaceDetails(m_pSwsContext, sws_getCoefficients(m_colorspace), srcRange, tbl, dstRange, brightness, contrast, saturation);
		}
	}
}

#if ENABLE_AYUV
HRESULT CFormatConverter::ConvertToAYUV(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[])
{
  const BYTE *y = NULL;
  const BYTE *u = NULL;
  const BYTE *v = NULL;
  int line, i = 0;
  int sourceStride = 0;
  BYTE *pTmpBuffer = NULL;

  if (m_FProps.avpixfmt != AV_PIX_FMT_YUV444P) {
    uint8_t *tmp[4] = {NULL};
    int     tmpStride[4] = {0};
    int scaleStride = FFALIGN(width, 32);

    pTmpBuffer = (BYTE *)av_malloc(height * scaleStride * 3);

    tmp[0] = pTmpBuffer;
    tmp[1] = tmp[0] + (height * scaleStride);
    tmp[2] = tmp[1] + (height * scaleStride);
    tmp[3] = NULL;
    tmpStride[0] = scaleStride;
    tmpStride[1] = scaleStride;
    tmpStride[2] = scaleStride;
    tmpStride[3] = 0;

    sws_scale(m_pSwsContext, src, srcStride, 0, height, tmp, tmpStride);

    y = tmp[0];
    u = tmp[1];
    v = tmp[2];
    sourceStride = scaleStride;
  } else {
    y = src[0];
    u = src[1];
    v = src[2];
    sourceStride = srcStride[0];
  }

#define YUV444_PACK_AYUV(offset) *idst++ = v[i+offset] | (u[i+offset] << 8) | (y[i+offset] << 16) | (0xff << 24);

  BYTE *out = dst[0];
  for (line = 0; line < height; ++line) {
    int32_t *idst = (int32_t *)out;
    for (i = 0; i < (width-7); i+=8) {
      YUV444_PACK_AYUV(0)
      YUV444_PACK_AYUV(1)
      YUV444_PACK_AYUV(2)
      YUV444_PACK_AYUV(3)
      YUV444_PACK_AYUV(4)
      YUV444_PACK_AYUV(5)
      YUV444_PACK_AYUV(6)
      YUV444_PACK_AYUV(7)
    }
    for (; i < width; ++i) {
      YUV444_PACK_AYUV(0)
    }
    y += sourceStride;
    u += sourceStride;
    v += sourceStride;
    out += dstStride[0];
  }

  av_freep(&pTmpBuffer);

  return S_OK;
}
#endif

void CFormatConverter::UpdateOutput(MPCPixelFormat out_pixfmt, int dstStride, int planeHeight)
{
	if (out_pixfmt != m_out_pixfmt) {
		m_out_pixfmt = out_pixfmt;
		Cleanup();
	}

	m_dstStride   = dstStride;
	m_planeHeight = planeHeight;
}

void CFormatConverter::UpdateOutput2(DWORD biCompression, LONG biWidth, LONG biHeight)
{
	UpdateOutput(GetPixFormat(biCompression), biWidth, abs(biHeight));
}

void CFormatConverter::SetOptions(int preset, int standard, int rgblevels)
{
	switch (standard) {
	case 0  : // SD(BT.601)
		m_autocolorspace = false;
		m_colorspace = SWS_CS_ITU601;
		break;
	case 1  : // HD(BT.709)
		m_autocolorspace = false;
		m_colorspace = SWS_CS_ITU709;
		break;
	case 2  : // Auto
	default :
		m_autocolorspace = true;
		m_colorspace = m_FProps.width > 768 ? SWS_CS_ITU709 : SWS_CS_ITU601;
		break;
	}

	m_dstRGBRange = rgblevels == 1 ? 0 : 1;

	int swsFlags = 0;
	switch (preset) {
	case 0  : // "Fastest"
		swsFlags = SWS_FAST_BILINEAR | SWS_ACCURATE_RND;
		// SWS_FAST_BILINEAR or SWS_POINT disable dither and enable low-quality yv12_to_yuy2 conversion.
		// any interpolation type has no effect.
		break;
	case 1  : // "Fast"
		swsFlags = SWS_BILINEAR | SWS_ACCURATE_RND;
		break;
	case 2  :// "Normal"
	default :
		swsFlags = SWS_BILINEAR | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INP;
		break;
	case 3  : // "Full"
		swsFlags = SWS_BILINEAR | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INP | SWS_FULL_CHR_H_INT;
		break;
	}

	if (swsFlags != m_swsFlags) {
		m_swsFlags = swsFlags;
		Cleanup();
	} else {
		UpdateDetails();
	}
}

int CFormatConverter::Converting(BYTE* dst, AVFrame* pFrame)
{
	if (!m_pSwsContext || pFrame->format != m_FProps.avpixfmt || pFrame->width != m_FProps.width || pFrame->height != m_FProps.height) {
		// update the basic properties
		m_FProps.avpixfmt	= (AVPixelFormat)pFrame->format;
		m_FProps.width		= pFrame->width;
		m_FProps.height		= pFrame->height;
		if (!Init()) {
			TRACE(_T("FormatConverter: Init() failed\n"));
			return 0;
		}
		// update the additional properties (updated only when changing basic properties.)
		m_FProps.colorspace	= pFrame->colorspace;
		m_FProps.colorrange	= pFrame->color_range;
		UpdateDetails();
	}

	const SW_OUT_FMT& swof = s_sw_formats[m_out_pixfmt];

	// From LAVVideo...
	uint8_t *out = dst;
	int outStride = m_dstStride;
	// Check if we have proper pixel alignment and the dst memory is actually aligned
	if (FFALIGN(m_dstStride, 16) != m_dstStride || ((uintptr_t)dst % 16u)) {
		outStride = FFALIGN(outStride, 16);
		size_t requiredSize = (outStride * m_planeHeight * swof.bpp) << 3;
		if (requiredSize > m_nAlignedBufferSize) {
			av_freep(&m_pAlignedBuffer);
			m_nAlignedBufferSize = requiredSize;
			m_pAlignedBuffer = (uint8_t*)av_malloc(m_nAlignedBufferSize + FF_INPUT_BUFFER_PADDING_SIZE);
		}
		out = m_pAlignedBuffer;
	}

	uint8_t*	dstArray[4]			= {NULL};
	int			dstStrideArray[4]	= {0};
	int			byteStride			= outStride * swof.codedbytes;

	dstArray[0] = out;
	dstStrideArray[0] = byteStride;
	for (int i = 1; i < swof.planes; ++i) {
		dstArray[i] = dstArray[i-1] + dstStrideArray[i-1] * (m_planeHeight / swof.planeWidth[i-1]);
		dstStrideArray[i] = byteStride / swof.planeWidth[i];
	}

	if (m_out_pixfmt == PixFmt_YV12) {
		std::swap(dstArray[1], dstArray[2]);
	}

	switch (m_out_pixfmt) {
#if ENABLE_AYUV
	case PixFmt_AYUV:
		ConvertToAYUV(pFrame->data, pFrame->linesize, dstArray, m_FProps.width, m_FProps.height, dstStrideArray);
		break;
#endif
	default:
		int ret = sws_scale(m_pSwsContext, pFrame->data, pFrame->linesize, 0, m_FProps.height, dstArray, dstStrideArray);
	}

	if (out != dst) {
		int line = 0;

		// Copy first plane
		const int widthBytes = m_FProps.width * swof.codedbytes;
		const int srcStrideBytes = outStride * swof.codedbytes;
		const int dstStrideBytes = m_dstStride * swof.codedbytes;
		for (line = 0; line < m_FProps.height; ++line) {
			memcpy(dst, out, widthBytes);
			out += srcStrideBytes;
			dst += dstStrideBytes;
		}
		dst += (m_planeHeight - m_FProps.height) * dstStrideBytes;
		
		for (int plane = 1; plane < swof.planes; ++plane) {
			const int planeWidth        = widthBytes      / swof.planeWidth[plane];
			const int activePlaneHeight = m_FProps.height / swof.planeHeight[plane];
			const int totalPlaneHeight  = m_planeHeight   / swof.planeHeight[plane];
			const int srcPlaneStride    = srcStrideBytes  / swof.planeWidth[plane];
			const int dstPlaneStride    = dstStrideBytes  / swof.planeWidth[plane];
			for (line = 0; line < activePlaneHeight; ++line) {
				memcpy(dst, out, planeWidth);
				out += srcPlaneStride;
				dst += dstPlaneStride;
			}
			dst+= (totalPlaneHeight - activePlaneHeight) * dstPlaneStride;
		}
	}

	return 0;
}

void CFormatConverter::Cleanup()
{
	if (m_pSwsContext) {
		sws_freeContext(m_pSwsContext);
		m_pSwsContext	= NULL;
	}
	if (m_pAlignedBuffer) {
		av_freep(&m_pAlignedBuffer);
	}
}
