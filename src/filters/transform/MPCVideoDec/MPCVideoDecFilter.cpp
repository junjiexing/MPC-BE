/*
 * $Id$
 *
 * (C) 2006-2012 see Authors.txt
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
#include <math.h>
#include <atlbase.h>
#include <MMReg.h>

#include <ffmpeg/PODtypes.h>
#include <ffmpeg/libavcodec/avcodec.h>

#ifdef REGISTER_FILTER
#include <InitGuid.h>
#endif
#include "MPCVideoDecFilter.h"
#include "VideoDecOutputPin.h"
#include "CpuId.h"

#include "ffImgfmt.h"
#include "FfmpegContext.h"
extern "C"
{
#include <ffmpeg/libswscale/swscale.h>

// === New swscaler options
#include <ffmpeg/libavutil/opt.h>
//
}

#include "../../../DSUtil/DSUtil.h"
#include "../../../DSUtil/MediaTypes.h"
#include "../../parser/MpegSplitter/MpegSplitter.h"
#include "../../parser/OggSplitter/OggSplitter.h"
#include "../../parser/RealMediaSplitter/RealMediaSplitter.h"
#include <moreuuids.h>
#include "DXVADecoderH264.h"
#include "../../../apps/mplayerc/FilterEnum.h"

#include "../../../DSUtil/WinAPIUtils.h"

#define MAX_SUPPORTED_MODE			5
#define AVRTIMEPERFRAME_VC1_EVO 417083

typedef struct {
	const int			PicEntryNumber;
	const UINT			PreferedConfigBitstream;
	const GUID*			Decoder[MAX_SUPPORTED_MODE];
	const WORD			RestrictedMode[MAX_SUPPORTED_MODE];
} DXVA_PARAMS;

typedef struct {
	const CLSID*		clsMinorType;
	const enum CodecID	nFFCodec;
	const DXVA_PARAMS*	DXVAModes;

	int					DXVAModeCount() {
		if (!DXVAModes) {
			return 0;
		}
		for (int i=0; i<MAX_SUPPORTED_MODE; i++) {
			if (DXVAModes->Decoder[i] == &GUID_NULL) {
				return i;
			}
		}
		return MAX_SUPPORTED_MODE;
	}
} FFMPEG_CODECS;

// DXVA modes supported for Mpeg2
DXVA_PARAMS		DXVA_Mpeg2 = {
	10,		// PicEntryNumber
	1,		// PreferedConfigBitstream
	{ &DXVA2_ModeMPEG2_VLD, &GUID_NULL },
	{ DXVA_RESTRICTED_MODE_UNRESTRICTED, 0 } // Restricted mode for DXVA1?
};

// DXVA modes supported for H264
DXVA_PARAMS		DXVA_H264 = {
	16,		// PicEntryNumber
	2,		// PreferedConfigBitstream
	{ &DXVA2_ModeH264_E, &DXVA2_ModeH264_F, &DXVA_Intel_H264_ClearVideo, &GUID_NULL },
	{ DXVA_RESTRICTED_MODE_H264_E, 0}
};

DXVA_PARAMS		DXVA_H264_VISTA = {
	24,		// PicEntryNumber
	2,		// PreferedConfigBitstream
	{ &DXVA2_ModeH264_E, &DXVA2_ModeH264_F, &DXVA_Intel_H264_ClearVideo, &GUID_NULL },
	{ DXVA_RESTRICTED_MODE_H264_E, 0}
};

// DXVA modes supported for VC1
DXVA_PARAMS		DXVA_VC1 = {
	10,		// PicEntryNumber
	1,		// PreferedConfigBitstream
	{ &DXVA2_ModeVC1_D,				&GUID_NULL },
	{ DXVA_RESTRICTED_MODE_VC1_D, 0}
};

FFMPEG_CODECS		ffCodecs[] = {
	// Flash video
	{ &MEDIASUBTYPE_FLV1, CODEC_ID_FLV1, NULL },
	{ &MEDIASUBTYPE_flv1, CODEC_ID_FLV1, NULL },
	{ &MEDIASUBTYPE_FLV4, CODEC_ID_VP6F, NULL },
	{ &MEDIASUBTYPE_flv4, CODEC_ID_VP6F, NULL },
	{ &MEDIASUBTYPE_VP6F, CODEC_ID_VP6F, NULL },
	{ &MEDIASUBTYPE_vp6f, CODEC_ID_VP6F, NULL },

	// VP3
	{ &MEDIASUBTYPE_VP30, CODEC_ID_VP3,  NULL },
	{ &MEDIASUBTYPE_VP31, CODEC_ID_VP3,  NULL },

	// VP5
	{ &MEDIASUBTYPE_VP50, CODEC_ID_VP5,  NULL },
	{ &MEDIASUBTYPE_vp50, CODEC_ID_VP5,  NULL },

	// VP6
	{ &MEDIASUBTYPE_VP60, CODEC_ID_VP6,  NULL },
	{ &MEDIASUBTYPE_vp60, CODEC_ID_VP6,  NULL },
	{ &MEDIASUBTYPE_VP61, CODEC_ID_VP6,  NULL },
	{ &MEDIASUBTYPE_vp61, CODEC_ID_VP6,  NULL },
	{ &MEDIASUBTYPE_VP62, CODEC_ID_VP6,  NULL },
	{ &MEDIASUBTYPE_vp62, CODEC_ID_VP6,  NULL },
	{ &MEDIASUBTYPE_VP6A, CODEC_ID_VP6A, NULL },
	{ &MEDIASUBTYPE_vp6a, CODEC_ID_VP6A, NULL },

	// VP8
	{ &MEDIASUBTYPE_VP80, CODEC_ID_VP8, NULL },

	// Xvid
	{ &MEDIASUBTYPE_XVID, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_xvid, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_XVIX, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_xvix, CODEC_ID_MPEG4, NULL },

	// DivX
	{ &MEDIASUBTYPE_DX50, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_dx50, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_DIVX, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_divx, CODEC_ID_MPEG4, NULL },

	// WMV1/2/3
	{ &MEDIASUBTYPE_WMV1, CODEC_ID_WMV1, NULL },
	{ &MEDIASUBTYPE_wmv1, CODEC_ID_WMV1, NULL },
	{ &MEDIASUBTYPE_WMV2, CODEC_ID_WMV2, NULL },
	{ &MEDIASUBTYPE_wmv2, CODEC_ID_WMV2, NULL },
	{ &MEDIASUBTYPE_WMV3, CODEC_ID_WMV3, &DXVA_VC1 },
	{ &MEDIASUBTYPE_wmv3, CODEC_ID_WMV3, &DXVA_VC1 },

	// MPEG-2
	{ &MEDIASUBTYPE_MPEG2_VIDEO,	CODEC_ID_MPEG2VIDEO, &DXVA_Mpeg2 },
	{ &MEDIASUBTYPE_MPG2,		CODEC_ID_MPEG2VIDEO, &DXVA_Mpeg2 },

	// MSMPEG-4
	{ &MEDIASUBTYPE_DIV3, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_div3, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_DVX3, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_dvx3, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_MP43, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_mp43, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_COL1, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_col1, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_DIV4, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_div4, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_DIV5, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_div5, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_DIV6, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_div6, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_AP41, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_ap41, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_MPG3, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_mpg3, CODEC_ID_MSMPEG4V3, NULL },
	{ &MEDIASUBTYPE_DIV2, CODEC_ID_MSMPEG4V2, NULL },
	{ &MEDIASUBTYPE_div2, CODEC_ID_MSMPEG4V2, NULL },
	{ &MEDIASUBTYPE_MP42, CODEC_ID_MSMPEG4V2, NULL },
	{ &MEDIASUBTYPE_mp42, CODEC_ID_MSMPEG4V2, NULL },
	{ &MEDIASUBTYPE_MPG4, CODEC_ID_MSMPEG4V1, NULL },
	{ &MEDIASUBTYPE_mpg4, CODEC_ID_MSMPEG4V1, NULL },
	{ &MEDIASUBTYPE_DIV1, CODEC_ID_MSMPEG4V1, NULL },
	{ &MEDIASUBTYPE_div1, CODEC_ID_MSMPEG4V1, NULL },
	{ &MEDIASUBTYPE_MP41, CODEC_ID_MSMPEG4V1, NULL },
	{ &MEDIASUBTYPE_mp41, CODEC_ID_MSMPEG4V1, NULL },

	// AMV Video
	{ &MEDIASUBTYPE_AMVV, CODEC_ID_AMV, NULL },

	// MJPEG
	{ &MEDIASUBTYPE_MJPG,   CODEC_ID_MJPEG,  NULL },
	{ &MEDIASUBTYPE_QTJpeg, CODEC_ID_MJPEG,  NULL },
	{ &MEDIASUBTYPE_MJPA,   CODEC_ID_MJPEG,  NULL },
	{ &MEDIASUBTYPE_MJPB,   CODEC_ID_MJPEGB, NULL },
	
	// DV VIDEO
	{ &MEDIASUBTYPE_dvsl,   CODEC_ID_DVVIDEO,  NULL },
	{ &MEDIASUBTYPE_dvsd,   CODEC_ID_DVVIDEO,  NULL },
	{ &MEDIASUBTYPE_dvhd,   CODEC_ID_DVVIDEO,  NULL },
	{ &MEDIASUBTYPE_dv25,   CODEC_ID_DVVIDEO,  NULL },
	{ &MEDIASUBTYPE_dv50,   CODEC_ID_DVVIDEO,  NULL },
	{ &MEDIASUBTYPE_dvh1,   CODEC_ID_DVVIDEO,  NULL },

	// CSCD
	{ &MEDIASUBTYPE_CSCD,   CODEC_ID_CSCD, NULL },

	// TSCC
	{ &MEDIASUBTYPE_TSCC,   CODEC_ID_TSCC, NULL },

	// QTRLE
	{ &MEDIASUBTYPE_QTRle,  CODEC_ID_QTRLE, NULL },

	// CINEPAK
	{ &MEDIASUBTYPE_CVID,  CODEC_ID_CINEPAK, NULL },

	// FLASHSV1
	{ &MEDIASUBTYPE_FLASHSV1,  CODEC_ID_FLASHSV, NULL },

	// FLASHSV2
//	{ &MEDIASUBTYPE_FLASHSV2,  CODEC_ID_FLASHSV2, NULL },

	// FRAPS
	{ &MEDIASUBTYPE_FPS1,  CODEC_ID_FRAPS, NULL },

	// TSCC2
	{ &MEDIASUBTYPE_TSCC2,   CODEC_ID_TSCC2, NULL },

	// MSS1
	{ &MEDIASUBTYPE_MSS1,   CODEC_ID_MSS1, NULL },

	// MSA1
	{ &MEDIASUBTYPE_MSA1,   CODEC_ID_MSA1, NULL },

	// MTS2
	{ &MEDIASUBTYPE_MTS2,   CODEC_ID_MTS2, NULL },

	// UtVideo
	{ &MEDIASUBTYPE_UTVD_ULRG,   CODEC_ID_UTVIDEO, NULL },
	{ &MEDIASUBTYPE_UTVD_ULRA,   CODEC_ID_UTVIDEO, NULL },
	{ &MEDIASUBTYPE_UTVD_ULY0,   CODEC_ID_UTVIDEO, NULL },
	{ &MEDIASUBTYPE_UTVD_ULY2,   CODEC_ID_UTVIDEO, NULL },

	// DIRAC
	{ &MEDIASUBTYPE_DRAC,   CODEC_ID_DIRAC,  NULL },

	// LAGARITH
	{ &MEDIASUBTYPE_Lagarith,   CODEC_ID_LAGARITH,  NULL },

	// Indeo 3/4/5
	{ &MEDIASUBTYPE_IV31,   CODEC_ID_INDEO3, NULL },
	{ &MEDIASUBTYPE_IV32,   CODEC_ID_INDEO3, NULL },
	{ &MEDIASUBTYPE_IV41,   CODEC_ID_INDEO4, NULL },
	{ &MEDIASUBTYPE_IV50,   CODEC_ID_INDEO5, NULL },

	// H264/AVC
	{ &MEDIASUBTYPE_H264, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_h264, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_X264, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_x264, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_VSSH, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_vssh, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_DAVC, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_davc, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_PAVC, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_pavc, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_AVC1, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_avc1, CODEC_ID_H264,	 &DXVA_H264 },
	{ &MEDIASUBTYPE_H264_bis, CODEC_ID_H264, &DXVA_H264 },

	// SVQ3
	{ &MEDIASUBTYPE_SVQ3, CODEC_ID_SVQ3, NULL },

	// SVQ1
	{ &MEDIASUBTYPE_SVQ1, CODEC_ID_SVQ1, NULL },

	// H263
	{ &MEDIASUBTYPE_H263, CODEC_ID_H263, NULL },
	{ &MEDIASUBTYPE_h263, CODEC_ID_H263, NULL },

	{ &MEDIASUBTYPE_S263, CODEC_ID_H263, NULL },
	{ &MEDIASUBTYPE_s263, CODEC_ID_H263, NULL },

	// Real Video
	{ &MEDIASUBTYPE_RV10, CODEC_ID_RV10, NULL },
	{ &MEDIASUBTYPE_RV20, CODEC_ID_RV20, NULL },
	{ &MEDIASUBTYPE_RV30, CODEC_ID_RV30, NULL },
	{ &MEDIASUBTYPE_RV40, CODEC_ID_RV40, NULL },

	// Theora
	{ &MEDIASUBTYPE_THEORA, CODEC_ID_THEORA, NULL },
	{ &MEDIASUBTYPE_theora, CODEC_ID_THEORA, NULL },

	// WVC1
	{ &MEDIASUBTYPE_WVC1, CODEC_ID_VC1, &DXVA_VC1 },
	{ &MEDIASUBTYPE_wvc1, CODEC_ID_VC1, &DXVA_VC1 },

	// Apple ProRes
	{ &MEDIASUBTYPE_apch, CODEC_ID_PRORES, NULL },
	{ &MEDIASUBTYPE_apcn, CODEC_ID_PRORES, NULL },
	{ &MEDIASUBTYPE_apcs, CODEC_ID_PRORES, NULL },
	{ &MEDIASUBTYPE_apco, CODEC_ID_PRORES, NULL },
	{ &MEDIASUBTYPE_ap4h, CODEC_ID_PRORES, NULL },

	// Bink Video
	{ &MEDIASUBTYPE_BINKVI, CODEC_ID_BINKVIDEO, NULL },
	{ &MEDIASUBTYPE_BINKVB, CODEC_ID_BINKVIDEO, NULL },

	// PNG
	{ &MEDIASUBTYPE_PNG, CODEC_ID_PNG, NULL },

	// Other MPEG-4
	{ &MEDIASUBTYPE_MP4V, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_mp4v, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_M4S2, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_m4s2, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_MP4S, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_mp4s, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_3IV1, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_3iv1, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_3IV2, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_3iv2, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_3IVX, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_3ivx, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_BLZ0, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_blz0, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_DM4V, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_dm4v, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_FFDS, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_ffds, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_FVFW, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_fvfw, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_DXGM, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_dxgm, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_FMP4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_fmp4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_HDX4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_hdx4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_LMP4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_lmp4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_NDIG, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_ndig, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_RMP4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_rmp4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_SMP4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_smp4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_SEDG, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_sedg, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_UMP4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_ump4, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_WV1F, CODEC_ID_MPEG4, NULL },
	{ &MEDIASUBTYPE_wv1f, CODEC_ID_MPEG4, NULL }
};

/* Important: the order should be exactly the same as in ffCodecs[] */
const AMOVIESETUP_MEDIATYPE CMPCVideoDecFilter::sudPinTypesIn[] = {
	// Flash video
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_FLV1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_flv1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_FLV4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_flv4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VP6F   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_vp6f   },

	// VP3
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VP30   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VP31   },

	// VP5
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VP50   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_vp50   },

	// VP6
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VP60   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_vp60   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VP61   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_vp61   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VP62   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_vp62   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VP6A   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_vp6a   },

	// VP8
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VP80   },

	// Xvid
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_XVID   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_xvid   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_XVIX   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_xvix   },

	// DivX
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DX50   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dx50   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DIVX   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_divx   },

	// WMV1/2/3
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_WMV1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_wmv1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_WMV2   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_wmv2   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_WMV3   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_wmv3   },

	// MPEG-2
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MPEG2_VIDEO },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MPG2 },

	// MSMPEG-4
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DIV3   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_div3   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DVX3   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dvx3   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MP43   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_mp43   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_COL1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_col1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DIV4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_div4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DIV5   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_div5   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DIV6   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_div6   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_AP41   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_ap41   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MPG3   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_mpg3   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DIV2   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_div2   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MP42   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_mp42   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MPG4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_mpg4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DIV1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_div1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MP41   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_mp41   },

	// AMV Video
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_AMVV   },

	// MJPEG
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MJPG   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_QTJpeg },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MJPA   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MJPB   },

	// DV VIDEO
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dvsl   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dvsd   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dvhd   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dv25   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dv50   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dvh1   },

	// CSCD
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_CSCD   },

	// TSCC
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_TSCC   },

	// QTRLE
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_QTRle   },

	// CINEPAK
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_CVID   },

	// FLASHSV1
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_FLASHSV1   },

	// FLASHSV2
