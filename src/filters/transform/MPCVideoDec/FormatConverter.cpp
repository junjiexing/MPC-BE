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
#include "CpuId.h"
#include "pixconv_sse2_templates.h"
#include <moreuuids.h>

#pragma warning(push)
#pragma warning(disable: 4005)
extern "C" {
	#include <ffmpeg/libavcodec/avcodec.h>
	#include <ffmpeg/libswscale/swscale.h>
	#include <ffmpeg/libavutil/pixdesc.h>
	#include <ffmpeg/libavutil/intreadwrite.h>
}
#pragma warning(pop)

static const SW_OUT_FMT s_sw_formats[] = {
	//  name     biCompression  subtype                                         av_pix_fmt    chroma_w chroma_h
	// YUV 8 bit
	{_T("NV12"),  FCC('NV12'), &MEDIASUBTYPE_NV12,  12, 1, 2, {1,2},   {1,1},   AV_PIX_FMT_NV12,        1, 1, 12,  8}, // PixFmt_NV12
	{_T("YV12"),  FCC('YV12'), &MEDIASUBTYPE_YV12,  12, 1, 3, {1,2,2}, {1,2,2}, AV_PIX_FMT_YUV420P,     1, 1, 12,  8}, // PixFmt_YV12
	{_T("YUY2"),  FCC('YUY2'), &MEDIASUBTYPE_YUY2,  16, 2, 0, {1},     {1},     AV_PIX_FMT_YUYV422,     1, 0, 16,  8}, // PixFmt_YUY2
	{_T("AYUV"),  FCC('AYUV'), &MEDIASUBTYPE_AYUV,  32, 4, 0, {1},     {1},     AV_PIX_FMT_YUV444P,     0, 0, 24,  8}, // PixFmt_AYUV
	{_T("YV24"),  FCC('YV24'), &MEDIASUBTYPE_YV24,  24, 1, 3, {1,1,1}, {1,1,1}, AV_PIX_FMT_YUV444P,     0, 0, 24,  8}, // PixFmt_YV24
	// YUV 10 bit
	{_T("P010"),  FCC('P010'), &MEDIASUBTYPE_P010,  24, 2, 2, {1,2},   {1,1},   AV_PIX_FMT_YUV420P16LE, 1, 1, 15, 10}, // PixFmt_P010
	{_T("P210"),  FCC('P210'), &MEDIASUBTYPE_P210,  32, 2, 2, {1,1},   {1,1},   AV_PIX_FMT_YUV422P16LE, 1, 0, 20, 10}, // PixFmt_P210
	{_T("Y410"),  FCC('Y410'), &MEDIASUBTYPE_Y410,  32, 4, 0, {1},     {1},     AV_PIX_FMT_YUV444P10LE, 0, 0, 30, 10}, // PixFmt_Y410
	// YUV 16 bit
	{_T("P016"),  FCC('P016'), &MEDIASUBTYPE_P016,  24, 2, 2, {1,2},   {1,1},   AV_PIX_FMT_YUV420P16LE, 1, 1, 24, 16}, // PixFmt_P016
	{_T("P216"),  FCC('P216'), &MEDIASUBTYPE_P216,  32, 2, 2, {1,1},   {1,1},   AV_PIX_FMT_YUV422P16LE, 1, 0, 32, 16}, // PixFmt_P216
	{_T("Y416"),  FCC('Y416'), &MEDIASUBTYPE_Y416,  64, 8, 0, {1},     {1},     AV_PIX_FMT_YUV444P16LE, 0, 0, 48, 16}, // PixFmt_Y416
	// RGB
	{_T("RGB32"), BI_RGB,      &MEDIASUBTYPE_RGB32, 32, 4, 0, {1},     {1},     AV_PIX_FMT_BGRA,        0, 0, 24,  8}, // PixFmt_RGB32
	// PS:
	// AV_PIX_FMT_YUV444P not equal to AYUV, but is used as an intermediate format.
	// AV_PIX_FMT_YUV420P16LE not equal to P010, but is used as an intermediate format.
	// AV_PIX_FMT_YUV422P16LE not equal to P210, but is used as an intermediate format.
};