//	{ &MEDIATYPE_Video, &MEDIASUBTYPE_FLASHSV2   },

	// Fraps
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_FPS1       },

	// TSCC2
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_TSCC2       },

	// MSS1
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MSS1       },

	// MSA1
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MSA1       },

	// MTS2
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MTS2       },

	// UtVideo
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_UTVD_ULRG   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_UTVD_ULRA   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_UTVD_ULY0   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_UTVD_ULY2   },

	// DIRAC
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DRAC   },

	// LAGARITH
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_Lagarith   },

	// Indeo 3/4/5
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_IV31   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_IV32   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_IV41   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_IV50   },

	// H264/AVC
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_H264   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_h264   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_X264   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_x264   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_VSSH   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_vssh   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DAVC   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_davc   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_PAVC   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_pavc   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_AVC1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_avc1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_H264_bis },

	// SVQ3
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_SVQ3   },

	// SVQ1
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_SVQ1   },

	// H263
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_H263   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_h263   },

	{ &MEDIATYPE_Video, &MEDIASUBTYPE_S263   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_s263   },

	// Real video
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_RV10   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_RV20   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_RV30   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_RV40   },

	// Theora
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_THEORA },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_theora },

	// VC1
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_WVC1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_wvc1   },

	// Apple ProRes
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_apch },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_apcn },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_apcs },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_apco },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_ap4h },

	// Bink Video
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_BINKVI },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_BINKVB },

	// PNG
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_PNG },

	// IMPORTANT : some of the last MediaTypes present in next group may be not available in
	// the standalone filter (workaround to prevent GraphEdit crash).
	// Other MPEG-4
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MP4V   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_mp4v   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_M4S2   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_m4s2   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_MP4S   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_mp4s   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_3IV1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_3iv1   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_3IV2   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_3iv2   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_3IVX   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_3ivx   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_BLZ0   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_blz0   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DM4V   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dm4v   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_FFDS   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_ffds   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_FVFW   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_fvfw   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_DXGM   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_dxgm   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_FMP4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_fmp4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_HDX4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_hdx4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_LMP4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_lmp4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_NDIG   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_ndig   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_RMP4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_rmp4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_SMP4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_smp4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_SEDG   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_sedg   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_UMP4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_ump4   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_WV1F   },
	{ &MEDIATYPE_Video, &MEDIASUBTYPE_wv1f   }
};

const int CMPCVideoDecFilter::sudPinTypesInCount = _countof(CMPCVideoDecFilter::sudPinTypesIn);

const AMOVIESETUP_MEDIATYPE CMPCVideoDecFilter::sudPinTypesOut[] = {
	{&MEDIATYPE_Video, &MEDIASUBTYPE_NV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_NV24}
};
const int CMPCVideoDecFilter::sudPinTypesOutCount = _countof(CMPCVideoDecFilter::sudPinTypesOut);

BOOL CALLBACK EnumFindProcessWnd (HWND hwnd, LPARAM lParam)
{
	DWORD	procid = 0;
	TCHAR	WindowClass [40];
	GetWindowThreadProcessId (hwnd, &procid);
	GetClassName (hwnd, WindowClass, _countof(WindowClass));

	if (procid == GetCurrentProcessId() && _tcscmp (WindowClass, _T("MediaPlayerClassicW")) == 0) {
		HWND*		pWnd = (HWND*) lParam;
		*pWnd = hwnd;
		return FALSE;
	}
	return TRUE;
}

CMPCVideoDecFilter::CMPCVideoDecFilter(LPUNKNOWN lpunk, HRESULT* phr)
	: CBaseVideoFilter(NAME("MPC - Video decoder"), lpunk, phr, __uuidof(this))
{
	HWND		hWnd = NULL;

	if (IsWinVistaOrLater()) {
		for (int i=0; i<_countof(ffCodecs); i++) {
			if (ffCodecs[i].nFFCodec == CODEC_ID_H264) {
				ffCodecs[i].DXVAModes = &DXVA_H264_VISTA;
			}
		}
	}

	if (phr) {
		*phr = S_OK;
	}

	if (m_pOutput)	{
		delete m_pOutput;
	}
	m_pOutput = DNew CVideoDecOutputPin(NAME("CVideoDecOutputPin"), this, phr, L"Output");
	if (!m_pOutput) {
		*phr = E_OUTOFMEMORY;
	}

	FFmpegFilters = NULL;
	DXVAFilters = NULL;

	m_pCpuId				= DNew CCpuId();
	m_pAVCodec				= NULL;
	m_pAVCtx				= NULL;
	m_pFrame				= NULL;
	m_nCodecNb				= -1;
	m_nCodecId				= CODEC_ID_NONE;
	m_bReorderBFrame		= true;
	m_DXVADecoderGUID		= GUID_NULL;
	m_nActiveCodecs			= MPCVD_H264|MPCVD_VC1|MPCVD_XVID|MPCVD_DIVX|MPCVD_MSMPEG4|MPCVD_FLASH|MPCVD_WMV|MPCVD_H263|MPCVD_SVQ3|MPCVD_AMVV|MPCVD_THEORA|MPCVD_H264_DXVA|MPCVD_VC1_DXVA|MPCVD_VP356|MPCVD_VP8|MPCVD_MJPEG|MPCVD_INDEO|MPCVD_RV|MPCVD_WMV3_DXVA|MPCVD_MPEG2_DXVA|MPCVD_DIRAC|MPCVD_DV|MPCVD_UTVD|MPCVD_SCREC|MPCVD_LAGARITH|MPCVD_PRORES|MPCVD_BINKV|MPCVD_PNG;

	m_rtAvrTimePerFrame		= 0;
	m_rtLastStart			= 0;
	m_nCountEstimated		= 0;
	m_rtPrevStop			= 0;

	m_nWorkaroundBug		= FF_BUG_AUTODETECT;
	m_nErrorConcealment		= FF_EC_DEBLOCK | FF_EC_GUESS_MVS;

	m_nThreadNumber			= 0;
	m_nDiscardMode			= AVDISCARD_DEFAULT;
	m_nErrorRecognition		= AV_EF_CAREFUL;
	m_nIDCTAlgo				= FF_IDCT_AUTO;
	m_bDXVACompatible		= true;
	m_pFFBuffer				= NULL;
	m_nFFBufferSize			= 0;
	m_pAlignedFFBuffer		= NULL;
	m_nAlignedFFBufferSize	= 0;
	ResetBuffer();

	m_nWidth				= 0;
	m_nHeight				= 0;
	m_nOutputWidth			= 0;
	m_nOutputHeight			= 0;
	m_pSwsContext			= NULL;

	m_bUseDXVA				= true;
	m_bUseFFmpeg			= true;

	m_nDXVAMode				= MODE_SOFTWARE;
	m_pDXVADecoder			= NULL;
	m_pVideoOutputFormat	= NULL;
	m_nVideoOutputCount		= 0;
	m_hDevice				= INVALID_HANDLE_VALUE;

	m_nARMode					= 1;
	m_nDXVACheckCompatibility	= 1; // skip level check by default
	m_nDXVA_SD					= 0;
	m_sar.SetSize(1,1);

	m_bWaitingForKeyFrame	= TRUE;
	m_nPosB					= 1;
	m_bFrame_repeat_pict	= false;
	m_bIsEVO				= false;

	m_nFrameType			= PICT_FRAME;
	m_nOutCsp				= 0;
	
	// === New swscaler options
	m_nSwRefresh			= 0;
	m_nSwOutputFormats		= 0;
	// set the output formats order in the DWORD nibbles with literal values [0x00543210] = 0,1,2,3,4,5
	// set the output formats count in the DWORD nibbles adding checked flag (8) [0x0054ba98] = 4
	for (int i=0; i<6; i++) {
		m_nSwOutputFormats = (m_nSwOutputFormats<<4) | (5-i + (i>1 ? 8 : 0));
	}
	m_nSwChromaToRGB		= 1;
	m_nSwResizeMethodBE		= 2;
	m_nSwColorspace			= 2;
	m_nSwInputLevels		= 2;
	m_nSwOutputLevels		= 2;
	//

	m_PixFmt				= PIX_FMT_NB;

	m_nDialogHWND			= 0;
#ifdef REGISTER_FILTER
	CRegKey key;
	if (ERROR_SUCCESS == key.Open(HKEY_CURRENT_USER, _T("Software\\MPC-BE Filters\\MPC Video Decoder"), KEY_READ)) {
		DWORD dw;
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("ThreadNumber"), dw)) {
			m_nThreadNumber = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("DiscardMode"), dw)) {
			m_nDiscardMode = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("ErrorRecognition"), dw)) {
			m_nErrorRecognition = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("IDCTAlgo"), dw)) {
			m_nIDCTAlgo = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("ActiveCodecs"), dw)) {
			m_nActiveCodecs = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("ARMode"), dw)) {
			m_nARMode = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("DXVACheckCompatibility"), dw)) {
			m_nDXVACheckCompatibility = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("DisableDXVA_SD"), dw)) {
			m_nDXVA_SD = dw;
		}

		// === New swscaler options
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("SwOutputFormats"), dw)) {
			m_nSwOutputFormats = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("SwChromaToRGB"), dw)) {
			m_nSwChromaToRGB = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("SwResizeMethodBE"), dw)) {
			m_nSwResizeMethodBE = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("SwColorspace"), dw)) {
			m_nSwColorspace = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("SwInputLevels"), dw)) {
			m_nSwInputLevels = dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(_T("SwOutputLevels"), dw)) {
			m_nSwOutputLevels = dw;
		}
		//
	}
#else
	m_nThreadNumber = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("ThreadNumber"), m_nThreadNumber);
	m_nDiscardMode = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("DiscardMode"), m_nDiscardMode);
	m_nErrorRecognition = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("ErrorRecognition"), m_nErrorRecognition);
	m_nIDCTAlgo = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("IDCTAlgo"), m_nIDCTAlgo);
	m_nARMode = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("ARMode"), m_nARMode);
	m_nDXVACheckCompatibility = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("DXVACheckCompatibility"), m_nDXVACheckCompatibility);
	m_nDXVA_SD = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("DisableDXVA_SD"), m_nDXVA_SD);

	// === New swscaler options
	m_nSwOutputFormats = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwOutputFormats"), m_nSwOutputFormats);
	m_nSwChromaToRGB = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwChromaToRGB"), m_nSwChromaToRGB);
	m_nSwResizeMethodBE = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwResizeMethodBE"), m_nSwResizeMethodBE);
	m_nSwColorspace = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwColorspace"), m_nSwColorspace);
	m_nSwInputLevels = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwInputLevels"), m_nSwInputLevels);
	m_nSwOutputLevels = AfxGetApp()->GetProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwOutputLevels"), m_nSwOutputLevels);
	//
#endif

	if (m_nErrorRecognition != AV_EF_CAREFUL && m_nErrorRecognition != AV_EF_COMPLIANT && m_nErrorRecognition != AV_EF_AGGRESSIVE) {
		m_nErrorRecognition = AV_EF_CAREFUL;
	}
	if (m_nDXVACheckCompatibility > 3) {
		m_nDXVACheckCompatibility = 1;    // skip level check by default
	}

	ff_avcodec_default_get_buffer		= avcodec_default_get_buffer;
	ff_avcodec_default_release_buffer	= avcodec_default_release_buffer;
	ff_avcodec_default_reget_buffer		= avcodec_default_reget_buffer;

	avcodec_register_all();
	av_log_set_callback(LogLibAVCodec);

	EnumWindows(EnumFindProcessWnd, (LPARAM)&hWnd);
	DetectVideoCard(hWnd);

#ifdef _DEBUG
	// Check codec definition table
	int nCodecs	  = _countof(ffCodecs);
	int nPinTypes = _countof(sudPinTypesIn);
	ASSERT (nCodecs == nPinTypes);
	for (int i=0; i<nPinTypes; i++) {
		ASSERT (ffCodecs[i].clsMinorType == sudPinTypesIn[i].clsMinorType);
	}
#endif
}

void CMPCVideoDecFilter::DetectVideoCard(HWND hWnd)
{
	IDirect3D9* pD3D9;
	m_nPCIVendor = 0;
	m_nPCIDevice = 0;
	m_VideoDriverVersion.HighPart = 0;
	m_VideoDriverVersion.LowPart = 0;

	pD3D9 = Direct3DCreate9(D3D_SDK_VERSION);
	if (pD3D9) {
		D3DADAPTER_IDENTIFIER9 adapterIdentifier;
		if (pD3D9->GetAdapterIdentifier(GetAdapter(pD3D9, hWnd), 0, &adapterIdentifier) == S_OK) {
			m_nPCIVendor = adapterIdentifier.VendorId;
			m_nPCIDevice = adapterIdentifier.DeviceId;
			m_VideoDriverVersion = adapterIdentifier.DriverVersion;
			m_strDeviceDescription = adapterIdentifier.Description;
			m_strDeviceDescription.AppendFormat (_T(" (%04X:%04X)"), m_nPCIVendor, m_nPCIDevice);
		}
		pD3D9->Release();
	}
}

CMPCVideoDecFilter::~CMPCVideoDecFilter()
{
	Cleanup();

	SAFE_DELETE(m_pCpuId);
}

bool CMPCVideoDecFilter::IsVideoInterlaced()
{
	// NOT A BUG : always tell DirectShow it's interlaced (progressive flags set in
	// SetTypeSpecificFlags function)
	return true;
};

void CMPCVideoDecFilter::UpdateFrameTime (REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop, bool b_repeat_pict)
{
	REFERENCE_TIME AvgTimePerFrame = m_rtAvrTimePerFrame;
	if (m_rtAvrTimePerFrame == 1 || m_rtAvrTimePerFrame < 166666) { // fps > 60 ... try to get fps value from ffmpeg
		if (m_pAVCtx->time_base.den && m_pAVCtx->time_base.num) {
			AvgTimePerFrame = (10000000I64 * m_pAVCtx->time_base.num / m_pAVCtx->time_base.den) * m_pAVCtx->ticks_per_frame;
		}
	}

	bool m_PullDownFlag = (m_nCodecId == CODEC_ID_VC1 && b_repeat_pict && AvgTimePerFrame == 333666);
	REFERENCE_TIME m_rtFrameDuration = m_PullDownFlag ? AVRTIMEPERFRAME_VC1_EVO : AvgTimePerFrame;

	if ((rtStart == _I64_MIN) || (m_PullDownFlag && m_rtPrevStop && (rtStart <= m_rtPrevStop))) {
		rtStart = m_rtLastStart + (m_rtFrameDuration / m_dRate) * m_nCountEstimated;
		m_nCountEstimated++;
	} else {
		m_rtLastStart		= rtStart;
		m_nCountEstimated	= 1;
	}

	rtStop  = rtStart + (m_rtFrameDuration / m_dRate);
}

void CMPCVideoDecFilter::GetOutputSize(int& w, int& h, int& arx, int& ary, int& RealWidth, int& RealHeight)
{
#if 1
	RealWidth = m_nWidth;
	RealHeight = m_nHeight;
	w = PictWidthRounded();
	h = PictHeightRounded();
#else
	if (m_nDXVAMode == MODE_SOFTWARE) {
		w = m_nWidth;
		h = m_nHeight;
	} else {
		// DXVA surface are multiple of 16 pixels!
		w = PictWidthRounded();
		h = PictHeightRounded();
	}
#endif
}

int CMPCVideoDecFilter::PictWidth()
{
	return m_nWidth;
}

int CMPCVideoDecFilter::PictHeight()
{
	return m_nHeight;
}

int CMPCVideoDecFilter::PictWidthRounded()
{
	// Picture height should be rounded to 16 for DXVA
	return m_nOutputWidth ? m_nOutputWidth : ((m_nWidth + 15) / 16) * 16;
}

int CMPCVideoDecFilter::PictHeightRounded()
{
	// Picture height should be rounded to 16 for DXVA
	return m_nOutputHeight ? m_nOutputHeight : ((m_nHeight + 15) / 16) * 16;
}

int CMPCVideoDecFilter::FindCodec(const CMediaType* mtIn)
{
	for (int i=0; i<_countof(ffCodecs); i++)
		if (mtIn->subtype == *ffCodecs[i].clsMinorType) {
#ifndef REGISTER_FILTER
			switch (ffCodecs[i].nFFCodec) {
				case CODEC_ID_H264 :
					m_bUseDXVA		= DXVAFilters && DXVAFilters[TRA_DXVA_H264];
					m_bUseFFmpeg	= FFmpegFilters && FFmpegFilters[FFM_H264];
					break;
				case CODEC_ID_VC1 :
					m_bUseDXVA		= DXVAFilters && DXVAFilters[TRA_DXVA_VC1];
					m_bUseFFmpeg	= FFmpegFilters && FFmpegFilters[FFM_VC1];
					break;
				case CODEC_ID_WMV3 :
					m_bUseDXVA		= DXVAFilters && DXVAFilters[TRA_DXVA_WMV3];
					m_bUseFFmpeg	= FFmpegFilters && FFmpegFilters[FFM_WMV];
					break;
				case CODEC_ID_MPEG2VIDEO :
					m_bUseDXVA		= true;
					m_bUseFFmpeg	= false; // No Mpeg2 software support with ffmpeg!
					break;
				default :
					m_bUseDXVA		= false;
			}

			return ((m_bUseDXVA || m_bUseFFmpeg) ? i : -1);
#else
			bool	bCodecActivated = false;
			switch (ffCodecs[i].nFFCodec) {
				case CODEC_ID_FLV1 :
				case CODEC_ID_VP6F :
					bCodecActivated = (m_nActiveCodecs & MPCVD_FLASH) != 0;
					break;
				case CODEC_ID_MPEG4 :
					if ((*ffCodecs[i].clsMinorType == MEDIASUBTYPE_DX50) ||		// DivX
							(*ffCodecs[i].clsMinorType == MEDIASUBTYPE_dx50) ||
							(*ffCodecs[i].clsMinorType == MEDIASUBTYPE_DIVX) ||
							(*ffCodecs[i].clsMinorType == MEDIASUBTYPE_divx) ) {
						bCodecActivated = (m_nActiveCodecs & MPCVD_DIVX) != 0;
					} else {
						bCodecActivated = (m_nActiveCodecs & MPCVD_XVID) != 0;	// Xvid/MPEG-4
					}
					break;
				case CODEC_ID_WMV1 :
				case CODEC_ID_WMV2 :
					bCodecActivated = (m_nActiveCodecs & MPCVD_WMV) != 0;
					break;
				case CODEC_ID_WMV3 :
					m_bUseDXVA = (m_nActiveCodecs & MPCVD_WMV3_DXVA) != 0;
					m_bUseFFmpeg = (m_nActiveCodecs & MPCVD_WMV) != 0;
					bCodecActivated = m_bUseDXVA || m_bUseFFmpeg;
					break;
				case CODEC_ID_MSMPEG4V3 :
				case CODEC_ID_MSMPEG4V2 :
				case CODEC_ID_MSMPEG4V1 :
					bCodecActivated = (m_nActiveCodecs & MPCVD_MSMPEG4) != 0;
					break;
				case CODEC_ID_H264 :
					m_bUseDXVA = (m_nActiveCodecs & MPCVD_H264_DXVA) != 0;
					m_bUseFFmpeg = (m_nActiveCodecs & MPCVD_H264) != 0;
					bCodecActivated = m_bUseDXVA || m_bUseFFmpeg;
					break;
				case CODEC_ID_SVQ3 :
				case CODEC_ID_SVQ1 :
					bCodecActivated = (m_nActiveCodecs & MPCVD_SVQ3) != 0;
					break;
				case CODEC_ID_H263 :
					bCodecActivated = (m_nActiveCodecs & MPCVD_H263) != 0;
					break;
				case CODEC_ID_DIRAC  :
					bCodecActivated = (m_nActiveCodecs & MPCVD_DIRAC) != 0;
					break;
				case CODEC_ID_DVVIDEO  :
					bCodecActivated = (m_nActiveCodecs & MPCVD_DV) != 0;
					break;
				case CODEC_ID_THEORA :
					bCodecActivated = (m_nActiveCodecs & MPCVD_THEORA) != 0;
					break;
				case CODEC_ID_VC1 :
					m_bUseDXVA = (m_nActiveCodecs & MPCVD_VC1_DXVA) != 0;
					m_bUseFFmpeg = (m_nActiveCodecs & MPCVD_VC1) != 0;
					bCodecActivated = m_bUseDXVA || m_bUseFFmpeg;
					break;
				case CODEC_ID_AMV :
					bCodecActivated = (m_nActiveCodecs & MPCVD_AMVV) != 0;
					break;
				case CODEC_ID_LAGARITH :
					bCodecActivated = (m_nActiveCodecs & MPCVD_LAGARITH) != 0;
					break;
				case CODEC_ID_VP3  :
				case CODEC_ID_VP5  :
				case CODEC_ID_VP6  :
				case CODEC_ID_VP6A :
					bCodecActivated = (m_nActiveCodecs & MPCVD_VP356) != 0;
					break;
				case CODEC_ID_VP8  :
					bCodecActivated = (m_nActiveCodecs & MPCVD_VP8) != 0;
					break;
				case CODEC_ID_MJPEG  :
				case CODEC_ID_MJPEGB :
					bCodecActivated = (m_nActiveCodecs & MPCVD_MJPEG) != 0;
					break;
				case CODEC_ID_INDEO3 :
				case CODEC_ID_INDEO4 :
				case CODEC_ID_INDEO5 :
					bCodecActivated = (m_nActiveCodecs & MPCVD_INDEO) != 0;
					break;
				case CODEC_ID_UTVIDEO  :
					bCodecActivated = (m_nActiveCodecs & MPCVD_UTVD) != 0;
					break;
				case CODEC_ID_CSCD     :
				case CODEC_ID_QTRLE    :
				case CODEC_ID_TSCC     :
					bCodecActivated = (m_nActiveCodecs & MPCVD_SCREC) != 0;
					break;
				case CODEC_ID_RV10 :
				case CODEC_ID_RV20 :
				case CODEC_ID_RV30 :
				case CODEC_ID_RV40 :
					bCodecActivated = (m_nActiveCodecs & MPCVD_RV) != 0;
					break;
				case CODEC_ID_MPEG2VIDEO :
					m_bUseDXVA = (m_nActiveCodecs & MPCVD_MPEG2_DXVA) != 0;
					m_bUseFFmpeg = false;
					bCodecActivated = m_bUseDXVA;
					break;
				case CODEC_ID_PRORES :
					bCodecActivated = (m_nActiveCodecs & MPCVD_PRORES) != 0;
					break;
				case CODEC_ID_BINKVIDEO :
					bCodecActivated = (m_nActiveCodecs & MPCVD_BINKV) != 0;
					break;
				case CODEC_ID_PNG :
					bCodecActivated = (m_nActiveCodecs & MPCVD_PNG) != 0;
					break;
			}
			return (bCodecActivated ? i : -1);
#endif
		}

	return -1;
}

void CMPCVideoDecFilter::Cleanup()
{
	SAFE_DELETE (m_pDXVADecoder);

	// Release FFMpeg
	if (m_pAVCtx) {
		if (m_pAVCtx->extradata) {
			av_freep(&m_pAVCtx->extradata);
		}
		if (m_pFFBuffer) {
			av_freep(&m_pFFBuffer);
		}
		m_nFFBufferSize = 0;
		if (m_pAlignedFFBuffer) {
			av_freep(&m_pAlignedFFBuffer);
		}
		m_nAlignedFFBufferSize = 0;

		if (m_pAVCtx->codec) {
			avcodec_close(m_pAVCtx);
		}

		// Free thread resource if necessary
		FFSetThreadNumber (m_pAVCtx, m_pAVCtx->codec_id, 0);

		av_freep(&m_pAVCtx);
	}

	if (m_pFrame) {
		av_freep(&m_pFrame);
	}

	if (m_pSwsContext) {
		sws_freeContext(m_pSwsContext);
		m_pSwsContext	= NULL;
		m_PixFmt		= PIX_FMT_NB;
	}

	m_pAVCodec		= NULL;
	m_pAVCtx		= NULL;
	m_pFrame		= NULL;
	m_pFFBuffer		= NULL;
	m_nFFBufferSize	= 0;
	m_nFFBufferPos	= 0;
	m_nFFPicEnd		= INT_MIN;
	m_nCodecNb		= -1;
	m_nCodecId		= CODEC_ID_NONE;
	SAFE_DELETE_ARRAY (m_pVideoOutputFormat);

	// Release DXVA ressources
	if (m_hDevice != INVALID_HANDLE_VALUE) {
		m_pDeviceManager->CloseDeviceHandle(m_hDevice);
		m_hDevice = INVALID_HANDLE_VALUE;
	}

	m_pDeviceManager		= NULL;
	m_pDecoderService		= NULL;
	m_pDecoderRenderTarget	= NULL;
}

void CMPCVideoDecFilter::CalcAvgTimePerFrame()
{
	CMediaType &mt = m_pInput->CurrentMediaType();
	if (mt.formattype==FORMAT_VideoInfo) {
		m_rtAvrTimePerFrame = ((VIDEOINFOHEADER*)mt.pbFormat)->AvgTimePerFrame;
	} else if (mt.formattype==FORMAT_VideoInfo2) {
		m_rtAvrTimePerFrame = ((VIDEOINFOHEADER2*)mt.pbFormat)->AvgTimePerFrame;
	} else if (mt.formattype==FORMAT_MPEGVideo) {
		m_rtAvrTimePerFrame = ((MPEG1VIDEOINFO*)mt.pbFormat)->hdr.AvgTimePerFrame;
	} else if (mt.formattype==FORMAT_MPEG2Video) {
		m_rtAvrTimePerFrame = ((MPEG2VIDEOINFO*)mt.pbFormat)->hdr.AvgTimePerFrame;
	} else {
		ASSERT (FALSE);
		m_rtAvrTimePerFrame	= 1;
	}

	m_rtAvrTimePerFrame = max (1, m_rtAvrTimePerFrame);
}

void CMPCVideoDecFilter::LogLibAVCodec(void* par, int level, const char *fmt, va_list valist)
{
#if defined(_DEBUG) && 0
	char		Msg [500];
	vsnprintf_s (Msg, sizeof(Msg), _TRUNCATE, fmt, valist);
	TRACE("AVLIB : %s", Msg);
#endif
}

void CMPCVideoDecFilter::OnGetBuffer(AVFrame *pic)
{
	// Callback from FFMpeg to store Ref Time in frame (needed to have correct rtStart after avcodec_decode_video calls)
	//	pic->rtStart	= m_rtStart;
}

STDMETHODIMP CMPCVideoDecFilter::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	return
		QI(IMPCVideoDecFilter)
		QI(IMPCVideoDecFilter2)
		QI(IMPCVideoDecFilterCodec)
		QI(ISpecifyPropertyPages)
		QI(ISpecifyPropertyPages2)
		__super::NonDelegatingQueryInterface(riid, ppv);
}



HRESULT CMPCVideoDecFilter::CheckInputType(const CMediaType* mtIn)
{
	for (int i=0; i<_countof(sudPinTypesIn); i++) {
		if ((mtIn->majortype == *sudPinTypesIn[i].clsMajorType) &&
				(mtIn->subtype == *sudPinTypesIn[i].clsMinorType)) {
			return S_OK;
		}
	}

	return VFW_E_TYPE_NOT_ACCEPTED;
}

bool CMPCVideoDecFilter::IsAVI()
{
	CString ext, fname;

	BeginEnumFilters(m_pGraph, pEF, pBF) {
		CComQIPtr<IFileSourceFilter> pFSF = pBF;
		if (pFSF) {
			LPOLESTR pFN = NULL;
			AM_MEDIA_TYPE mt;
			if (SUCCEEDED(pFSF->GetCurFile(&pFN, &mt)) && pFN && *pFN) {
				fname	= CString(pFN);
				ext		= CPath(fname).GetExtension().MakeLower();
				CoTaskMemFree(pFN);
			}
			break;
		}
	}
	EndEnumFilters

	if (ext == _T(".avi")) {
		CFile f;
		CFileException fileException;
		if (!f.Open(fname, CFile::modeRead|CFile::typeBinary|CFile::shareDenyNone, &fileException)) {
			TRACE(_T("CMPCVideoDecFilter::IsAVI() : Can't open file %ws, error = %u\n"), fname, fileException.m_cause);
			return false;
		}

		DWORD SYNC = 0;
		if (f.Read(&SYNC, sizeof(SYNC)) != sizeof(SYNC)) {
			return false;
		}

		if (SYNC == MAKEFOURCC('R','I','F','F')) {
			return true;
		}
	}

	return false;
}

#define ATI_IDENTIFY		_T("ATI ")
#define AMD_IDENTIFY		_T("AMD ")
#define RADEON_HD_IDENTIFY	_T("Radeon HD ")