const SW_OUT_FMT* GetSWOF(int pixfmt)
{
	if (pixfmt < 0 && pixfmt >= PixFmt_count) {
		return NULL;
	}
	return &s_sw_formats[pixfmt];
}

LPCTSTR GetChromaSubsamplingStr(AVPixelFormat av_pix_fmt)
{
	int h_shift, v_shift;

	if (0 == av_pix_fmt_get_chroma_sub_sample(av_pix_fmt, &h_shift, &v_shift)) {
		if (h_shift == 0 && v_shift == 0) {
			return _T("4:4:4");
		} else if (h_shift == 0 && v_shift == 1) {
			return _T("4:4:0");
		} else if (h_shift == 1 && v_shift == 0) {
			return _T("4:2:2");
		} else if (h_shift == 1 && v_shift == 1) {
			return _T("4:2:0");
		} else if (h_shift == 2 && v_shift == 0) {
			return _T("4:1:1");
		} else if (h_shift == 2 && v_shift == 2) {
			return _T("4:1:0");
		}
	}

	return _T("");
}

int GetLumaBits(AVPixelFormat av_pix_fmt)
{
	const AVPixFmtDescriptor* pfdesc = av_pix_fmt_desc_get(av_pix_fmt);

	return (pfdesc ? pfdesc->comp->depth_minus1 + 1 : 0);
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

MPCPixFmtType GetPixFmtType(AVPixelFormat av_pix_fmt)
{
	const AVPixFmtDescriptor* pfdesc = av_pix_fmt_desc_get(av_pix_fmt);
	int lumabits = pfdesc->comp->depth_minus1 + 1;

	if (pfdesc->flags & (AV_PIX_FMT_FLAG_RGB|AV_PIX_FMT_FLAG_PAL)) {
		return PFType_RGB;
	}

	if (lumabits <= 8 || lumabits > 16 || pfdesc->nb_components != 3 + (pfdesc->flags & AV_PIX_FMT_FLAG_ALPHA ? 1 : 0)) {
		return PFType_unspecified;
	}

	if ((pfdesc->flags & ~AV_PIX_FMT_FLAG_ALPHA) == AV_PIX_FMT_FLAG_PLANAR) {
		// must be planar type, ignore alpha channel, other flags are forbidden

		if (pfdesc->log2_chroma_w == 1 && pfdesc->log2_chroma_h == 1) {
			return PFType_YUV420Px;
		}

		if (pfdesc->log2_chroma_w == 1 && pfdesc->log2_chroma_h == 0) {
			return PFType_YUV422Px;
		}

		if (pfdesc->log2_chroma_w == 0 && pfdesc->log2_chroma_h == 0) {
			return PFType_YUV444Px;
		}
	}

	return PFType_unspecified;
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
	, m_nCPUFlag(0)
{
	ASSERT(PixFmt_count == _countof(s_sw_formats));

	m_FProps.avpixfmt	= AV_PIX_FMT_NONE;
	m_FProps.width		= 0;
	m_FProps.height		= 0;
	m_FProps.lumabits	= 0;
	m_FProps.pftype		= PFType_unspecified;
	m_FProps.colorspace	= AVCOL_SPC_UNSPECIFIED;
	m_FProps.colorrange	= AVCOL_RANGE_UNSPECIFIED;

	CCpuId cpuId;
	m_nCPUFlag = cpuId.GetFeatures();

	pConvertFn			= &CFormatConverter::ConvertGeneric;
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

	SetConvertFunc();

	return true;
}

void  CFormatConverter::UpdateDetails()
{
	if (m_pSwsContext) {
		int *inv_tbl = NULL, *tbl = NULL;
		int srcRange, dstRange, brightness, contrast, saturation;
		int ret = sws_getColorspaceDetails(m_pSwsContext, &inv_tbl, &srcRange, &tbl, &dstRange, &brightness, &contrast, &saturation);
		if (ret >= 0) {
			if (m_out_pixfmt == PixFmt_RGB32 && !(av_pix_fmt_desc_get(m_FProps.avpixfmt)->flags & (AV_PIX_FMT_FLAG_RGB|AV_PIX_FMT_FLAG_PAL))) {
				dstRange = m_dstRGBRange;
			}
			ret = sws_setColorspaceDetails(m_pSwsContext, sws_getCoefficients(m_colorspace), srcRange, tbl, dstRange, brightness, contrast, saturation);
		}
	}
}

void CFormatConverter::SetConvertFunc()
{
	pConvertFn = &CFormatConverter::ConvertGeneric;

	if (m_nCPUFlag & CCpuId::MPC_MM_SSE2) {
		switch (m_out_pixfmt) {
		case PixFmt_AYUV:
			if (m_FProps.pftype == PFType_YUV444Px) {
				pConvertFn = &CFormatConverter::convert_yuv444_ayuv_dither_le;
			}
			break;
		case PixFmt_P010:
		case PixFmt_P016:
			if (m_FProps.pftype == PFType_YUV420Px) {
				pConvertFn = &CFormatConverter::convert_yuv420_px1x_le;
			}
			break;
		case PixFmt_Y410:
			if (m_FProps.pftype == PFType_YUV444Px && m_FProps.lumabits <= 10) {
				pConvertFn = &CFormatConverter::convert_yuv444_y410;
			}
			break;
		case PixFmt_P210:
		case PixFmt_P216:
			if (m_FProps.pftype == PFType_YUV422Px) {
				pConvertFn = &CFormatConverter::convert_yuv420_px1x_le;
			}
			break;
		case PixFmt_YUY2:
			if (m_FProps.pftype == PFType_YUV422Px) {
				pConvertFn = &CFormatConverter::convert_yuv422_yuy2_uyvy_dither_le;
				break;
			}
		}
	}
}

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

HRESULT CFormatConverter::ConvertToPX1X(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[], int chromaVertical)
{
  const BYTE *y = NULL;
  const BYTE *u = NULL;
  const BYTE *v = NULL;
  int line, i = 0;
  int sourceStride = 0;

  int shift = 0;

  BYTE *pTmpBuffer = NULL;

  if ((m_FProps.pftype != PFType_YUV422Px && chromaVertical == 1) || (m_FProps.pftype != PFType_YUV420Px && chromaVertical == 2)) {
    uint8_t *tmp[4] = {NULL};
    int     tmpStride[4] = {0};
    int scaleStride = FFALIGN(width, 32) * 2;

    pTmpBuffer = (BYTE *)av_malloc(height * scaleStride * 2);

    tmp[0] = pTmpBuffer;
    tmp[1] = tmp[0] + (height * scaleStride);
    tmp[2] = tmp[1] + ((height / chromaVertical) * (scaleStride / 2));
    tmp[3] = NULL;
    tmpStride[0] = scaleStride;
    tmpStride[1] = scaleStride / 2;
    tmpStride[2] = scaleStride / 2;
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

    shift = (16 - m_FProps.lumabits);
  }

  // copy Y
  BYTE *pLineOut = dst[0];
  const BYTE *pLineIn = y;
  for (line = 0; line < height; ++line) {
    if (shift == 0) {
      memcpy(pLineOut, pLineIn, width * 2);
    } else {
      const int16_t *yc = (int16_t *)pLineIn;
      int16_t *idst = (int16_t *)pLineOut;
      for (i = 0; i < width; ++i) {
        int32_t yv = AV_RL16(yc+i);
        if (shift) yv <<= shift;
        *idst++ = yv;
      }
    }
    pLineOut += dstStride[0];
    pLineIn += sourceStride;
  }

  sourceStride >>= 2;

  // Merge U/V
  BYTE *out = dst[1];
  const int16_t *uc = (int16_t *)u;
  const int16_t *vc = (int16_t *)v;
  for (line = 0; line < height/chromaVertical; ++line) {
    int32_t *idst = (int32_t *)out;
    for (i = 0; i < width/2; ++i) {
      int32_t uv = AV_RL16(uc+i);
      int32_t vv = AV_RL16(vc+i);
      if (shift) {
        uv <<= shift;
        vv <<= shift;
      }
      *idst++ = uv | (vv << 16);
    }
    uc += sourceStride;
    vc += sourceStride;
    out += dstStride[1];
  }

  av_freep(&pTmpBuffer);

  return S_OK;
}

#define YUV444_PACKED_LOOP_HEAD(width, height, y, u, v, out) \
  for (int line = 0; line < height; ++line) { \
    int32_t *idst = (int32_t *)out; \
    for(int i = 0; i < width; ++i) { \
      int32_t yv, uv, vv;

#define YUV444_PACKED_LOOP_HEAD_LE(width, height, y, u, v, out) \
  YUV444_PACKED_LOOP_HEAD(width, height, y, u, v, out) \
    yv = AV_RL16(y+i); uv = AV_RL16(u+i); vv = AV_RL16(v+i);

#define YUV444_PACKED_LOOP_END(y, u, v, out, srcStride, dstStride) \
    } \
    y += srcStride; \
    u += srcStride; \
    v += srcStride; \
    out += dstStride; \
  }

HRESULT CFormatConverter::ConvertToY410(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[])
{
  const int16_t *y = NULL;
  const int16_t *u = NULL;
  const int16_t *v = NULL;
  int sourceStride = 0;
  bool b9Bit = false;

  BYTE *pTmpBuffer = NULL;

  if (m_FProps.pftype != PFType_YUV444Px || m_FProps.lumabits > 10) {
    uint8_t *tmp[4] = {NULL};
    int     tmpStride[4] = {0};
    int scaleStride = FFALIGN(width, 32);

    pTmpBuffer = (BYTE *)av_malloc(height * scaleStride * 6);

    tmp[0] = pTmpBuffer;
    tmp[1] = tmp[0] + (height * scaleStride * 2);
    tmp[2] = tmp[1] + (height * scaleStride * 2);
    tmp[3] = NULL;
    tmpStride[0] = scaleStride * 2;
    tmpStride[1] = scaleStride * 2;
    tmpStride[2] = scaleStride * 2;
    tmpStride[3] = 0;

    sws_scale(m_pSwsContext, src, srcStride, 0, height, tmp, tmpStride);

    y = (int16_t *)tmp[0];
    u = (int16_t *)tmp[1];
    v = (int16_t *)tmp[2];
    sourceStride = scaleStride;
  } else {
    y = (int16_t *)src[0];
    u = (int16_t *)src[1];
    v = (int16_t *)src[2];
    sourceStride = srcStride[0] / 2;

    b9Bit = (m_FProps.lumabits == 9);
  }

#define YUV444_Y410_PACK \
  *idst++ = (uv & 0x3FF) | ((yv & 0x3FF) << 10) | ((vv & 0x3FF) << 20) | (3 << 30);

  BYTE *out = dst[0];
  YUV444_PACKED_LOOP_HEAD_LE(width, height, y, u, v, out)
    if (b9Bit) {
      yv <<= 1;
      uv <<= 1;
      vv <<= 1;
    }
    YUV444_Y410_PACK
  YUV444_PACKED_LOOP_END(y, u, v, out, sourceStride, dstStride[0])

  av_freep(&pTmpBuffer);

  return S_OK;
}

HRESULT CFormatConverter::ConvertToY416(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[])
{
  const int16_t *y = NULL;
  const int16_t *u = NULL;
  const int16_t *v = NULL;
  int sourceStride = 0;

  BYTE *pTmpBuffer = NULL;

  if (m_FProps.pftype != PFType_YUV444Px || m_FProps.lumabits != 16) {
    uint8_t *tmp[4] = {NULL};
    int     tmpStride[4] = {0};
    int scaleStride = FFALIGN(width, 32);

    pTmpBuffer = (BYTE *)av_malloc(height * scaleStride * 6);

    tmp[0] = pTmpBuffer;
    tmp[1] = tmp[0] + (height * scaleStride * 2);
    tmp[2] = tmp[1] + (height * scaleStride * 2);
    tmp[3] = NULL;
    tmpStride[0] = scaleStride * 2;
    tmpStride[1] = scaleStride * 2;
    tmpStride[2] = scaleStride * 2;
    tmpStride[3] = 0;

    sws_scale(m_pSwsContext, src, srcStride, 0, height, tmp, tmpStride);

    y = (int16_t *)tmp[0];
    u = (int16_t *)tmp[1];
    v = (int16_t *)tmp[2];
    sourceStride = scaleStride;
  } else {
    y = (int16_t *)src[0];
    u = (int16_t *)src[1];
    v = (int16_t *)src[2];
    sourceStride = srcStride[0] / 2;
  }

#define YUV444_Y416_PACK \
  *idst++ = 0xFFFF | (vv << 16); \
  *idst++ = yv | (uv << 16);

  BYTE *out = dst[0];
  YUV444_PACKED_LOOP_HEAD_LE(width, height, y, u, v, out)
    YUV444_Y416_PACK
  YUV444_PACKED_LOOP_END(y, u, v, out, sourceStride, dstStride[0])

  av_freep(&pTmpBuffer);

  return S_OK;
}

HRESULT CFormatConverter::ConvertGeneric(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[])
{
	switch (m_out_pixfmt) {
	case PixFmt_AYUV:
		ConvertToAYUV(src, srcStride, dst, width, height, dstStride);
		break;
	case PixFmt_P010:
	case PixFmt_P016:
		ConvertToPX1X(src, srcStride, dst, width, height, dstStride, 2);
		break;
	case PixFmt_Y410:
		ConvertToY410(src, srcStride, dst, width, height, dstStride);
		break;
	case PixFmt_P210:
	case PixFmt_P216:
		ConvertToPX1X(src, srcStride, dst, width, height, dstStride, 1);
		break;
	case PixFmt_Y416:
		ConvertToY416(src, srcStride, dst, width, height, dstStride);
		break;
	default:
		int ret = sws_scale(m_pSwsContext, src, srcStride, 0, height, dst, dstStride);
	}

	return S_OK;
}

HRESULT CFormatConverter::convert_yuv444_y410(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[])
{
  const uint16_t *y = (const uint16_t *)src[0];
  const uint16_t *u = (const uint16_t *)src[1];
  const uint16_t *v = (const uint16_t *)src[2];

  const ptrdiff_t inStride = srcStride[0] >> 1;
  const ptrdiff_t outStride = dstStride[0];
  int shift = 10 - m_FProps.lumabits;

  ptrdiff_t line, i;

  __m128i xmm0,xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7;

  xmm7 = _mm_set1_epi32(0xC0000000);
  xmm6 = _mm_setzero_si128();

  _mm_sfence();

  for (line = 0; line < height; ++line) {
    __m128i *dst128 = (__m128i *)(dst[0] + line * outStride);

    for (i = 0; i < width; i+=8) {
      PIXCONV_LOAD_PIXEL8_ALIGNED(xmm0, (y+i));
      xmm0 = _mm_slli_epi16(xmm0, shift);
      PIXCONV_LOAD_PIXEL8_ALIGNED(xmm1, (u+i));
      xmm1 = _mm_slli_epi16(xmm1, shift);
      PIXCONV_LOAD_PIXEL8_ALIGNED(xmm2, (v+i));
      xmm2 = _mm_slli_epi16(xmm2, shift+4);  // +4 so its directly aligned properly (data from bit 14 to bit 4)

      xmm3 = _mm_unpacklo_epi16(xmm1, xmm2); // 0VVVVV00000UUUUU
      xmm4 = _mm_unpackhi_epi16(xmm1, xmm2); // 0VVVVV00000UUUUU
      xmm3 = _mm_or_si128(xmm3, xmm7);       // AVVVVV00000UUUUU
      xmm4 = _mm_or_si128(xmm4, xmm7);       // AVVVVV00000UUUUU

      xmm5 = _mm_unpacklo_epi16(xmm0, xmm6); // 00000000000YYYYY
      xmm2 = _mm_unpackhi_epi16(xmm0, xmm6); // 00000000000YYYYY
      xmm5 = _mm_slli_epi32(xmm5, 10);       // 000000YYYYY00000
      xmm2 = _mm_slli_epi32(xmm2, 10);       // 000000YYYYY00000

      xmm3 = _mm_or_si128(xmm3, xmm5);       // AVVVVVYYYYYUUUUU
      xmm4 = _mm_or_si128(xmm4, xmm2);       // AVVVVVYYYYYUUUUU

      // Write data back
      _mm_stream_si128(dst128++, xmm3);
      _mm_stream_si128(dst128++, xmm4);
    }

    y += inStride;
    u += inStride;
    v += inStride;
  }
  return S_OK;
}

#define PIXCONV_INTERLEAVE_AYUV(regY, regU, regV, regA, regOut1, regOut2) \
  regY    = _mm_unpacklo_epi8(regY, regA);     /* YAYAYAYA */             \
  regV    = _mm_unpacklo_epi8(regV, regU);     /* VUVUVUVU */             \
  regOut1 = _mm_unpacklo_epi16(regV, regY);    /* VUYAVUYA */             \
  regOut2 = _mm_unpackhi_epi16(regV, regY);    /* VUYAVUYA */

#define YUV444_PACK_AYUV(dst) *idst++ = v[i] | (u[i] << 8) | (y[i] << 16) | (0xff << 24);

HRESULT CFormatConverter::convert_yuv444_ayuv(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[])
{
  const uint8_t *y = (const uint8_t *)src[0];
  const uint8_t *u = (const uint8_t *)src[1];
  const uint8_t *v = (const uint8_t *)src[2];

  const ptrdiff_t inStride = srcStride[0];
  const ptrdiff_t outStride = dstStride[0];

  ptrdiff_t line, i;

  __m128i xmm0,xmm1,xmm2,xmm3,xmm4,xmm5,xmm6;

  xmm6 = _mm_set1_epi32(-1);

  _mm_sfence();

  for (line = 0; line < height; ++line) {
    __m128i *dst128 = (__m128i *)(dst[0] + line * outStride);

    for (i = 0; i < width; i+=16) {
      // Load pixels into registers
      PIXCONV_LOAD_PIXEL8_ALIGNED(xmm0, (y+i)); /* YYYYYYYY */
      PIXCONV_LOAD_PIXEL8_ALIGNED(xmm1, (u+i)); /* UUUUUUUU */
      PIXCONV_LOAD_PIXEL8_ALIGNED(xmm2, (v+i)); /* VVVVVVVV */

      // Interlave into AYUV
      xmm4 = xmm0;
      xmm0 = _mm_unpacklo_epi8(xmm0, xmm6);     /* YAYAYAYA */
      xmm4 = _mm_unpackhi_epi8(xmm4, xmm6);     /* YAYAYAYA */

      xmm5 = xmm2;
      xmm2 = _mm_unpacklo_epi8(xmm2, xmm1);     /* VUVUVUVU */
      xmm5 = _mm_unpackhi_epi8(xmm5, xmm1);     /* VUVUVUVU */

      xmm1 = _mm_unpacklo_epi16(xmm2, xmm0);    /* VUYAVUYA */
      xmm2 = _mm_unpackhi_epi16(xmm2, xmm0);    /* VUYAVUYA */

      xmm0 = _mm_unpacklo_epi16(xmm5, xmm4);    /* VUYAVUYA */
      xmm3 = _mm_unpackhi_epi16(xmm5, xmm4);    /* VUYAVUYA */

      // Write data back
      _mm_stream_si128(dst128++, xmm1);
      _mm_stream_si128(dst128++, xmm2);
      _mm_stream_si128(dst128++, xmm0);
      _mm_stream_si128(dst128++, xmm3);
    }

    y += inStride;
    u += inStride;
    v += inStride;
  }

  return S_OK;
}

HRESULT CFormatConverter::convert_yuv444_ayuv_dither_le(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[])
{
  const uint16_t *y = (const uint16_t *)src[0];
  const uint16_t *u = (const uint16_t *)src[1];
  const uint16_t *v = (const uint16_t *)src[2];

  const ptrdiff_t inStride = srcStride[0] >> 1;
  const ptrdiff_t outStride = dstStride[0];

  const uint16_t *dithers = NULL;

  ptrdiff_t line, i;

  __m128i xmm0,xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7;

  xmm7 = _mm_set1_epi16(-256); /* 0xFF00 - 0A0A0A0A */

  _mm_sfence();

  for (line = 0; line < height; ++line) {
    // Load dithering coefficients for this line
    PIXCONV_LOAD_DITHER_COEFFS(xmm6,line,8,dithers);
    xmm4 = xmm5 = xmm6;

    __m128i *dst128 = (__m128i *)(dst[0] + line * outStride);

    for (i = 0; i < width; i+=8) {
      // Load pixels into registers, and apply dithering
      PIXCONV_LOAD_PIXEL16_DITHER(xmm0, xmm4, (y+i), m_FProps.lumabits); /* Y0Y0Y0Y0 */
      PIXCONV_LOAD_PIXEL16_DITHER_HIGH(xmm1, xmm5, (u+i), m_FProps.lumabits); /* U0U0U0U0 */
      PIXCONV_LOAD_PIXEL16_DITHER(xmm2, xmm6, (v+i), m_FProps.lumabits); /* V0V0V0V0 */

      // Interlave into AYUV
      xmm0 = _mm_or_si128(xmm0, xmm7);          /* YAYAYAYA */
      xmm1 = _mm_and_si128(xmm1, xmm7);         /* clear out clobbered low-bytes */
      xmm2 = _mm_or_si128(xmm2, xmm1);          /* VUVUVUVU */

      xmm3 = xmm2;
      xmm2 = _mm_unpacklo_epi16(xmm2, xmm0);    /* VUYAVUYA */
      xmm3 = _mm_unpackhi_epi16(xmm3, xmm0);    /* VUYAVUYA */

      // Write data back
      _mm_stream_si128(dst128++, xmm2);
      _mm_stream_si128(dst128++, xmm3);
    }

    y += inStride;
    u += inStride;
    v += inStride;
  }

  return S_OK;
}

HRESULT CFormatConverter::convert_yuv420_px1x_le(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[])
{
  const uint16_t *y = (const uint16_t *)src[0];
  const uint16_t *u = (const uint16_t *)src[1];
  const uint16_t *v = (const uint16_t *)src[2];

  const ptrdiff_t inYStride   = srcStride[0] >> 1;
  const ptrdiff_t inUVStride  = srcStride[1] >> 1;
  const ptrdiff_t outYStride  = dstStride[0];
  const ptrdiff_t outUVStride = dstStride[1];
  const ptrdiff_t uvHeight    = (m_out_pixfmt == PixFmt_P010 || m_out_pixfmt == PixFmt_P016) ? (height >> 1) : height;
  const ptrdiff_t uvWidth     = (width + 1) >> 1;

  ptrdiff_t line, i;
  __m128i xmm0,xmm1,xmm2;

  _mm_sfence();

  // Process Y
  for (line = 0; line < height; ++line) {
    __m128i *dst128Y = (__m128i *)(dst[0] + line * outYStride);

    for (i = 0; i < width; i+=16) {
      // Load 8 pixels into register
      PIXCONV_LOAD_PIXEL16(xmm0, (y+i+0), m_FProps.lumabits); /* YYYY */
      PIXCONV_LOAD_PIXEL16(xmm1, (y+i+8), m_FProps.lumabits); /* YYYY */
      // and write them out
      _mm_stream_si128(dst128Y++, xmm0);
      _mm_stream_si128(dst128Y++, xmm1);
    }

    y += inYStride;
  }

  // Process UV
  for (line = 0; line < uvHeight; ++line) {
    __m128i *dst128UV = (__m128i *)(dst[1] + line * outUVStride);

    for (i = 0; i < uvWidth; i+=8) {
      // Load 8 pixels into register
      PIXCONV_LOAD_PIXEL16(xmm0, (v+i), m_FProps.lumabits); /* VVVV */
      PIXCONV_LOAD_PIXEL16(xmm1, (u+i), m_FProps.lumabits); /* UUUU */

      xmm2 = xmm0;
      xmm0 = _mm_unpacklo_epi16(xmm1, xmm0);    /* UVUV */
      xmm2 = _mm_unpackhi_epi16(xmm1, xmm2);    /* UVUV */

      _mm_stream_si128(dst128UV++, xmm0);
      _mm_stream_si128(dst128UV++, xmm2);
    }

    u += inUVStride;
    v += inUVStride;
  }

  return S_OK;
}

HRESULT CFormatConverter::convert_yuv422_yuy2_uyvy_dither_le(const uint8_t* const src[4], const int srcStride[4], uint8_t* dst[], int width, int height, int dstStride[])
{
  const uint16_t *y = (const uint16_t *)src[0];
  const uint16_t *u = (const uint16_t *)src[1];
  const uint16_t *v = (const uint16_t *)src[2];

  const ptrdiff_t inLumaStride    = srcStride[0] >> 1;
  const ptrdiff_t inChromaStride  = srcStride[1] >> 1;
  const ptrdiff_t outStride       = dstStride[0];
  const ptrdiff_t chromaWidth     = (width + 1) >> 1;

  const uint16_t *dithers = NULL;

  ptrdiff_t line,i;
  __m128i xmm0,xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7;

  _mm_sfence();

  for (line = 0;  line < height; ++line) {
    __m128i *dst128 = (__m128i *)(dst[0] + line * outStride);

    PIXCONV_LOAD_DITHER_COEFFS(xmm7,line,8,dithers);
    xmm4 = xmm5 = xmm6 = xmm7;

    for (i = 0; i < chromaWidth; i+=8) {
      // Load pixels
      PIXCONV_LOAD_PIXEL16_DITHER(xmm0, xmm4, (y+(i*2)+0), m_FProps.lumabits);  /* YYYY */
      PIXCONV_LOAD_PIXEL16_DITHER(xmm1, xmm5, (y+(i*2)+8), m_FProps.lumabits);  /* YYYY */
      PIXCONV_LOAD_PIXEL16_DITHER(xmm2, xmm6, (u+i), m_FProps.lumabits);        /* UUUU */
      PIXCONV_LOAD_PIXEL16_DITHER(xmm3, xmm7, (v+i), m_FProps.lumabits);        /* VVVV */

      // Pack Ys
      xmm0 = _mm_packus_epi16(xmm0, xmm1);

      // Interleave Us and Vs
      xmm2 = _mm_packus_epi16(xmm2, xmm2);
      xmm3 = _mm_packus_epi16(xmm3, xmm3);
      xmm2 = _mm_unpacklo_epi8(xmm2, xmm3);

      // Interlave those with the Ys
      xmm3 = xmm0;
      xmm3 = _mm_unpacklo_epi8(xmm3, xmm2);
      xmm2 = _mm_unpackhi_epi8(xmm0, xmm2);

      _mm_stream_si128(dst128++, xmm3);
      _mm_stream_si128(dst128++, xmm2);
    }
    y += inLumaStride;
    u += inChromaStride;
    v += inChromaStride;
  }

  return S_OK;
}


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

		// update the additional properties (updated only when changing basic properties)
		m_FProps.lumabits	= GetLumaBits(m_FProps.avpixfmt);
		m_FProps.pftype		= GetPixFmtType(m_FProps.avpixfmt);
		m_FProps.colorspace	= pFrame->colorspace;
		m_FProps.colorrange	= pFrame->color_range;

		if (!Init()) {
			TRACE(_T("FormatConverter: Init() failed\n"));
			return 0;
		}

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

	if (m_out_pixfmt == PixFmt_YV12 || m_out_pixfmt == PixFmt_YV24) {
		std::swap(dstArray[1], dstArray[2]);
	}

	(this->*pConvertFn)(pFrame->data, pFrame->linesize, dstArray, m_FProps.width, m_FProps.height, dstStrideArray);

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