HRESULT CMPCVideoDecFilter::SetMediaType(PIN_DIRECTION direction,const CMediaType *pmt)
{
	if (direction == PINDIR_INPUT) {

		int nNewCodec = FindCodec(pmt);

		if (nNewCodec == -1) {
			return VFW_E_TYPE_NOT_ACCEPTED;
		}

		if (nNewCodec != m_nCodecNb) {
			m_nCodecNb	= nNewCodec;
			m_nCodecId	= ffCodecs[nNewCodec].nFFCodec;

			CLSID ClsidSourceFilter = GetCLSID(m_pInput->GetConnected());
			if ((ClsidSourceFilter == __uuidof(CMpegSourceFilter)) || (ClsidSourceFilter == __uuidof(CMpegSplitterFilter))) {
				if (CComPtr<IBaseFilter> pFilter = GetFilterFromPin(m_pInput->GetConnected()) ) {
					if (CComQIPtr<IMpegSplitterFilter> MpegSplitterFilter = pFilter ) {
						m_bIsEVO = (m_nCodecId == CODEC_ID_VC1 && mpeg_ps == MpegSplitterFilter->GetMPEGType());
					}
				}
			}

			m_bReorderBFrame	= true;
			m_pAVCodec			= avcodec_find_decoder(m_nCodecId);
			CheckPointer (m_pAVCodec, VFW_E_UNSUPPORTED_VIDEO);

			m_pAVCtx	= avcodec_alloc_context3(m_pAVCodec);
			CheckPointer (m_pAVCtx, E_POINTER);

			int nThreadNumber = m_nThreadNumber ? m_nThreadNumber : m_pCpuId->GetProcessorNumber() * 3/2;
			if ((nThreadNumber > 1) && FFGetThreadType(m_nCodecId)) {
				FFSetThreadNumber(m_pAVCtx, m_nCodecId, IsDXVASupported() ? 1 : nThreadNumber);
			}

			m_pFrame = avcodec_alloc_frame();
			CheckPointer (m_pFrame,	  E_POINTER);

			m_h264RandomAccess.flush(m_pAVCtx->thread_count);

			if (pmt->formattype == FORMAT_VideoInfo) {
				VIDEOINFOHEADER* vih			= (VIDEOINFOHEADER*)pmt->pbFormat;
				m_pAVCtx->width					= vih->bmiHeader.biWidth;
				m_pAVCtx->height				= abs(vih->bmiHeader.biHeight);
				m_pAVCtx->codec_tag				= vih->bmiHeader.biCompression;
				m_pAVCtx->bits_per_coded_sample = vih->bmiHeader.biBitCount;
			} else if (pmt->formattype == FORMAT_VideoInfo2) {
				VIDEOINFOHEADER2* vih2			= (VIDEOINFOHEADER2*)pmt->pbFormat;
				m_pAVCtx->width					= vih2->bmiHeader.biWidth;
				m_pAVCtx->height				= abs(vih2->bmiHeader.biHeight);
				m_pAVCtx->codec_tag				= vih2->bmiHeader.biCompression;
				m_pAVCtx->bits_per_coded_sample = vih2->bmiHeader.biBitCount;
			} else if (pmt->formattype == FORMAT_MPEGVideo) {
				MPEG1VIDEOINFO* mpgv			= (MPEG1VIDEOINFO*)pmt->pbFormat;
				m_pAVCtx->width					= mpgv->hdr.bmiHeader.biWidth;
				m_pAVCtx->height				= abs(mpgv->hdr.bmiHeader.biHeight);
				m_pAVCtx->codec_tag				= mpgv->hdr.bmiHeader.biCompression;
				m_pAVCtx->bits_per_coded_sample = mpgv->hdr.bmiHeader.biBitCount;
			} else if (pmt->formattype == FORMAT_MPEG2Video) {
				MPEG2VIDEOINFO* mpg2v			= (MPEG2VIDEOINFO*)pmt->pbFormat;
				m_pAVCtx->width					= mpg2v->hdr.bmiHeader.biWidth;
				m_pAVCtx->height				= abs(mpg2v->hdr.bmiHeader.biHeight);
				m_pAVCtx->codec_tag				= mpg2v->hdr.bmiHeader.biCompression;
				m_pAVCtx->bits_per_coded_sample = mpg2v->hdr.bmiHeader.biBitCount;

				if (mpg2v->hdr.bmiHeader.biCompression == NULL) {
					m_pAVCtx->codec_tag			= pmt->subtype.Data1;
				} else if ( (m_pAVCtx->codec_tag == MAKEFOURCC('a','v','c','1')) || (m_pAVCtx->codec_tag == MAKEFOURCC('A','V','C','1'))) {
					m_pAVCtx->nal_length_size	= mpg2v->dwFlags;
					m_bReorderBFrame			= IsAVI() ? true : false;
				} else if ( (m_pAVCtx->codec_tag == MAKEFOURCC('m','p','4','v')) || (m_pAVCtx->codec_tag == MAKEFOURCC('M','P','4','V'))) {
					m_bReorderBFrame			= false;
				}
			} else {
				return VFW_E_INVALIDMEDIATYPE;
			}
			m_nWidth	= m_pAVCtx->width;
			m_nHeight	= m_pAVCtx->height;

			if (m_pAVCtx->codec_tag == MAKEFOURCC('m','p','g','2')) {
				m_pAVCtx->codec_tag = MAKEFOURCC('M','P','E','G');
			}

			if (m_nCodecId == CODEC_ID_RV10 || m_nCodecId == CODEC_ID_RV20 || m_nCodecId == CODEC_ID_RV30 || m_nCodecId == CODEC_ID_RV40) {
				m_bReorderBFrame = false;
			}

			m_pAVCtx->codec_id              = m_nCodecId;
			m_pAVCtx->workaround_bugs		= m_nWorkaroundBug;
			m_pAVCtx->error_concealment		= m_nErrorConcealment;
			m_pAVCtx->err_recognition		= m_nErrorRecognition;
			m_pAVCtx->idct_algo				= m_nIDCTAlgo;
			m_pAVCtx->skip_loop_filter		= (AVDiscard)m_nDiscardMode;

			m_pAVCtx->opaque				= this;
			m_pAVCtx->get_buffer			= get_buffer;

			if (m_nCodecId == CODEC_ID_H264) {
				m_pAVCtx->flags2			|= CODEC_FLAG2_SHOW_ALL;
			}

			m_pAVCtx->mpeg2_using_dxva		= (m_nCodecId == CODEC_ID_MPEG2VIDEO);

			AllocExtradata (m_pAVCtx, pmt);
			ConnectTo (m_pAVCtx);
			CalcAvgTimePerFrame();

			if (avcodec_open2(m_pAVCtx, m_pAVCodec, NULL)<0) {
				return VFW_E_INVALIDMEDIATYPE;
			}

			FFGetOutputSize(m_pAVCtx, m_pFrame, &m_nOutputWidth, &m_nOutputHeight);

			if (IsDXVASupported()) {
				do {
					m_bDXVACompatible = false;

					if (!DXVACheckFramesize(PictWidth(), PictHeight(), m_nPCIVendor)) { // check frame size
						break;
					}

					if (m_nCodecId == CODEC_ID_H264) {
						if (m_nDXVA_SD && PictWidthRounded() < 1280) { // check "Disable DXVA for SD" option
							break;
						}

						bool IsAtiDXVACompatible = false;
						if (m_nPCIVendor == PCIV_ATI) {
							if (!m_strDeviceDescription.Find(ATI_IDENTIFY) || !m_strDeviceDescription.Find(AMD_IDENTIFY)) {
								m_strDeviceDescription.Delete(0, 4);
								if (!m_strDeviceDescription.Find(RADEON_HD_IDENTIFY)) {
									TCHAR ati_version = m_strDeviceDescription.GetAt(CString(RADEON_HD_IDENTIFY).GetLength());
									IsAtiDXVACompatible = (atoi(&ati_version) >= 4); // HD4xxx and above AMD/ATI cards support level 5.1 and ref = 16
								}
							}
						} else if (m_nPCIVendor == PCIV_Intel && !IsWinVistaOrLater() && m_nPCIDevice == 0x8108) {
							break; // Disable support H.264 DXVA on Intel GMA500 in WinXP
						}
						int nCompat = FFH264CheckCompatibility (PictWidthRounded(), PictHeightRounded(), m_pAVCtx, (BYTE*)m_pAVCtx->extradata, m_pAVCtx->extradata_size, m_nPCIVendor, m_nPCIDevice, m_VideoDriverVersion, IsAtiDXVACompatible);
						if (nCompat && (
								nCompat == DXVA_HIGH_BIT ||												// unsupported video
								m_nDXVACheckCompatibility == 0 ||										// full check
								m_nDXVACheckCompatibility == 1 && nCompat != DXVA_UNSUPPORTED_LEVEL ||	// skip level check
								m_nDXVACheckCompatibility == 2 && nCompat != DXVA_TOO_MANY_REF_FRAMES)	// skip reference frame check
							) {
							break;
						}
					} else if (m_nCodecId == CODEC_ID_MPEG2VIDEO) {
						if (!MPEG2CheckCompatibility(m_pAVCtx, m_pFrame)) {
							break;
						}
					} else if (m_nCodecId == CODEC_ID_WMV3) {
						if (PictWidth() <= 720) { // fixes color problem for some wmv files (profile <= MP@ML)
							break;
						}
					}

					m_bDXVACompatible = true;
				} while (false);

				if (!m_bDXVACompatible) { // reset the threads count
					m_bUseDXVA = false;
					if ((nThreadNumber > 1) && FFGetThreadType(m_nCodecId)) {
						avcodec_close (m_pAVCtx);
						FFSetThreadNumber(m_pAVCtx, m_nCodecId, nThreadNumber);
						if (avcodec_open2(m_pAVCtx, m_pAVCodec, NULL)<0) {
							return VFW_E_INVALIDMEDIATYPE;
						}
					}
				}

			}

			BuildDXVAOutputFormat();
		}
	}

	return __super::SetMediaType(direction, pmt);
}

VIDEO_OUTPUT_FORMATS DXVAFormats[] = { // DXVA2
	{&MEDIASUBTYPE_NV12, 1, 12, 'avxd'},
	{&MEDIASUBTYPE_NV12, 1, 12, 'AVXD'},
	{&MEDIASUBTYPE_NV12, 1, 12, 'AVxD'},
	{&MEDIASUBTYPE_NV12, 1, 12, 'AvXD'}
};

// === New swscaler options
VIDEO_OUTPUT_FORMATS SoftwareFormats[] = { // Software
	{&MEDIASUBTYPE_NV12,   2, 12, '21VN'},
	{&MEDIASUBTYPE_YV12,   3, 12, '21VY'},
	{&MEDIASUBTYPE_YUY2,   1, 16, '2YUY'},
	{&MEDIASUBTYPE_RGB32,  1, 32, BI_RGB}, //1
	{&MEDIASUBTYPE_RGB565, 1, 16, BI_BITFIELDS},
	{&MEDIASUBTYPE_RGB555, 1, 16, BI_RGB}
};

bool CMPCVideoDecFilter::IsDXVASupported()
{
	if (m_nCodecNb != -1) {
		// Does the codec suppport DXVA ?
		if (ffCodecs[m_nCodecNb].DXVAModes != NULL) {
			// Enabled by user ?
			if (m_bUseDXVA) {
				// is the file compatible ?
				if (m_bDXVACompatible) {
					return true;
				}
			}
		}
	}
	return false;
}

void CMPCVideoDecFilter::BuildDXVAOutputFormat()
{
	SAFE_DELETE_ARRAY (m_pVideoOutputFormat);

	// === New swscaler options
	int nSwOF = 0;
	int	nSwIndex[6];
	int nSwCount = 0;
	// get the output formats order from the DWORD nibbles extracting literal values [0x00543210] = 0,1,2,3,4,5
	// get the output formats count from the DWORD nibbles extracting checked flag (8) [0x0054ba98] = 4
	for (int i=0; i<6; i++) {
		nSwOF = ( m_nSwOutputFormats & (0x0000000F << (4*i)) ) >> (4*i);
		if ((nSwOF & 8) !=0) {
			nSwIndex[nSwCount]=nSwOF & ~8;
			nSwCount++;
		}
	}
	m_nVideoOutputCount = (IsDXVASupported() ? ffCodecs[m_nCodecNb].DXVAModeCount() + _countof (DXVAFormats) : 0) +
						  (m_bUseFFmpeg   ? (nSwCount>0) ? nSwCount : _countof(SoftwareFormats) : 0);

	m_pVideoOutputFormat = DNew VIDEO_OUTPUT_FORMATS[m_nVideoOutputCount];

	int nPos = 0;
	if (IsDXVASupported()) {
		// Dynamic DXVA media types for DXVA1
		for (nPos=0; nPos<ffCodecs[m_nCodecNb].DXVAModeCount(); nPos++) {
			m_pVideoOutputFormat[nPos].subtype			= ffCodecs[m_nCodecNb].DXVAModes->Decoder[nPos];
			m_pVideoOutputFormat[nPos].biCompression	= 'avxd';
			m_pVideoOutputFormat[nPos].biBitCount		= 12;
			m_pVideoOutputFormat[nPos].biPlanes			= 1;
		}

		// Static list for DXVA2
		memcpy (&m_pVideoOutputFormat[nPos], DXVAFormats, sizeof(DXVAFormats));
		nPos += _countof (DXVAFormats);
	}
	// Software rendering
	if (m_bUseFFmpeg) {
		if (nSwCount>0) {
			for (int i=0; i<nSwCount; i++) {
				m_pVideoOutputFormat[nPos + i] = SoftwareFormats[nSwIndex[i]];
			}
		} else {
			memcpy (&m_pVideoOutputFormat[nPos], SoftwareFormats, sizeof(SoftwareFormats));
		}
	}
}

int CMPCVideoDecFilter::GetPicEntryNumber()
{
	if (IsDXVASupported()) {
		return ffCodecs[m_nCodecNb].DXVAModes->PicEntryNumber;
	} else {
		return 0;
	}
}

void CMPCVideoDecFilter::GetOutputFormats (int& nNumber, VIDEO_OUTPUT_FORMATS** ppFormats)
{
	nNumber		= m_nVideoOutputCount;
	*ppFormats	= m_pVideoOutputFormat;
}

void CMPCVideoDecFilter::AllocExtradata(AVCodecContext* pAVCtx, const CMediaType* pmt)
{
	// code from LAV ...
	// Process Extradata
	BYTE *extra = NULL;
	unsigned int extralen = 0;
	getExtraData((const BYTE *)pmt->Format(), pmt->FormatType(), pmt->FormatLength(), NULL, &extralen);

	BOOL bH264avc = FALSE;
	if (extralen > 0) {
		TRACE(_T("CMPCVideoDecFilter::AllocExtradata() : processing extradata of %d bytes"), extralen);
		// Reconstruct AVC1 extradata format
		if (pmt->formattype == FORMAT_MPEG2Video && (m_pAVCtx->codec_tag == MAKEFOURCC('a','v','c','1') || m_pAVCtx->codec_tag == MAKEFOURCC('A','V','C','1') || m_pAVCtx->codec_tag == MAKEFOURCC('C','C','V','1'))) {
			MPEG2VIDEOINFO *mp2vi = (MPEG2VIDEOINFO *)pmt->Format();
			extralen += 7;
			extra = (uint8_t *)av_mallocz(extralen + FF_INPUT_BUFFER_PADDING_SIZE);
			extra[0] = 1;
			extra[1] = (BYTE)mp2vi->dwProfile;
			extra[2] = 0;
			extra[3] = (BYTE)mp2vi->dwLevel;
			extra[4] = (BYTE)(mp2vi->dwFlags ? mp2vi->dwFlags : 2) - 1;

			// Actually copy the metadata into our new buffer
			unsigned int actual_len;
			getExtraData((const BYTE *)pmt->Format(), pmt->FormatType(), pmt->FormatLength(), extra+6, &actual_len);

			// Count the number of SPS/PPS in them and set the length
			// We'll put them all into one block and add a second block with 0 elements afterwards
			// The parsing logic does not care what type they are, it just expects 2 blocks.
			BYTE *p = extra+6, *end = extra+6+actual_len;
			BOOL bSPS = FALSE, bPPS = FALSE;
			int count = 0;
			while (p+1 < end) {
				unsigned len = (((unsigned)p[0] << 8) | p[1]) + 2;
				if (p + len > end) {
					break;
				}
				if ((p[2] & 0x1F) == 7)
					bSPS = TRUE;
				if ((p[2] & 0x1F) == 8)
					bPPS = TRUE;
				count++;
				p += len;
			}
			extra[5] = count;
			extra[extralen-1] = 0;

			bH264avc = TRUE;
			if (!bSPS) {
				TRACE(_T("CMPCVideoDecFilter::AllocExtradata() : AVC1 extradata doesn't contain a SPS, setting thread_count = 1"));
				m_pAVCtx->thread_count = 1;
			}
		} else {
			// Just copy extradata for other formats
			extra = (uint8_t *)av_mallocz(extralen + FF_INPUT_BUFFER_PADDING_SIZE);
			getExtraData((const BYTE *)pmt->Format(), pmt->FormatType(), pmt->FormatLength(), extra, NULL);
		}
		// Hack to discard invalid MP4 metadata with AnnexB style video
		if (m_nCodecId == CODEC_ID_H264 && !bH264avc && extra[0] == 1) {
			av_freep(&extra);
			extralen = 0;
		}
		m_pAVCtx->extradata = extra;
		m_pAVCtx->extradata_size = (int)extralen;
	}
}

HRESULT CMPCVideoDecFilter::CompleteConnect(PIN_DIRECTION direction, IPin* pReceivePin)
{
	LOG(_T("CMPCVideoDecFilter::CompleteConnect"));

	if (direction == PINDIR_INPUT && m_pOutput->IsConnected()) {
		ReconnectOutput (m_nWidth, m_nHeight);
	} else if (direction == PINDIR_OUTPUT) {
		DetectVideoCard_EVR(pReceivePin);

		if (IsDXVASupported()) {
			if (m_nDXVAMode == MODE_DXVA1) {
				m_pDXVADecoder->ConfigureDXVA1();
			} else if (SUCCEEDED (ConfigureDXVA2 (pReceivePin)) && SUCCEEDED (SetEVRForDXVA2 (pReceivePin)) ) {
				m_nDXVAMode  = MODE_DXVA2;
			}
		}
		if (m_nDXVAMode == MODE_SOFTWARE && (!m_bUseFFmpeg || !FFSoftwareCheckCompatibility(m_pAVCtx))) {
			return VFW_E_INVALIDMEDIATYPE;
		}

		if (m_nDXVAMode == MODE_SOFTWARE && IsDXVASupported()) { // reset the threads count
			int nThreadNumber = m_nThreadNumber ? m_nThreadNumber : m_pCpuId->GetProcessorNumber() * 3/2;
			m_bUseDXVA = false;
			if ((nThreadNumber > 1) && FFGetThreadType(m_nCodecId)) {
				avcodec_close (m_pAVCtx);
				FFSetThreadNumber(m_pAVCtx, m_nCodecId, nThreadNumber);
				if (avcodec_open2(m_pAVCtx, m_pAVCodec, NULL)<0) {
					return VFW_E_INVALIDMEDIATYPE;
				}
			}
		}

		CLSID ClsidSourceFilter = GetCLSID(m_pInput->GetConnected());
		if ((ClsidSourceFilter == __uuidof(CMpegSourceFilter)) || (ClsidSourceFilter == __uuidof(CMpegSplitterFilter))) {
			m_bReorderBFrame = false;
		}

		if (m_nDXVAMode != MODE_SOFTWARE) {
			m_nOutCsp = FF_CSP_UNSUPPORTED;
		}
		
		// Cannot use YUY2 if horizontal or vertical resolution is not even
		if (((m_pOutput->CurrentMediaType().subtype == MEDIASUBTYPE_YUY2) && (m_pAVCtx->width&1 || m_pAVCtx->height&1))) {
			return VFW_E_INVALIDMEDIATYPE;
		}
	}

	return __super::CompleteConnect (direction, pReceivePin);
}

HRESULT CMPCVideoDecFilter::DecideBufferSize(IMemAllocator* pAllocator, ALLOCATOR_PROPERTIES* pProperties)
{
	if (UseDXVA2()) {
		HRESULT					hr;
		ALLOCATOR_PROPERTIES	Actual;

		if (m_pInput->IsConnected() == FALSE) {
			return E_UNEXPECTED;
		}

		pProperties->cBuffers = GetPicEntryNumber();

		if (FAILED(hr = pAllocator->SetProperties(pProperties, &Actual))) {
			return hr;
		}

		return pProperties->cBuffers > Actual.cBuffers || pProperties->cbBuffer > Actual.cbBuffer
			   ? E_FAIL
			   : NOERROR;
	} else {
		return __super::DecideBufferSize (pAllocator, pProperties);
	}
}

HRESULT CMPCVideoDecFilter::BeginFlush()
{
	return __super::BeginFlush();
}

HRESULT CMPCVideoDecFilter::EndFlush()
{
	CAutoLock cAutoLock(&m_csReceive);
	return __super::EndFlush();
}

HRESULT CMPCVideoDecFilter::NewSegment(REFERENCE_TIME rtStart, REFERENCE_TIME rtStop, double dRate)
{
	CAutoLock cAutoLock(&m_csReceive);
	
	if (m_pAVCtx) {
		avcodec_flush_buffers (m_pAVCtx);
	}

	if (m_pDXVADecoder) {
		m_pDXVADecoder->Flush();
	}
	
	m_nPosB = 1;
	memset (&m_BFrames, 0, sizeof(m_BFrames));
	m_rtLastStart		= 0;
	m_nCountEstimated	= 0;
	m_dRate				= dRate;

	ResetBuffer();

	m_h264RandomAccess.flush (m_pAVCtx->thread_count);

	m_bWaitingForKeyFrame = TRUE;

	m_rtPrevStop = 0;

	rm.video_after_seek	= true;
	m_rtStart			= rtStart;

	return __super::NewSegment (rtStart, rtStop, dRate);
}

HRESULT CMPCVideoDecFilter::EndOfStream()
{
	CAutoLock cAutoLock(&m_csReceive);

	if (m_nDXVAMode == MODE_SOFTWARE) {
		REFERENCE_TIME rtStart = 0, rtStop = 0;
		SoftwareDecode(NULL, NULL, 0, rtStart, rtStop);
	} else if (m_nDXVAMode == MODE_DXVA2) { // TODO - need check under WinXP on DXVA1
		m_pDXVADecoder->EndOfStream();
	}

	return __super::EndOfStream();
}

HRESULT CMPCVideoDecFilter::BreakConnect(PIN_DIRECTION dir)
{
	if (dir == PINDIR_INPUT) {
		Cleanup();
	}

	return __super::BreakConnect (dir);
}

void CMPCVideoDecFilter::SetTypeSpecificFlags(IMediaSample* pMS)
{
	if (CComQIPtr<IMediaSample2> pMS2 = pMS) {
		AM_SAMPLE2_PROPERTIES props;
		if (SUCCEEDED(pMS2->GetProperties(sizeof(props), (BYTE*)&props))) {
			props.dwTypeSpecificFlags &= ~0x7f;

			m_nFrameType = PICT_BOTTOM_FIELD;
			if (!m_pFrame->interlaced_frame) {
				props.dwTypeSpecificFlags	|= AM_VIDEO_FLAG_WEAVE;
				m_nFrameType				= PICT_FRAME;
			} else {
				if (m_pFrame->top_field_first) {
					props.dwTypeSpecificFlags	|= AM_VIDEO_FLAG_FIELD1FIRST;
					m_nFrameType				= PICT_TOP_FIELD;
				}
			}

			switch (m_pFrame->pict_type) {
				case AV_PICTURE_TYPE_I :
				case AV_PICTURE_TYPE_SI :
					props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_I_SAMPLE;
					break;
				case AV_PICTURE_TYPE_P :
				case AV_PICTURE_TYPE_SP :
					props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_P_SAMPLE;
					break;
				default :
					props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_B_SAMPLE;
					break;
			}

			pMS2->SetProperties(sizeof(props), (BYTE*)&props);
		}
	}
}

unsigned __int64 CMPCVideoDecFilter::GetCspFromMediaType(GUID& subtype)
{
	// === New swscaler options
	if (subtype == MEDIASUBTYPE_I420 || subtype == MEDIASUBTYPE_IYUV || subtype == MEDIASUBTYPE_YV12) {
		return (FF_CSP_420P|FF_CSP_FLAGS_YUV_ADJ);
	} else if (subtype == MEDIASUBTYPE_NV12) {
		return FF_CSP_NV12;
	} else if (subtype == MEDIASUBTYPE_YUY2) {
		return FF_CSP_YUY2;
	} else if (subtype == MEDIASUBTYPE_RGB32) {
		return FF_CSP_RGB32;
	} else if (subtype == MEDIASUBTYPE_RGB565) {
		return FF_CSP_RGB16;
	} else if (subtype == MEDIASUBTYPE_RGB555) {
		return FF_CSP_RGB15;
	}
	//
	ASSERT (FALSE);
	return FF_CSP_NULL;
}

void CMPCVideoDecFilter::InitSwscale()
{
	if (m_pSwsContext == NULL) {
		int sws_FlagsR = 0;
		int sws_FlagsO = 0;

		BITMAPINFOHEADER bihOut;
		ExtractBIH(&m_pOutput->CurrentMediaType(), &bihOut);

		switch (m_nSwChromaToRGB) {
			case 0  :										// GUI 'Fast'
				sws_FlagsO = SWS_ACCURATE_RND;
				break;
			case 1  :										// GUI 'Normal'
			default :
				sws_FlagsO = SWS_FULL_CHR_H_INP | SWS_ACCURATE_RND;
				break;
			case 2  :										// GUI 'Full'
				sws_FlagsO = SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP | SWS_ACCURATE_RND;
				break;
		}

		switch (m_nSwResizeMethodBE) {
			case 0  :										// GUI 'Area'
				sws_FlagsR = SWS_AREA;
				break;
			case 1  :										// GUI 'Bicubic'
				sws_FlagsR = SWS_BICUBIC;
				break;
			case 2  :										// GUI 'Bilinear'
			default :
				sws_FlagsR = SWS_BILINEAR;
				break;
			case 3  :										// GUI 'Fast Bilinear'
				sws_FlagsR = SWS_FAST_BILINEAR;
				break;
			case 4  :										// GUI 'Gauss'
				sws_FlagsR = SWS_GAUSS;
				break;
			case 5  :										// GUI 'Lanczos'
				sws_FlagsR = SWS_LANCZOS;
				break;
			case 6  :										// GUI 'Point'
				sws_FlagsR = SWS_POINT;
				break;
			case 7  :										// GUI 'Sinc'
				sws_FlagsR = SWS_SINC;
				break;
			case 8  :										// GUI 'Spline'
				sws_FlagsR = SWS_SPLINE;
				break;
			case 9  :										// GUI 'X'
				sws_FlagsR = SWS_X;
				break;
		}
		
		int sws_Flags = sws_FlagsR | sws_FlagsO;
		
		m_nOutCsp = GetCspFromMediaType(m_pOutput->CurrentMediaType().subtype);

		if (m_nDialogHWND) {
			EnableWindow(GetDlgItem(m_nDialogHWND, IDC_PP_RESIZEMETHODBE), TRUE);
			EnableWindow(GetDlgItem(m_nDialogHWND, IDC_PP_SWCHROMATORGB), (m_nOutCsp == 0 || csp_isRGB_RGB(m_nOutCsp)));
			EnableWindow(GetDlgItem(m_nDialogHWND, IDC_PP_SWCOLORSPACE), (m_nOutCsp == 0 || csp_isRGB_RGB(m_nOutCsp)));
			EnableWindow(GetDlgItem(m_nDialogHWND, IDC_PP_SWINPUTLEVELS), (m_nOutCsp == 0 || csp_isRGB_RGB(m_nOutCsp)));
			EnableWindow(GetDlgItem(m_nDialogHWND, IDC_PP_SWOUTPUTLEVELS), (m_nOutCsp == 0 || csp_isRGB_RGB(m_nOutCsp)));
		}

		m_PixFmt = csp_ffdshow2lavc(csp_lavc2ffdshow(m_pAVCtx->pix_fmt));
		if (m_PixFmt == PIX_FMT_NB) {
			m_PixFmt = m_pAVCtx->pix_fmt;
		}

		m_pSwsContext = sws_getCachedContext(
							NULL,
							m_pAVCtx->width,
							m_pAVCtx->height,
							m_PixFmt,
							m_pAVCtx->width,
							m_pAVCtx->height,
							csp_ffdshow2lavc(m_nOutCsp),
							sws_Flags | SWS_PRINT_INFO,
							NULL,
							NULL,
							NULL);

		if (m_pSwsContext == NULL) {
			m_PixFmt = PIX_FMT_NB;
			return;
		}

		m_nSwOutBpp		= bihOut.biBitCount;
		m_pOutSize.cx	= bihOut.biWidth;
		m_pOutSize.cy	= abs(bihOut.biHeight);

		int *inv_tbl = NULL, *tbl = NULL;
		int srcRange, dstRange, brightness, contrast, saturation;
		int ret = sws_getColorspaceDetails(m_pSwsContext, &inv_tbl, &srcRange, &tbl, &dstRange, &brightness, &contrast, &saturation);
		if (ret >= 0) {
			int nColorspace;
			if (m_nSwColorspace == 2) {
				nColorspace = PictWidthRounded() > 768 ? SWS_CS_ITU709 : SWS_CS_ITU601;	// GUI 'Auto'
			} else {
				nColorspace = m_nSwColorspace == 1 ? SWS_CS_ITU709 : SWS_CS_ITU601;	// GUI 'HD(BT.709)' : 'SD(BT.601)'
			}
		
			dstRange = m_nSwOutputLevels>1 ? 0 : m_nSwOutputLevels; // GUI 'Auto' = 'TV(16-235)'
			srcRange = m_nSwInputLevels>1 ? 0 : m_nSwInputLevels; // GUI 'Auto' = 'TV(16-235)'
			sws_setColorspaceDetails(m_pSwsContext, sws_getCoefficients(nColorspace), srcRange, tbl, dstRange, brightness, contrast, saturation);
		}
	}
}

#define RM_SKIP_BITS(n)	(buffer<<=n)
#define RM_SHOW_BITS(n)	((buffer)>>(32-(n)))
static int rm_fix_timestamp(uint8_t *buf, int64_t timestamp, enum CodecID nCodecId, int64_t *kf_base, int *kf_pts)
{
	uint8_t *s = buf + 1 + (*buf+1)*8;
	uint32_t buffer = (s[0]<<24) + (s[1]<<16) + (s[2]<<8) + s[3];
	uint32_t kf = timestamp;
	int pict_type;
	uint32_t orig_kf;

	if (nCodecId == CODEC_ID_RV30) {
		RM_SKIP_BITS(3);
		pict_type = RM_SHOW_BITS(2);
		RM_SKIP_BITS(2 + 7);
	} else {
		RM_SKIP_BITS(1);
		pict_type = RM_SHOW_BITS(2);
		RM_SKIP_BITS(2 + 7 + 3);
	}
	orig_kf = kf = RM_SHOW_BITS(13); // kf= 2*RM_SHOW_BITS(12);
	if (pict_type <= 1) {
		// I frame, sync timestamps:
		*kf_base = (int64_t)timestamp-kf;
		kf = timestamp;
	} else {
		// P/B frame, merge timestamps:
		int64_t tmp = (int64_t)timestamp - *kf_base;
		kf |= tmp&(~0x1fff); // combine with packet timestamp
		if (kf<tmp-4096) {
			kf += 8192;
		} else if (kf>tmp+4096) { // workaround wrap-around problems
			kf -= 8192;
		}
		kf += *kf_base;
	}
	if (pict_type != 3) { // P || I  frame -> swap timestamps
		uint32_t tmp=kf;
		kf = *kf_pts;
		*kf_pts = tmp;
	}

	return kf;
}

static int64_t process_rv_timestamp(RMDemuxContext *rm, enum CodecID nCodecId, uint8_t *buf, int64_t timestamp)
{
	if (rm->video_after_seek) {
		rm->kf_base = 0;
		rm->kf_pts = timestamp;
		rm->video_after_seek = false;
	}
	return rm_fix_timestamp(buf, timestamp, nCodecId, &rm->kf_base, &rm->kf_pts);
}

void copyPlane(BYTE *dstp, stride_t dst_pitch, const BYTE *srcp, stride_t src_pitch, int row_size, int height, bool flip = false)
{
	if (!flip) {
		for (int y=height; y>0; --y) {
			memcpy(dstp, srcp, row_size);
			dstp += dst_pitch;
			srcp += src_pitch;
		}
	} else {
		dstp += dst_pitch * (height - 1);
		for (int y=height; y>0; --y) {
			memcpy(dstp, srcp, row_size);
			dstp -= dst_pitch;
			srcp += src_pitch;
		}
	}
}

HRESULT CMPCVideoDecFilter::SoftwareDecode(IMediaSample* pIn, BYTE* pDataIn, int nSize, REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop)
{
	HRESULT			hr = S_OK;
	int				got_picture;
	int				used_bytes;
	BOOL			bFlush = (pDataIn == NULL);

	AVPacket		avpkt;
	av_init_packet(&avpkt);

	if (!bFlush && m_nCodecId == CODEC_ID_H264) {
		if (!m_h264RandomAccess.searchRecoveryPoint(m_pAVCtx, pDataIn, nSize)) {
			return S_OK;
		}
	}

	while (nSize > 0 || bFlush) {
		if (!bFlush) {
			if (nSize+FF_INPUT_BUFFER_PADDING_SIZE > m_nFFBufferSize) {
				m_nFFBufferSize	= nSize+FF_INPUT_BUFFER_PADDING_SIZE;
				m_pFFBuffer		= (BYTE*)av_realloc(m_pFFBuffer, m_nFFBufferSize);
				if (!m_pFFBuffer) {
					m_nFFBufferSize = 0;
					return E_FAIL;
				}
			}

			// Required number of additionally allocated bytes at the end of the input bitstream for decoding.
			// This is mainly needed because some optimized bitstream readers read
			// 32 or 64 bit at once and could read over the end.
			// Note: If the first 23 bits of the additional bytes are not 0, then damaged
			// MPEG bitstreams could cause overread and segfault.
			memcpy(m_pFFBuffer, pDataIn, nSize);
			memset(m_pFFBuffer+nSize,0,FF_INPUT_BUFFER_PADDING_SIZE);

			avpkt.data = m_pFFBuffer;
			avpkt.size = nSize;
			avpkt.pts  = rtStart;
			avpkt.dts  = rtStop;
			avpkt.flags = AV_PKT_FLAG_KEY;
		} else {
			avpkt.data = NULL;
			avpkt.size = 0;
		}

		used_bytes = avcodec_decode_video2 (m_pAVCtx, m_pFrame, &got_picture, &avpkt);

		if (used_bytes < 0) {
			return S_OK;
		}

		// Comment from LAV Video code:
		// When Frame Threading, we won't know how much data has been consumed, so it by default eats everything.
		// In addition, if no data got consumed, and no picture was extracted, the frame probably isn't all that useufl.
		// The MJPEB decoder is somewhat buggy and doesn't let us know how much data was consumed really...
		if ((m_pAVCtx->active_thread_type & FF_THREAD_FRAME || (!got_picture && used_bytes == 0)) || m_nCodecId == CODEC_ID_MJPEGB || bFlush) {
			nSize = 0;
		} else {
			nSize   -= used_bytes;
			pDataIn += used_bytes;
		}

		if (m_nCodecId == CODEC_ID_H264) {
			m_h264RandomAccess.judgeFrameUsability(m_pFrame, &got_picture);
		} else if (m_nCodecId == CODEC_ID_VC1 || m_nCodecId == CODEC_ID_RV30 || m_nCodecId == CODEC_ID_RV40) {
			if (m_bWaitingForKeyFrame && got_picture) {
				if (m_pFrame->key_frame) {
					m_bWaitingForKeyFrame = FALSE;
				} else {
					got_picture = 0;
				}
			}
		}

		if (!got_picture || !m_pFrame->data[0]) {
			bFlush = FALSE;
			continue;
		}

		if ((pIn && pIn->IsPreroll() == S_OK) || rtStart < 0) {
			return S_OK;
		}

		if (!m_bFrame_repeat_pict && m_pFrame->repeat_pict) {
			m_bFrame_repeat_pict = true;
		}

		CComPtr<IMediaSample>	pOut;
		BYTE*					pDataOut = NULL;

		UpdateAspectRatio();
		if (FAILED(hr = GetDeliveryBuffer(m_pAVCtx->width, m_pAVCtx->height, &pOut)) || FAILED(hr = pOut->GetPointer(&pDataOut))) {
			return hr;
		}

		if (m_nCodecId == CODEC_ID_THEORA || (m_nCodecId == CODEC_ID_VP8 && m_rtAvrTimePerFrame == 10000)) { // need more tests
			rtStart = m_pFrame->pkt_pts;
			rtStop = m_pFrame->pkt_dts;
		} else if ((m_nCodecId == CODEC_ID_RV10 || m_nCodecId == CODEC_ID_RV20) && m_pFrame->pict_type == AV_PICTURE_TYPE_B) {
			rtStart = m_rtPrevStop;
			rtStop = rtStart + m_rtAvrTimePerFrame;
		} else if ((m_nCodecId == CODEC_ID_RV30 || m_nCodecId == CODEC_ID_RV40) && avpkt.data) {
			rtStart = (rtStart == _I64_MIN) ? m_rtPrevStop : (10000i64*process_rv_timestamp(&rm, m_nCodecId, avpkt.data, (rtStart + m_rtStart)/10000) - m_rtStart);
			rtStop = rtStart + m_rtAvrTimePerFrame;
		} else if (!(m_nCodecId == CODEC_ID_VC1 && m_bFrame_repeat_pict && m_rtAvrTimePerFrame == 333666)) {
			rtStart = m_pFrame->reordered_opaque;
			rtStop  = m_pFrame->reordered_opaque2;
		}

		m_rtPrevStop = rtStop;

		ReorderBFrames(rtStart, rtStop);

		pOut->SetTime(&rtStart, &rtStop);
		pOut->SetMediaTime(NULL, NULL);

		// === New swscaler options
		//soft refresh - signal new swscaler colorspace details
		if (m_nSwRefresh == 1){
			m_nSwRefresh--;
			if (m_pSwsContext) {
				sws_freeContext(m_pSwsContext);
				m_pSwsContext	= NULL;
				m_PixFmt		= PIX_FMT_NB;
			}
		}

		// === New swscaler options
		// hard refresh - signal new output format
		if (m_nSwRefresh == 2){
			m_nSwRefresh--;
			CComPtr<IPin> pRendererPin;
			CComPtr<IPinConnection> pRendererConn;
			CMediaType cmtRenderer;

			ResetBuffer();

			BuildDXVAOutputFormat(); // refresh supported media types (m_pVideoOutputFormat)

			CAutoLock cObjectLock(m_pLock);

			cmtRenderer.InitMediaType();
			GetMediaType(0, &cmtRenderer);

 			m_pOutput->ConnectedTo(&pRendererPin);
			hr = pRendererPin->QueryInterface(IID_IPinConnection, (void**)&pRendererConn);
			if (FAILED(hr))	{
				// madVR accepts dynamic media type changes but does not support IPinConnection
				if (S_OK == (hr = pRendererPin->QueryAccept(&cmtRenderer))) {
					if (S_OK == (hr = m_pOutput->SetMediaType(&cmtRenderer))) {
						ReconnectOutput(PictWidthRounded()-64, PictHeightRounded(), true, PictWidth(), PictHeight()); // Force by altering 'w'
					}
				}
				return hr;
			}
			hr = pRendererConn->DynamicQueryAccept(&cmtRenderer);
			if (SUCCEEDED(hr)) {
				//VMR accepts dynamic media type changes.
				if (S_OK == (hr = m_pOutput->SetMediaType(&cmtRenderer))) {
					ReconnectOutput(PictWidthRounded()-64, PictHeightRounded(), true, PictWidth(), PictHeight());  // Force by altering 'w'
				}
			}	else {
				//EVR does not accept dynamic media type changes so it needs the DISPLAY CHANGED hack
				hr = m_pOutput->AddRef();
				if (S_OK == (hr = NotifyEvent(EC_DISPLAY_CHANGED, (LONG_PTR)m_pOutput->GetConnected(), 0))) {
					SleepEx(300,TRUE); // lame...
					if (S_OK == (hr = m_pOutput->Release())) {
						hr = m_pOutput->SetMediaType(&cmtRenderer);
					}
				}
			}
			return hr;
		}
		//

		PixelFormat PixFmt = csp_ffdshow2lavc(csp_lavc2ffdshow(m_pAVCtx->pix_fmt));
		if (PixFmt == PIX_FMT_NB) {
			PixFmt = m_pAVCtx->pix_fmt;
		}

		if ((m_PixFmt != PIX_FMT_NB) && (PixFmt != m_PixFmt)) {
			sws_freeContext(m_pSwsContext);
			m_pSwsContext	= NULL;
			m_PixFmt		= PIX_FMT_NB;
		}

		if (m_pSwsContext == NULL) {
			InitSwscale();
		}
		if (m_pSwsContext != NULL) {

			int outStride = m_pOutSize.cx;
			BYTE *outData = pDataOut;

			// From LAVVideo ...
			// Check if we have proper pixel alignment and the dst memory is actually aligned
			if (FFALIGN(outStride, 16) != outStride || ((uintptr_t)pDataOut % 16u)) {
				outStride = FFALIGN(outStride, 16);
				int requiredSize = (outStride * m_pAVCtx->height * m_nSwOutBpp) << 3;
				if (requiredSize > m_nAlignedFFBufferSize) {
					av_freep(&m_pAlignedFFBuffer);
					m_nAlignedFFBufferSize	= requiredSize;
					m_pAlignedFFBuffer		= (BYTE*)av_malloc(m_nAlignedFFBufferSize+FF_INPUT_BUFFER_PADDING_SIZE);
				}
				outData = m_pAlignedFFBuffer;
			}

			uint8_t*	dst[4] = {NULL, NULL, NULL, NULL};
			stride_t	dstStride[4] = {0, 0, 0, 0};
			const TcspInfo *outcspInfo=csp_getInfo(m_nOutCsp);

			// === New swscaler options
			if (m_nOutCsp == FF_CSP_YUY2 || m_nOutCsp == FF_CSP_RGB32 || m_nOutCsp == FF_CSP_RGBA || m_nOutCsp == FF_CSP_RGB16 || m_nOutCsp == FF_CSP_RGB15) {
				dst[0] = outData;
				dstStride[0] = (m_nSwOutBpp>>3) * (outStride);
			} else {
				for (unsigned int i=0; i<outcspInfo->numPlanes; i++) {
					dstStride[i] = outStride >> outcspInfo->shiftX[i];
					dst[i] = !i ? outData : dst[i-1] + dstStride[i-1] * (m_pOutSize.cy >> outcspInfo->shiftY[i-1]) ;
				}

				if (m_nOutCsp & FF_CSP_420P) {
					std::swap(dst[1], dst[2]);
				}
			}

			sws_scale(m_pSwsContext, m_pFrame->data, m_pFrame->linesize, 0, m_pAVCtx->height, dst, dstStride);

			if (outData != pDataOut) {
				if (m_nOutCsp & FF_CSP_420P) {
					std::swap(dst[1], dst[2]);
				}
				int rowsize = 0, height = 0;
				for (unsigned int i=0; i<outcspInfo->numPlanes; i++) {
					rowsize	= (m_pOutSize.cx*outcspInfo->Bpp) >> outcspInfo->shiftX[i];
					height	= m_pAVCtx->height >> outcspInfo->shiftY[i];
					copyPlane(pDataOut, rowsize, dst[i], (outStride*outcspInfo->Bpp) >> outcspInfo->shiftX[i], rowsize, height, (m_nOutCsp == FF_CSP_RGB32));
					pDataOut += rowsize * height;
				}
			}
		}

#if defined(_DEBUG) && 0
		static REFERENCE_TIME	rtLast = 0;
		TRACE ("Deliver : %10I64d - %10I64d   (%10I64d)  {%10I64d}\n", rtStart, rtStop,
			   rtStop - rtStart, rtStart - rtLast);
		rtLast = rtStart;
#endif

		SetTypeSpecificFlags (pOut);
		hr = m_pOutput->Deliver(pOut);
	}

	return hr;
}

bool CMPCVideoDecFilter::FindPicture(int nIndex, int nStartCode)
{
	DWORD dw = 0;

	for (int i=0; i<m_nFFBufferPos-nIndex; i++) {
		dw = (dw<<8) + m_pFFBuffer[i+nIndex];
		if (i >= 4) {
			if (m_nFFPicEnd == INT_MIN) {
				if ( (dw & 0xffffff00) == 0x00000100 &&
						(dw & 0x000000FF) == (DWORD)nStartCode ) {
					m_nFFPicEnd = i+nIndex-3;
				}
			} else {
				if ( (dw & 0xffffff00) == 0x00000100 &&
						((dw & 0x000000FF) == (DWORD)nStartCode || (dw & 0x000000FF) == 0xB3 )) {
					m_nFFPicEnd = i+nIndex-3;
					return true;
				}
			}
		}

	}

	return false;
}

void CMPCVideoDecFilter::ResetBuffer()
{
	m_nFFBufferPos	= 0;
	m_nFFPicEnd		= INT_MIN;

	for (int i=0; i<MAX_BUFF_TIME; i++) {
		m_FFBufferTime[i].nBuffPos	= INT_MIN;
		m_FFBufferTime[i].rtStart	= _I64_MIN;
		m_FFBufferTime[i].rtStop	= _I64_MIN;
	}
}

void CMPCVideoDecFilter::PushBufferTime(int nPos, REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop)
{
	for (int i=0; i<MAX_BUFF_TIME; i++) {
		if (m_FFBufferTime[i].nBuffPos == INT_MIN) {
			m_FFBufferTime[i].nBuffPos	= nPos;
			m_FFBufferTime[i].rtStart	= rtStart;
			m_FFBufferTime[i].rtStop	= rtStop;
			break;
		}
	}
}

void CMPCVideoDecFilter::PopBufferTime(int nPos)
{
	int nDestPos	= 0;
	int i			= 0;

	// Shift buffer time list
	while (i<MAX_BUFF_TIME && m_FFBufferTime[i].nBuffPos!=INT_MIN) {
		if (m_FFBufferTime[i].nBuffPos >= nPos) {
			m_FFBufferTime[nDestPos].nBuffPos	= m_FFBufferTime[i].nBuffPos - nPos;
			m_FFBufferTime[nDestPos].rtStart	= m_FFBufferTime[i].rtStart;
			m_FFBufferTime[nDestPos].rtStop		= m_FFBufferTime[i].rtStop;
			nDestPos++;
		}
		i++;
	}

	// Free unused slots
	for (i=nDestPos; i<MAX_BUFF_TIME; i++) {
		m_FFBufferTime[i].nBuffPos	= INT_MIN;
		m_FFBufferTime[i].rtStart	= _I64_MIN;
		m_FFBufferTime[i].rtStop	= _I64_MIN;
	}
}

bool CMPCVideoDecFilter::AppendBuffer (BYTE* pDataIn, int nSize, REFERENCE_TIME rtStart, REFERENCE_TIME rtStop)
{
	if (rtStart != _I64_MIN) {
		PushBufferTime (m_nFFBufferPos, rtStart, rtStop);
	}

	if (m_nFFBufferPos+nSize+FF_INPUT_BUFFER_PADDING_SIZE > m_nFFBufferSize) {
		m_nFFBufferSize = m_nFFBufferPos+nSize+FF_INPUT_BUFFER_PADDING_SIZE;
		m_pFFBuffer		= (BYTE*)av_realloc(m_pFFBuffer, m_nFFBufferSize);
	}

	memcpy(m_pFFBuffer+m_nFFBufferPos, pDataIn, nSize);

	m_nFFBufferPos += nSize;

	return true;
}

void CMPCVideoDecFilter::ShrinkBuffer()
{
	int nRemaining = m_nFFBufferPos-m_nFFPicEnd;

	ASSERT (m_nFFPicEnd != INT_MIN);

	PopBufferTime (m_nFFPicEnd);
	memcpy (m_pFFBuffer, m_pFFBuffer+m_nFFPicEnd, nRemaining);
	m_nFFBufferPos	= nRemaining;

	m_nFFPicEnd = (m_pFFBuffer[3] == 0x00) ?  0 : INT_MIN;
}

HRESULT CMPCVideoDecFilter::Transform(IMediaSample* pIn)
{
	CAutoLock cAutoLock(&m_csReceive);
	HRESULT			hr;
	BYTE*			pDataIn;
	int				nSize;
	REFERENCE_TIME	rtStart	= _I64_MIN;
	REFERENCE_TIME	rtStop	= _I64_MIN;

	if (FAILED(hr = pIn->GetPointer(&pDataIn))) {
		return hr;
	}

	nSize = pIn->GetActualDataLength();
	// Skip empty packet
	if (nSize == 0) {
		return S_OK;
	}

	hr = pIn->GetTime(&rtStart, &rtStop);

	if (FAILED(hr)) {
		rtStart = rtStop = _I64_MIN;
	}

	if (m_nDXVAMode == MODE_SOFTWARE || (m_nCodecId == CODEC_ID_VC1 && !m_bIsEVO)) {
		UpdateFrameTime(rtStart, rtStop, m_bFrame_repeat_pict);
	}

	m_pAVCtx->reordered_opaque  = rtStart;
	m_pAVCtx->reordered_opaque2 = rtStop;

	if (m_pAVCtx->has_b_frames) {
		m_BFrames[m_nPosB].rtStart	= rtStart;
		m_BFrames[m_nPosB].rtStop	= rtStop;
		m_nPosB						= 1-m_nPosB;
	}

	switch (m_nDXVAMode) {
		case MODE_SOFTWARE :
			hr = SoftwareDecode (pIn, pDataIn, nSize, rtStart, rtStop);
			break;
		case MODE_DXVA1 :
		case MODE_DXVA2 :
			CheckPointer (m_pDXVADecoder, E_UNEXPECTED);
			UpdateAspectRatio();

			// Change aspect ratio for DXVA1
			if ((m_nDXVAMode == MODE_DXVA1) &&
					ReconnectOutput(PictWidthRounded(), PictHeightRounded(), true, PictWidth(), PictHeight()) == S_OK) {
				m_pDXVADecoder->ConfigureDXVA1();
			}

			if (m_pAVCtx->codec_id == CODEC_ID_MPEG2VIDEO) {
				AppendBuffer (pDataIn, nSize, rtStart, rtStop);
				hr = S_OK;

				while (FindPicture (max (m_nFFBufferPos-nSize-4, 0), 0x00)) {
					if (m_FFBufferTime[0].nBuffPos != INT_MIN && m_FFBufferTime[0].nBuffPos < m_nFFPicEnd) {
						rtStart = m_FFBufferTime[0].rtStart;
						rtStop  = m_FFBufferTime[0].rtStop;
					} else {
						rtStart = rtStop = _I64_MIN;
					}
					hr = m_pDXVADecoder->DecodeFrame (m_pFFBuffer, m_nFFPicEnd, rtStart, rtStop);
					ShrinkBuffer();
				}
			} else {
				hr = m_pDXVADecoder->DecodeFrame (pDataIn, nSize, rtStart, rtStop);
			}
			break;
		default :
			ASSERT (FALSE);
			hr = E_UNEXPECTED;
	}

	return hr;
}

void CMPCVideoDecFilter::UpdateAspectRatio()
{
	if (((m_nARMode) && (m_pAVCtx)) && ((m_pAVCtx->sample_aspect_ratio.num>0) && (m_pAVCtx->sample_aspect_ratio.den>0))) {
		CSize SAR(m_pAVCtx->sample_aspect_ratio.num, m_pAVCtx->sample_aspect_ratio.den);
		if (m_sar != SAR) {
			m_sar = SAR;
			CSize aspect(m_nWidth * SAR.cx, m_nHeight * SAR.cy);
			int lnko = LNKO(aspect.cx, aspect.cy);
			if (lnko > 1) {
				aspect.cx /= lnko, aspect.cy /= lnko;
			}
			SetAspect(aspect);
		}
	}
}

void CMPCVideoDecFilter::ReorderBFrames(REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop)
{
	// Re-order B-frames if needed
	if (m_pAVCtx->has_b_frames && m_bReorderBFrame) {
		rtStart	= m_BFrames [m_nPosB].rtStart;
		rtStop	= m_BFrames [m_nPosB].rtStop;
	}
}

void CMPCVideoDecFilter::FillInVideoDescription(DXVA2_VideoDesc *pDesc)
{
	memset (pDesc, 0, sizeof(DXVA2_VideoDesc));
	pDesc->SampleWidth			= PictWidthRounded();
	pDesc->SampleHeight			= PictHeightRounded();
	pDesc->Format				= D3DFMT_A8R8G8B8;
	pDesc->UABProtectionLevel	= 1;
}

BOOL CMPCVideoDecFilter::IsSupportedDecoderMode(const GUID& mode)
{
	if (IsDXVASupported()) {
		for (int i=0; i<MAX_SUPPORTED_MODE; i++) {
			if (*ffCodecs[m_nCodecNb].DXVAModes->Decoder[i] == GUID_NULL) {
				break;
			} else if (*ffCodecs[m_nCodecNb].DXVAModes->Decoder[i] == mode) {
				return true;
			}
		}
	}

	return false;
}

BOOL CMPCVideoDecFilter::IsSupportedDecoderConfig(const D3DFORMAT nD3DFormat, const DXVA2_ConfigPictureDecode& config, bool& bIsPrefered)
{
	bool	bRet = false;

	bRet = (nD3DFormat == MAKEFOURCC('N', 'V', '1', '2') || nD3DFormat == MAKEFOURCC('I', 'M', 'C', '3'));

	bIsPrefered = (config.ConfigBitstreamRaw == ffCodecs[m_nCodecNb].DXVAModes->PreferedConfigBitstream);
	LOG (_T("IsSupportedDecoderConfig  0x%08x  %d"), nD3DFormat, bRet);
	return bRet;
}

HRESULT CMPCVideoDecFilter::FindDXVA2DecoderConfiguration(IDirectXVideoDecoderService *pDecoderService,
		const GUID& guidDecoder,
		DXVA2_ConfigPictureDecode *pSelectedConfig,
		BOOL *pbFoundDXVA2Configuration)
{
	HRESULT hr = S_OK;
	UINT cFormats = 0;
	UINT cConfigurations = 0;
	bool bIsPrefered = false;

	D3DFORMAT                   *pFormats = NULL;			// size = cFormats
	DXVA2_ConfigPictureDecode   *pConfig = NULL;			// size = cConfigurations

	// Find the valid render target formats for this decoder GUID.
	hr = pDecoderService->GetDecoderRenderTargets(guidDecoder, &cFormats, &pFormats);
	LOG (_T("GetDecoderRenderTargets => %d"), cFormats);

	if (SUCCEEDED(hr)) {
		// Look for a format that matches our output format.
		for (UINT iFormat = 0; iFormat < cFormats;  iFormat++) {
			LOG (_T("Try to negociate => 0x%08x"), pFormats[iFormat]);

			// Fill in the video description. Set the width, height, format, and frame rate.
			FillInVideoDescription(&m_VideoDesc); // Private helper function.
			m_VideoDesc.Format = pFormats[iFormat];

			// Get the available configurations.
			hr = pDecoderService->GetDecoderConfigurations(guidDecoder, &m_VideoDesc, NULL, &cConfigurations, &pConfig);

			if (FAILED(hr)) {
				continue;
			}

			// Find a supported configuration.
			for (UINT iConfig = 0; iConfig < cConfigurations; iConfig++) {
				if (IsSupportedDecoderConfig(pFormats[iFormat], pConfig[iConfig], bIsPrefered)) {
					// This configuration is good.
					if (bIsPrefered || !*pbFoundDXVA2Configuration) {
						*pbFoundDXVA2Configuration = TRUE;
						*pSelectedConfig = pConfig[iConfig];
					}

					if (bIsPrefered) {
						break;
					}
				}
			}

			CoTaskMemFree(pConfig);
		} // End of formats loop.
	}

	CoTaskMemFree(pFormats);

	// Note: It is possible to return S_OK without finding a configuration.
	return hr;
}

HRESULT CMPCVideoDecFilter::ConfigureDXVA2(IPin *pPin)
{
	HRESULT hr						 = S_OK;
	UINT	cDecoderGuids			 = 0;
	BOOL	bFoundDXVA2Configuration = FALSE;
	BOOL    bHasIntelGuid			 = FALSE;
	GUID	guidDecoder				 = GUID_NULL;

	DXVA2_ConfigPictureDecode config;
	ZeroMemory(&config, sizeof(config));

	CComPtr<IMFGetService>					pGetService;
	CComPtr<IDirect3DDeviceManager9>		pDeviceManager;
	CComPtr<IDirectXVideoDecoderService>	pDecoderService;
	GUID*									pDecoderGuids = NULL;
	HANDLE									hDevice = INVALID_HANDLE_VALUE;

	// Query the pin for IMFGetService.
	hr = pPin->QueryInterface(__uuidof(IMFGetService), (void**)&pGetService);

	// Get the Direct3D device manager.
	if (SUCCEEDED(hr)) {
		hr = pGetService->GetService(
				 MR_VIDEO_ACCELERATION_SERVICE,
				 __uuidof(IDirect3DDeviceManager9),
				 (void**)&pDeviceManager);
	}

	// Open a new device handle.
	if (SUCCEEDED(hr)) {
		hr = pDeviceManager->OpenDeviceHandle(&hDevice);
	}

	// Get the video decoder service.
	if (SUCCEEDED(hr)) {
		hr = pDeviceManager->GetVideoService(
				 hDevice,
				 __uuidof(IDirectXVideoDecoderService),
				 (void**)&pDecoderService);
	}

	// Get the decoder GUIDs.
	if (SUCCEEDED(hr)) {
		hr = pDecoderService->GetDecoderDeviceGuids(&cDecoderGuids, &pDecoderGuids);
	}

	if (SUCCEEDED(hr)) {

		//Intel patch for Ivy Bridge and Sandy Bridge
		if (m_nPCIVendor == PCIV_Intel) {
			for (UINT iCnt = 0; iCnt < cDecoderGuids; iCnt++) {
				if (pDecoderGuids[iCnt] == DXVA_Intel_H264_ClearVideo)
					bHasIntelGuid = TRUE;
			}
		}
		// Look for the decoder GUIDs we want.
		for (UINT iGuid = 0; iGuid < cDecoderGuids; iGuid++) {
			// Do we support this mode?
			if (!IsSupportedDecoderMode(pDecoderGuids[iGuid])) {
				continue;
			}

			// Find a configuration that we support.
			hr = FindDXVA2DecoderConfiguration(pDecoderService, pDecoderGuids[iGuid], &config, &bFoundDXVA2Configuration);

			if (FAILED(hr)) {
				break;
			}

			// Patch for the Sandy Bridge (prevent crash on Mode_E, fixme later)
			if (m_nPCIVendor == PCIV_Intel && pDecoderGuids[iGuid] == DXVA2_ModeH264_E && bHasIntelGuid) {
				continue;
			}

			if (bFoundDXVA2Configuration) {
				// Found a good configuration. Save the GUID.
				guidDecoder = pDecoderGuids[iGuid];
				if (!bHasIntelGuid) break;
			}
		}
	}

	if (pDecoderGuids) {
		CoTaskMemFree(pDecoderGuids);
	}
	if (!bFoundDXVA2Configuration) {
		hr = E_FAIL; // Unable to find a configuration.
	}

	if (SUCCEEDED(hr)) {
		// Store the things we will need later.
		m_pDeviceManager	= pDeviceManager;
		m_pDecoderService	= pDecoderService;

		m_DXVA2Config		= config;
		m_DXVADecoderGUID	= guidDecoder;
		m_hDevice			= hDevice;
	}

	if (FAILED(hr)) {
		if (hDevice != INVALID_HANDLE_VALUE) {
			pDeviceManager->CloseDeviceHandle(hDevice);
		}
	}

	return hr;
}

HRESULT CMPCVideoDecFilter::SetEVRForDXVA2(IPin *pPin)
{
    IMFGetService* pGetService;
    HRESULT hr = pPin->QueryInterface(__uuidof(IMFGetService), reinterpret_cast<void**>(&pGetService));
	if (SUCCEEDED(hr)) {
		IDirectXVideoMemoryConfiguration* pVideoConfig;
		hr = pGetService->GetService(MR_VIDEO_ACCELERATION_SERVICE, IID_IDirectXVideoMemoryConfiguration, reinterpret_cast<void**>(&pVideoConfig));
		if (SUCCEEDED(hr)) {
			// Notify the EVR.
			DXVA2_SurfaceType surfaceType;
			DWORD dwTypeIndex = 0;
			for (;;) {
				hr = pVideoConfig->GetAvailableSurfaceTypeByIndex(dwTypeIndex, &surfaceType);
				if (FAILED(hr)) {
					break;
				}
				if (surfaceType == DXVA2_SurfaceType_DecoderRenderTarget) {
					hr = pVideoConfig->SetSurfaceType(DXVA2_SurfaceType_DecoderRenderTarget);
					break;
				}
				++dwTypeIndex;
			}
			pVideoConfig->Release();
		}
		pGetService->Release();
    }
	return hr;
}

HRESULT CMPCVideoDecFilter::CreateDXVA2Decoder(UINT nNumRenderTargets, IDirect3DSurface9** pDecoderRenderTargets)
{
	HRESULT							hr;
	CComPtr<IDirectXVideoDecoder>	pDirectXVideoDec;

	m_pDecoderRenderTarget	= NULL;

	if (m_pDXVADecoder) {
		m_pDXVADecoder->SetDirectXVideoDec (NULL);
	}

	hr = m_pDecoderService->CreateVideoDecoder (m_DXVADecoderGUID, &m_VideoDesc, &m_DXVA2Config,
			pDecoderRenderTargets, nNumRenderTargets, &pDirectXVideoDec);

	if (SUCCEEDED (hr)) {
		if (!m_pDXVADecoder) {
			m_pDXVADecoder	= CDXVADecoder::CreateDecoder (this, pDirectXVideoDec, &m_DXVADecoderGUID, GetPicEntryNumber(), &m_DXVA2Config);
			if (m_pDXVADecoder) {
				m_pDXVADecoder->SetExtraData ((BYTE*)m_pAVCtx->extradata, m_pAVCtx->extradata_size);
			}
		}

		m_pDXVADecoder->SetDirectXVideoDec (pDirectXVideoDec);
	}

	return hr;
}

HRESULT CMPCVideoDecFilter::FindDXVA1DecoderConfiguration(IAMVideoAccelerator* pAMVideoAccelerator, const GUID* guidDecoder, DDPIXELFORMAT* pPixelFormat)
{
	HRESULT			hr				= E_FAIL;
	DWORD			dwFormats		= 0;
	DDPIXELFORMAT*	pPixelFormats	= NULL;


	pAMVideoAccelerator->GetUncompFormatsSupported (guidDecoder, &dwFormats, NULL);
	if (dwFormats > 0) {
		// Find the valid render target formats for this decoder GUID.
		pPixelFormats = DNew DDPIXELFORMAT[dwFormats];
		hr = pAMVideoAccelerator->GetUncompFormatsSupported (guidDecoder, &dwFormats, pPixelFormats);
		if (SUCCEEDED(hr)) {
			// Look for a format that matches our output format.
			for (DWORD iFormat = 0; iFormat < dwFormats; iFormat++) {
				if (pPixelFormats[iFormat].dwFourCC == MAKEFOURCC ('N', 'V', '1', '2')) {
					memcpy (pPixelFormat, &pPixelFormats[iFormat], sizeof(DDPIXELFORMAT));
					SAFE_DELETE_ARRAY(pPixelFormats)
					return S_OK;
				}
			}

			SAFE_DELETE_ARRAY(pPixelFormats);
			hr = E_FAIL;
		}
	}

	return hr;
}

HRESULT CMPCVideoDecFilter::CheckDXVA1Decoder(const GUID *pGuid)
{
	if (m_nCodecNb != -1) {
		for (int i=0; i<MAX_SUPPORTED_MODE; i++)
			if (*ffCodecs[m_nCodecNb].DXVAModes->Decoder[i] == *pGuid) {
				return S_OK;
			}
	}

	return E_INVALIDARG;
}

void CMPCVideoDecFilter::SetDXVA1Params(const GUID* pGuid, DDPIXELFORMAT* pPixelFormat)
{
	m_DXVADecoderGUID		= *pGuid;
	memcpy (&m_PixelFormat, pPixelFormat, sizeof (DDPIXELFORMAT));
}

WORD CMPCVideoDecFilter::GetDXVA1RestrictedMode()
{
	if (m_nCodecNb != -1) {
		for (int i=0; i<MAX_SUPPORTED_MODE; i++)
			if (*ffCodecs[m_nCodecNb].DXVAModes->Decoder[i] == m_DXVADecoderGUID) {
				return ffCodecs[m_nCodecNb].DXVAModes->RestrictedMode [i];
			}
	}

	return DXVA_RESTRICTED_MODE_UNRESTRICTED;
}

HRESULT CMPCVideoDecFilter::CreateDXVA1Decoder(IAMVideoAccelerator*  pAMVideoAccelerator, const GUID* pDecoderGuid, DWORD dwSurfaceCount)
{
	if (m_pDXVADecoder && m_DXVADecoderGUID	== *pDecoderGuid) {
		return S_OK;
	}
	SAFE_DELETE (m_pDXVADecoder);

	if (!m_bUseDXVA) {
		return E_FAIL;
	}

	m_nDXVAMode			= MODE_DXVA1;
	m_DXVADecoderGUID	= *pDecoderGuid;
	m_pDXVADecoder		= CDXVADecoder::CreateDecoder (this, pAMVideoAccelerator, &m_DXVADecoderGUID, dwSurfaceCount);
	if (m_pDXVADecoder) {
		m_pDXVADecoder->SetExtraData ((BYTE*)m_pAVCtx->extradata, m_pAVCtx->extradata_size);
	}

	return S_OK;
}

// ISpecifyPropertyPages2

STDMETHODIMP CMPCVideoDecFilter::GetPages(CAUUID* pPages)
{
	CheckPointer(pPages, E_POINTER);

#ifdef REGISTER_FILTER
	pPages->cElems		= 2;
#else
	pPages->cElems		= 1;
#endif

	pPages->pElems		= (GUID*)CoTaskMemAlloc(sizeof(GUID) * pPages->cElems);
	pPages->pElems[0]	= __uuidof(CMPCVideoDecSettingsWnd);
	if (pPages->cElems>1) {
		pPages->pElems[1]	= __uuidof(CMPCVideoDecCodecWnd);
	}

	return S_OK;
}

STDMETHODIMP CMPCVideoDecFilter::CreatePage(const GUID& guid, IPropertyPage** ppPage)
{
	CheckPointer(ppPage, E_POINTER);

	if (*ppPage != NULL) {
		return E_INVALIDARG;
	}

	HRESULT hr;

	if (guid == __uuidof(CMPCVideoDecSettingsWnd)) {
		(*ppPage = DNew CInternalPropertyPageTempl<CMPCVideoDecSettingsWnd>(NULL, &hr))->AddRef();
	} else if (guid == __uuidof(CMPCVideoDecCodecWnd)) {
		(*ppPage = DNew CInternalPropertyPageTempl<CMPCVideoDecCodecWnd>(NULL, &hr))->AddRef();
	}

	return *ppPage ? S_OK : E_FAIL;
}

void CMPCVideoDecFilter::SetFrameType(FF_FIELD_TYPE nFrameType)
{
	m_nFrameType = nFrameType;
}

// EVR functions
HRESULT CMPCVideoDecFilter::DetectVideoCard_EVR(IPin *pPin)
{
	IMFGetService* pGetService;
	HRESULT hr = pPin->QueryInterface(__uuidof(IMFGetService), reinterpret_cast<void**>(&pGetService));
	if (SUCCEEDED(hr)) {
		// Try to get the adapter description of the active DirectX 9 device.
		IDirect3DDeviceManager9* pDevMan9;
		hr = pGetService->GetService(MR_VIDEO_ACCELERATION_SERVICE, IID_IDirect3DDeviceManager9, reinterpret_cast<void**>(&pDevMan9));
		if (SUCCEEDED(hr)) {
			HANDLE hDevice;
			hr = pDevMan9->OpenDeviceHandle(&hDevice);
			if (SUCCEEDED(hr)) {
				IDirect3DDevice9* pD3DDev9;
				hr = pDevMan9->LockDevice(hDevice, &pD3DDev9, TRUE);
				if (hr == DXVA2_E_NEW_VIDEO_DEVICE) {
					// Invalid device handle. Try to open a new device handle.
					hr = pDevMan9->CloseDeviceHandle(hDevice);
					if (SUCCEEDED(hr)) {
						hr = pDevMan9->OpenDeviceHandle(&hDevice);
						// Try to lock the device again.
						if (SUCCEEDED(hr)) {
							hr = pDevMan9->LockDevice(hDevice, &pD3DDev9, TRUE);
						}
					}
				}
				if (SUCCEEDED(hr)) {
					D3DDEVICE_CREATION_PARAMETERS DevPar9;
					hr = pD3DDev9->GetCreationParameters(&DevPar9);
					if (SUCCEEDED(hr)) {
						IDirect3D9* pD3D9;
						hr = pD3DDev9->GetDirect3D(&pD3D9);
						if (SUCCEEDED(hr)) {
							D3DADAPTER_IDENTIFIER9 AdapID9;
							hr = pD3D9->GetAdapterIdentifier(DevPar9.AdapterOrdinal, 0, &AdapID9);
							if (SUCCEEDED(hr)) {
								// copy adapter description
								m_nPCIVendor = AdapID9.VendorId;
								m_nPCIDevice = AdapID9.DeviceId;
								m_VideoDriverVersion.QuadPart = AdapID9.DriverVersion.QuadPart;
								m_strDeviceDescription = AdapID9.Description;
								m_strDeviceDescription.AppendFormat(_T(" (%04hX:%04hX)"), m_nPCIVendor, m_nPCIDevice);
								}
						}
						pD3D9->Release();
					}
					pD3DDev9->Release();
					pDevMan9->UnlockDevice(hDevice, FALSE);
				}
				pDevMan9->CloseDeviceHandle(hDevice);
			}
			pDevMan9->Release();
		}
		pGetService->Release();
	}
	return hr;
}

// IFFmpegDecFilter
STDMETHODIMP CMPCVideoDecFilter::Apply()
{
#ifdef REGISTER_FILTER
	CRegKey key;
	if (ERROR_SUCCESS == key.Create(HKEY_CURRENT_USER, _T("Software\\MPC-BE Filters\\MPC Video Decoder"))) {
		key.SetDWORDValue(_T("ThreadNumber"), m_nThreadNumber);
		key.SetDWORDValue(_T("DiscardMode"), m_nDiscardMode);
		key.SetDWORDValue(_T("ErrorRecognition"), m_nErrorRecognition);
		key.SetDWORDValue(_T("IDCTAlgo"), m_nIDCTAlgo);
		key.SetDWORDValue(_T("ActiveCodecs"), m_nActiveCodecs);
		key.SetDWORDValue(_T("ARMode"), m_nARMode);
		key.SetDWORDValue(_T("DXVACheckCompatibility"), m_nDXVACheckCompatibility);
		key.SetDWORDValue(_T("DisableDXVA_SD"), m_nDXVA_SD);

		// === New swscaler options
		if (m_nSwRefresh>0) {
			key.SetDWORDValue(_T("SwChromaToRGB"), m_nSwChromaToRGB);
			key.SetDWORDValue(_T("SwResizeMethodBE"), m_nSwResizeMethodBE);
			key.SetDWORDValue(_T("SwColorspace"), m_nSwColorspace);
			key.SetDWORDValue(_T("SwInputLevels"), m_nSwInputLevels);
			key.SetDWORDValue(_T("SwOutputLevels"), m_nSwOutputLevels);
		}
		if (m_nSwRefresh>1) {
			key.SetDWORDValue(_T("SwOutputFormats"), m_nSwOutputFormats);
		}
		//
	}
#else
	AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("ThreadNumber"), m_nThreadNumber);
	AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("DiscardMode"), m_nDiscardMode);
	AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("ErrorRecognition"), m_nErrorRecognition);
	AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("IDCTAlgo"), m_nIDCTAlgo);
	AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("ARMode"), m_nARMode);
	AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("DXVACheckCompatibility"), m_nDXVACheckCompatibility);
	AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("DisableDXVA_SD"), m_nDXVA_SD);

	// === New swscaler options
	if (m_nSwRefresh>0) {
		AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwChromaToRGB"), m_nSwChromaToRGB);
		AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwResizeMethodBE"), m_nSwResizeMethodBE);
		AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwColorspace"), m_nSwColorspace);
		AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwInputLevels"), m_nSwInputLevels);
		AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwOutputLevels"), m_nSwOutputLevels);
	}
	if (m_nSwRefresh>1) {
		AfxGetApp()->WriteProfileInt(_T("Filters\\MPC Video Decoder"), _T("SwOutputFormats"), m_nSwOutputFormats);
	}
	//
#endif

	return S_OK;
}

// === IMPCVideoDecFilter

STDMETHODIMP CMPCVideoDecFilter::SetThreadNumber(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nThreadNumber = nValue;
	return S_OK;
}

STDMETHODIMP_(int) CMPCVideoDecFilter::GetThreadNumber()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nThreadNumber;
}

STDMETHODIMP CMPCVideoDecFilter::SetDiscardMode(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nDiscardMode = nValue;
	return S_OK;
}

STDMETHODIMP_(int) CMPCVideoDecFilter::GetDiscardMode()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nDiscardMode;
}

STDMETHODIMP CMPCVideoDecFilter::SetErrorRecognition(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nErrorRecognition = nValue;
	return S_OK;
}

STDMETHODIMP_(int) CMPCVideoDecFilter::GetErrorRecognition()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nErrorRecognition;
}

STDMETHODIMP CMPCVideoDecFilter::SetIDCTAlgo(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nIDCTAlgo = nValue;
	return S_OK;
}

STDMETHODIMP_(int) CMPCVideoDecFilter::GetIDCTAlgo()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nIDCTAlgo;
}

STDMETHODIMP_(GUID*) CMPCVideoDecFilter::GetDXVADecoderGuid()
{
	if (m_pGraph == NULL) {
		return NULL;
	} else {
		return &m_DXVADecoderGUID;
	}
}

STDMETHODIMP CMPCVideoDecFilter::SetActiveCodecs(MPC_VIDEO_CODEC nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nActiveCodecs = (int)nValue;
	return S_OK;
}

STDMETHODIMP_(MPC_VIDEO_CODEC) CMPCVideoDecFilter::GetActiveCodecs()
{
	CAutoLock cAutoLock(&m_csProps);
	return (MPC_VIDEO_CODEC)m_nActiveCodecs;
}

STDMETHODIMP_(LPCTSTR) CMPCVideoDecFilter::GetVideoCardDescription()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_strDeviceDescription;
}

STDMETHODIMP CMPCVideoDecFilter::SetARMode(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nARMode = nValue;
	return S_OK;
}

STDMETHODIMP_(int) CMPCVideoDecFilter::GetARMode()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nARMode;
}

STDMETHODIMP CMPCVideoDecFilter::SetDXVACheckCompatibility(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nDXVACheckCompatibility = nValue;
	return S_OK;
}

STDMETHODIMP_(int) CMPCVideoDecFilter::GetDXVACheckCompatibility()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nDXVACheckCompatibility;
}

STDMETHODIMP CMPCVideoDecFilter::SetDXVA_SD(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nDXVA_SD = nValue;
	return S_OK;
}

STDMETHODIMP_(int) CMPCVideoDecFilter::GetDXVA_SD()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nDXVA_SD;
}

// === New swscaler options
STDMETHODIMP CMPCVideoDecFilter::SetSwRefresh(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nSwRefresh = nValue;
	return S_OK;
}

STDMETHODIMP CMPCVideoDecFilter::SetSwOutputFormats(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nSwOutputFormats = (int)nValue;
	return S_OK;
}
STDMETHODIMP_(int) CMPCVideoDecFilter::GetSwOutputFormats()
{
	CAutoLock cAutoLock(&m_csProps);
	return (int)m_nSwOutputFormats;
}

STDMETHODIMP CMPCVideoDecFilter::SetSwChromaToRGB(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nSwChromaToRGB = nValue;
	return S_OK;
}
STDMETHODIMP_(int) CMPCVideoDecFilter::GetSwChromaToRGB()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nSwChromaToRGB;
}

STDMETHODIMP CMPCVideoDecFilter::SetSwResizeMethodBE(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nSwResizeMethodBE = nValue;
	return S_OK;
}
STDMETHODIMP_(int) CMPCVideoDecFilter::GetSwResizeMethodBE()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nSwResizeMethodBE;
}

STDMETHODIMP CMPCVideoDecFilter::SetSwColorspace(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nSwColorspace = nValue;
	return S_OK;
}
STDMETHODIMP_(int) CMPCVideoDecFilter::GetSwColorspace()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nSwColorspace;
}

STDMETHODIMP CMPCVideoDecFilter::SetSwInputLevels(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nSwInputLevels = nValue;
	return S_OK;
}
STDMETHODIMP_(int) CMPCVideoDecFilter::GetSwInputLevels()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nSwInputLevels;
}

STDMETHODIMP CMPCVideoDecFilter::SetSwOutputLevels(int nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nSwOutputLevels = nValue;
	return S_OK;
}
STDMETHODIMP_(int) CMPCVideoDecFilter::GetSwOutputLevels()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nSwOutputLevels;
}

STDMETHODIMP CMPCVideoDecFilter::SetDialogHWND(HWND nValue)
{
	CAutoLock cAutoLock(&m_csProps);
	m_nDialogHWND = nValue;
	return S_OK;
}

STDMETHODIMP_(unsigned __int64) CMPCVideoDecFilter::GetOutputFormat()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nOutCsp;
}

// === IMPCVideoDecFilter2
STDMETHODIMP_(int) CMPCVideoDecFilter::GetFrameType()
{
	CAutoLock cAutoLock(&m_csProps);
	return m_nFrameType;
}

// === IMPCVideoDecFilterCodec
STDMETHODIMP CMPCVideoDecFilter::SetFFMpegCodec(bool* bValue)
{
	CAutoLock cAutoLock(&m_csProps);
	FFmpegFilters = bValue;
	return S_OK;
}

STDMETHODIMP CMPCVideoDecFilter::SetDXVACodec(bool* bValue)
{
	CAutoLock cAutoLock(&m_csProps);
	DXVAFilters = bValue;
	return S_OK;
}
