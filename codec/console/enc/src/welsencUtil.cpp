/*!
 * \copy
 *     Copyright (c)  2013, Cisco Systems
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#if defined (ANDROID_NDK)
#include <android/log.h>
#endif
#ifdef ONLY_ENC_FRAMES_NUM
#undef ONLY_ENC_FRAMES_NUM
#endif//ONLY_ENC_FRAMES_NUM
#define ONLY_ENC_FRAMES_NUM INT_MAX // 2, INT_MAX // type the num you try to encode here, 2, 10, etc

#if defined (WINDOWS_PHONE)
float   g_fFPS           = 0.0;
double  g_dEncoderTime   = 0.0;
int     g_iEncodedFrame  = 0;
#endif

#if defined (ANDROID_NDK)
#define LOG_TAG "welsenc"
#define LOGI(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define printf(...) LOGI(__VA_ARGS__)
#define fprintf(a, ...) LOGI(__VA_ARGS__)
#endif
//#define STICK_STREAM_SIZE

#include "measure_time.h"


#include "typedefs.h"
#include "read_config.h"

#ifdef _MSC_VER
#include <io.h>     /* _setmode() */
#include <fcntl.h>  /* _O_BINARY */
#endif//_MSC_VER

#include "codec_def.h"
#include "codec_api.h"
#include "extern.h"
#include "macros.h"
#include "wels_const.h"

#include "mt_defs.h"
#include "WelsThreadLib.h"

#ifdef _WIN32
#ifdef WINAPI_FAMILY
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#define HAVE_PROCESS_AFFINITY
#endif
#else /* defined(WINAPI_FAMILY) */
#define HAVE_PROCESS_AFFINITY
#endif
#endif /* _WIN32 */

#if defined(__linux__) || defined(__unix__)
#define _FILE_OFFSET_BITS 64
#endif

#include "welsencUtil.h"

using namespace WelsEnc;

/* Ctrl-C handler */
static int     g_iCtrlC = 0;
static void    SigIntHandler (int a) {
  g_iCtrlC = 1;
}
static int     g_LevelSetting = WELS_LOG_ERROR;


int ParseLayerConfig (CReadConfig& cRdLayerCfg, const int iLayer, SEncParamExt& pSvcParam, SFilesSet& sFileSet) {
  if (!cRdLayerCfg.ExistFile()) {
    fprintf (stderr, "Unabled to open layer #%d configuration file: %s.\n", iLayer, cRdLayerCfg.GetFileName().c_str());
    return 1;
  }

  SSpatialLayerConfig* pDLayer = &pSvcParam.sSpatialLayers[iLayer];
  int iLeftTargetBitrate = (pSvcParam.iRCMode != RC_OFF_MODE) ? pSvcParam.iTargetBitrate : 0;
  SLayerPEncCtx sLayerCtx;
  memset (&sLayerCtx, 0, sizeof (SLayerPEncCtx));

  string strTag[4];
  string str_ ("SlicesAssign");
  const int kiSize = (int)str_.size();

  while (!cRdLayerCfg.EndOfFile()) {
    long iLayerRd = cRdLayerCfg.ReadLine (&strTag[0]);
    if (iLayerRd > 0) {
      if (strTag[0].empty())
        continue;
      if (strTag[0].compare ("FrameWidth") == 0) {
        pDLayer->iVideoWidth = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("FrameHeight") == 0) {
        pDLayer->iVideoHeight = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("FrameRateOut") == 0) {
        pDLayer->fFrameRate = (float)atof (strTag[1].c_str());
      } else if (strTag[0].compare ("ReconFile") == 0) {
        const unsigned int kiLen = (unsigned int)strTag[1].length();
        if (kiLen >= sizeof (sFileSet.sRecFileName[iLayer]))
          return -1;
        sFileSet.sRecFileName[iLayer][kiLen] = '\0';
        strncpy (sFileSet.sRecFileName[iLayer], strTag[1].c_str(), kiLen); // confirmed_safe_unsafe_usage
      } else if (strTag[0].compare ("ProfileIdc") == 0) {
        pDLayer->uiProfileIdc = (EProfileIdc)atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("FRExt") == 0) {
//        pDLayer->frext_mode = (bool)atoi(strTag[1].c_str());
      } else if (strTag[0].compare ("SpatialBitrate") == 0) {
        pDLayer->iSpatialBitrate = 1000 * atoi (strTag[1].c_str());
        if (pSvcParam.iRCMode != RC_OFF_MODE) {
          if (pDLayer->iSpatialBitrate <= 0) {
            fprintf (stderr, "Invalid spatial bitrate(%d) in dependency layer #%d.\n", pDLayer->iSpatialBitrate, iLayer);
            return -1;
          }
          if (pDLayer->iSpatialBitrate > iLeftTargetBitrate) {
            fprintf (stderr, "Invalid spatial(#%d) bitrate(%d) setting due to unavailable left(%d)!\n", iLayer,
                     pDLayer->iSpatialBitrate, iLeftTargetBitrate);
            return -1;
          }
          iLeftTargetBitrate -= pDLayer->iSpatialBitrate;
        }
      } else if (strTag[0].compare ("MaxSpatialBitrate") == 0) {
        pDLayer->iMaxSpatialBitrate = 1000 * atoi (strTag[1].c_str());
        if (pSvcParam.iRCMode != RC_OFF_MODE) {
          if (pDLayer->iMaxSpatialBitrate < 0) {
            fprintf (stderr, "Invalid max spatial bitrate(%d) in dependency layer #%d.\n", pDLayer->iMaxSpatialBitrate, iLayer);
            return -1;
          }
          if (pDLayer->iMaxSpatialBitrate > 0 && pDLayer->iMaxSpatialBitrate < pDLayer->iSpatialBitrate) {
            fprintf (stderr, "Invalid max spatial(#%d) bitrate(%d) setting::: < layerBitrate(%d)!\n", iLayer,
                     pDLayer->iMaxSpatialBitrate, pDLayer->iSpatialBitrate);
            return -1;
          }
        }
      } else if (strTag[0].compare ("InitialQP") == 0) {
        sLayerCtx.iDLayerQp = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("SliceMode") == 0) {
        sLayerCtx.sSliceArgument.uiSliceMode = (SliceModeEnum)atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("SliceSize") == 0) { //SM_SIZELIMITED_SLICE
        sLayerCtx.sSliceArgument.uiSliceSizeConstraint = atoi (strTag[1].c_str());
        continue;
      } else if (strTag[0].compare ("SliceNum") == 0) {
        sLayerCtx.sSliceArgument.uiSliceNum = atoi (strTag[1].c_str());
      } else if (strTag[0].compare (0, kiSize, str_) == 0) {
        const char* kpString = strTag[0].c_str();
        int uiSliceIdx = atoi (&kpString[kiSize]);
        assert (uiSliceIdx < MAX_SLICES_NUM);
        sLayerCtx.sSliceArgument.uiSliceMbNum[uiSliceIdx] = atoi (strTag[1].c_str());
      }
    }
  }
  pDLayer->iDLayerQp             = sLayerCtx.iDLayerQp;
  pDLayer->sSliceArgument.uiSliceMode = sLayerCtx.sSliceArgument.uiSliceMode;

  memcpy (&pDLayer->sSliceArgument, &sLayerCtx.sSliceArgument, sizeof (SSliceArgument)); // confirmed_safe_unsafe_usage
  memcpy (&pDLayer->sSliceArgument.uiSliceMbNum[0], &sLayerCtx.sSliceArgument.uiSliceMbNum[0],
          sizeof (sLayerCtx.sSliceArgument.uiSliceMbNum)); // confirmed_safe_unsafe_usage

  return 0;
}
int ParseConfig (CReadConfig& cRdCfg, SSourcePicture* pSrcPic, SEncParamExt& pSvcParam, SFilesSet& sFileSet) {
  string strTag[4];
  int32_t iRet = 0;
  int8_t iLayerCount = 0;

  while (!cRdCfg.EndOfFile()) {
    long iRd = cRdCfg.ReadLine (&strTag[0]);
    if (iRd > 0) {
      if (strTag[0].empty())
        continue;

      if (strTag[0].compare ("UsageType") == 0) {
        pSvcParam.iUsageType = (EUsageType)atoi (strTag[1].c_str());
      }else if (strTag[0].compare ("SimulcastAVC") == 0) {
        pSvcParam.bSimulcastAVC = atoi (strTag[1].c_str()) ? true : false;
      }else if (strTag[0].compare ("SourceWidth") == 0) {
        pSrcPic->iPicWidth = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("SourceHeight") == 0) {
        pSrcPic->iPicHeight = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("InputFile") == 0) {
        if (strTag[1].length() > 0)
          sFileSet.strSeqFile = strTag[1];
      } else if (strTag[0].compare ("OutputFile") == 0) {
        sFileSet.strBsFile = strTag[1];
      } else if (strTag[0].compare ("MaxFrameRate") == 0) {
        pSvcParam.fMaxFrameRate = (float)atof (strTag[1].c_str());
      } else if (strTag[0].compare ("FramesToBeEncoded") == 0) {
        sFileSet.uiFrameToBeCoded = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("TemporalLayerNum") == 0) {
        pSvcParam.iTemporalLayerNum = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("IntraPeriod") == 0) {
        pSvcParam.uiIntraPeriod = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("MaxNalSize") == 0) {
        pSvcParam.uiMaxNalSize = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("SpsPpsIDStrategy") == 0) {
        int32_t iValue = atoi (strTag[1].c_str());
        switch (iValue) {
        case 0:
          pSvcParam.eSpsPpsIdStrategy  = CONSTANT_ID;
          break;
        case 0x01:
          pSvcParam.eSpsPpsIdStrategy  = INCREASING_ID;
          break;
        case 0x02:
          pSvcParam.eSpsPpsIdStrategy  = SPS_LISTING;
          break;
        case 0x03:
          pSvcParam.eSpsPpsIdStrategy  = SPS_LISTING_AND_PPS_INCREASING;
          break;
        case 0x06:
          pSvcParam.eSpsPpsIdStrategy  = SPS_PPS_LISTING;
          break;
        default:
          pSvcParam.eSpsPpsIdStrategy  = CONSTANT_ID;
          break;
        }
      } else if (strTag[0].compare ("EnableScalableSEI") == 0) {
        pSvcParam.bEnableSSEI = atoi (strTag[1].c_str()) ? true : false;
      } else if (strTag[0].compare ("EnableFrameCropping") == 0) {
        pSvcParam.bEnableFrameCroppingFlag = (atoi (strTag[1].c_str()) != 0);
      } else if (strTag[0].compare ("EntropyCodingModeFlag") == 0) {
        pSvcParam.iEntropyCodingModeFlag = (atoi (strTag[1].c_str()) != 0);
      } else if (strTag[0].compare ("LoopFilterDisableIDC") == 0) {
        pSvcParam.iLoopFilterDisableIdc = (int8_t)atoi (strTag[1].c_str());
        if (pSvcParam.iLoopFilterDisableIdc > 6 || pSvcParam.iLoopFilterDisableIdc < 0) {
          fprintf (stderr, "Invalid parameter in iLoopFilterDisableIdc: %d.\n", pSvcParam.iLoopFilterDisableIdc);
          iRet = 1;
          break;
        }
      } else if (strTag[0].compare ("LoopFilterAlphaC0Offset") == 0) {
        pSvcParam.iLoopFilterAlphaC0Offset = (int8_t)atoi (strTag[1].c_str());
        if (pSvcParam.iLoopFilterAlphaC0Offset < -6)
          pSvcParam.iLoopFilterAlphaC0Offset = -6;
        else if (pSvcParam.iLoopFilterAlphaC0Offset > 6)
          pSvcParam.iLoopFilterAlphaC0Offset = 6;
      } else if (strTag[0].compare ("LoopFilterBetaOffset") == 0) {
        pSvcParam.iLoopFilterBetaOffset = (int8_t)atoi (strTag[1].c_str());
        if (pSvcParam.iLoopFilterBetaOffset < -6)
          pSvcParam.iLoopFilterBetaOffset = -6;
        else if (pSvcParam.iLoopFilterBetaOffset > 6)
          pSvcParam.iLoopFilterBetaOffset = 6;
      } else if (strTag[0].compare ("MultipleThreadIdc") == 0) {
        // # 0: auto(dynamic imp. internal encoder); 1: multiple threads imp. disabled; > 1: count number of threads;
        pSvcParam.iMultipleThreadIdc = atoi (strTag[1].c_str());
        if (pSvcParam.iMultipleThreadIdc < 0)
          pSvcParam.iMultipleThreadIdc = 0;
        else if (pSvcParam.iMultipleThreadIdc > MAX_THREADS_NUM)
          pSvcParam.iMultipleThreadIdc = MAX_THREADS_NUM;
      } else if (strTag[0].compare ("UseLoadBalancing") == 0) {
        pSvcParam.bUseLoadBalancing = (atoi (strTag[1].c_str())) ? true : false;
      } else if (strTag[0].compare ("RCMode") == 0) {
        pSvcParam.iRCMode = (RC_MODES) atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("TargetBitrate") == 0) {
        pSvcParam.iTargetBitrate = 1000 * atoi (strTag[1].c_str());
        if ((pSvcParam.iRCMode != RC_OFF_MODE) && pSvcParam.iTargetBitrate <= 0) {
          fprintf (stderr, "Invalid target bitrate setting due to RC enabled. Check TargetBitrate field please!\n");
          return 1;
        }
      } else if (strTag[0].compare ("MaxOverallBitrate") == 0) {
        pSvcParam.iMaxBitrate = 1000 * atoi (strTag[1].c_str());
        if ((pSvcParam.iRCMode != RC_OFF_MODE) && pSvcParam.iMaxBitrate < 0) {
          fprintf (stderr, "Invalid max overall bitrate setting due to RC enabled. Check MaxOverallBitrate field please!\n");
          return 1;
        }
      } else if (strTag[0].compare ("MaxQp") == 0) {
          pSvcParam.iMaxQp = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("MinQp") == 0) {
          pSvcParam.iMinQp = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("EnableDenoise") == 0) {
        pSvcParam.bEnableDenoise = atoi (strTag[1].c_str()) ? true : false;
      } else if (strTag[0].compare ("EnableSceneChangeDetection") == 0) {
        pSvcParam.bEnableSceneChangeDetect = atoi (strTag[1].c_str()) ? true : false;
      } else if (strTag[0].compare ("EnableBackgroundDetection") == 0) {
        pSvcParam.bEnableBackgroundDetection = atoi (strTag[1].c_str()) ? true : false;
      } else if (strTag[0].compare ("EnableAdaptiveQuantization") == 0) {
        pSvcParam.bEnableAdaptiveQuant = atoi (strTag[1].c_str()) ? true : false;
      } else if (strTag[0].compare ("EnableFrameSkip") == 0) {
        pSvcParam.bEnableFrameSkip = atoi (strTag[1].c_str()) ? true : false;
      } else if (strTag[0].compare ("EnableLongTermReference") == 0) {
        pSvcParam.bEnableLongTermReference = atoi (strTag[1].c_str()) ? true : false;
      } else if (strTag[0].compare ("LongTermReferenceNumber") == 0) {
        pSvcParam.iLTRRefNum = atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("LtrMarkPeriod") == 0) {
        pSvcParam.iLtrMarkPeriod = (uint32_t)atoi (strTag[1].c_str());
      } else if (strTag[0].compare ("LosslessLink") == 0) {
        pSvcParam.bIsLosslessLink = atoi (strTag[1].c_str()) ? true : false;
      } else if (strTag[0].compare ("NumLayers") == 0) {
        pSvcParam.iSpatialLayerNum = (int8_t)atoi (strTag[1].c_str());
        if (pSvcParam.iSpatialLayerNum > MAX_DEPENDENCY_LAYER || pSvcParam.iSpatialLayerNum <= 0) {
          fprintf (stderr, "Invalid parameter in iSpatialLayerNum: %d.\n", pSvcParam.iSpatialLayerNum);
          iRet = 1;
          break;
        }
      } else if (strTag[0].compare ("LayerCfg") == 0) {
        if (strTag[1].length() > 0)
          sFileSet.strLayerCfgFile[iLayerCount] = strTag[1];
//          pSvcParam.sDependencyLayers[iLayerCount].uiDependencyId = iLayerCount;
        ++ iLayerCount;
      } else if (strTag[0].compare ("PrefixNALAddingCtrl") == 0) {
        int ctrl_flag = atoi (strTag[1].c_str());
        if (ctrl_flag > 1)
          ctrl_flag = 1;
        else if (ctrl_flag < 0)
          ctrl_flag = 0;
        pSvcParam.bPrefixNalAddingCtrl = ctrl_flag ? true : false;
      }
    }
  }

  const int8_t kiActualLayerNum = WELS_MIN (pSvcParam.iSpatialLayerNum, iLayerCount);
  if (pSvcParam.iSpatialLayerNum >
      kiActualLayerNum) { // fixed number of dependency layer due to parameter error in settings
    pSvcParam.iSpatialLayerNum = kiActualLayerNum;
  }

  assert (kiActualLayerNum <= MAX_DEPENDENCY_LAYER);

  for (int8_t iLayer = 0; iLayer < kiActualLayerNum; ++ iLayer) {
    CReadConfig cRdLayerCfg (sFileSet.strLayerCfgFile[iLayer]);
    if (-1 == ParseLayerConfig (cRdLayerCfg, iLayer, pSvcParam, sFileSet)) {
      iRet = 1;
      break;
    }
  }

  return iRet;
}

void PrintHelp() {
  printf ("\n Wels SVC Encoder Usage:\n\n");
  printf (" Syntax: welsenc.exe welsenc.cfg\n");
}


int FillSpecificParameters (SEncParamExt& sParam) {
  /* Test for temporal, spatial, SNR scalability */
  sParam.iUsageType = CAMERA_VIDEO_REAL_TIME;
  sParam.fMaxFrameRate  = 60.0f;                // input frame rate
  sParam.iPicWidth      = 1280;                 // width of picture in samples
  sParam.iPicHeight     = 720;                  // height of picture in samples
  sParam.iTargetBitrate = 2500000;              // target bitrate desired
  sParam.iMaxBitrate    = UNSPECIFIED_BIT_RATE;
  sParam.iRCMode        = RC_QUALITY_MODE;      //  rc mode control
  sParam.iTemporalLayerNum = 3;    // layer number at temporal level
  sParam.iSpatialLayerNum  = 4;    // layer number at spatial level
  sParam.bEnableDenoise    = 0;    // denoise control
  sParam.bEnableBackgroundDetection = 1; // background detection control
  sParam.bEnableAdaptiveQuant       = 1; // adaptive quantization control
  sParam.bEnableFrameSkip           = 1; // frame skipping
  sParam.bEnableLongTermReference   = 0; // long term reference control
  sParam.iLtrMarkPeriod = 30;
  sParam.uiIntraPeriod  = 320;           // period of Intra frame
  sParam.eSpsPpsIdStrategy = INCREASING_ID;
  sParam.bPrefixNalAddingCtrl = 0;
  sParam.iComplexityMode = LOW_COMPLEXITY;
  sParam.bSimulcastAVC         = false;
  int iIndexLayer = 0;
  sParam.sSpatialLayers[iIndexLayer].uiProfileIdc       = PRO_BASELINE;
  sParam.sSpatialLayers[iIndexLayer].iVideoWidth        = 160;
  sParam.sSpatialLayers[iIndexLayer].iVideoHeight       = 90;
  sParam.sSpatialLayers[iIndexLayer].fFrameRate         = 7.5f;
  sParam.sSpatialLayers[iIndexLayer].iSpatialBitrate    = 64000;
  sParam.sSpatialLayers[iIndexLayer].iMaxSpatialBitrate    = UNSPECIFIED_BIT_RATE;
  sParam.sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

  ++ iIndexLayer;
  sParam.sSpatialLayers[iIndexLayer].uiProfileIdc       = PRO_SCALABLE_BASELINE;
  sParam.sSpatialLayers[iIndexLayer].iVideoWidth        = 320;
  sParam.sSpatialLayers[iIndexLayer].iVideoHeight       = 180;
  sParam.sSpatialLayers[iIndexLayer].fFrameRate         = 15.0f;
  sParam.sSpatialLayers[iIndexLayer].iSpatialBitrate    = 160000;
  sParam.sSpatialLayers[iIndexLayer].iMaxSpatialBitrate    = UNSPECIFIED_BIT_RATE;
  sParam.sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

  ++ iIndexLayer;
  sParam.sSpatialLayers[iIndexLayer].uiProfileIdc       = PRO_SCALABLE_BASELINE;
  sParam.sSpatialLayers[iIndexLayer].iVideoWidth        = 640;
  sParam.sSpatialLayers[iIndexLayer].iVideoHeight       = 360;
  sParam.sSpatialLayers[iIndexLayer].fFrameRate         = 30.0f;
  sParam.sSpatialLayers[iIndexLayer].iSpatialBitrate    = 512000;
  sParam.sSpatialLayers[iIndexLayer].iMaxSpatialBitrate    = UNSPECIFIED_BIT_RATE;
  sParam.sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
  sParam.sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceNum = 1;

  ++ iIndexLayer;
  sParam.sSpatialLayers[iIndexLayer].uiProfileIdc       = PRO_SCALABLE_BASELINE;
  sParam.sSpatialLayers[iIndexLayer].iVideoWidth        = 1280;
  sParam.sSpatialLayers[iIndexLayer].iVideoHeight       = 720;
  sParam.sSpatialLayers[iIndexLayer].fFrameRate         = 30.0f;
  sParam.sSpatialLayers[iIndexLayer].iSpatialBitrate    = 1500000;
  sParam.sSpatialLayers[iIndexLayer].iMaxSpatialBitrate    = UNSPECIFIED_BIT_RATE;
  sParam.sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
  sParam.sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceNum = 1;

  float fMaxFr = sParam.sSpatialLayers[sParam.iSpatialLayerNum - 1].fFrameRate;
  for (int32_t i = sParam.iSpatialLayerNum - 2; i >= 0; -- i) {
    if (sParam.sSpatialLayers[i].fFrameRate > fMaxFr + EPSN)
      fMaxFr = sParam.sSpatialLayers[i].fFrameRate;
  }
  sParam.fMaxFrameRate = fMaxFr;

  return 0;
}

int ProcessEncoding (ISVCEncoder* pPtrEnc, int argc, char** argv, bool bConfigFile) {
  int iRet = 0;
  
  if (pPtrEnc == NULL)
    return 1;
  
  SFrameBSInfo sFbi;
  SEncParamExt sSvcParam;
  int64_t iStart = 0, iTotal = 0;
  
  // Preparing encoding process
  FILE* pFileYUV = NULL;
  int32_t iActualFrameEncodedCount = 0;
  int32_t iFrameIdx = 0;
  int32_t iTotalFrameMax = -1;
  uint8_t* pYUV = NULL;
  SSourcePicture* pSrcPic = NULL;
  uint32_t iSourceWidth, iSourceHeight, kiPicResSize;
  // Inactive with sink with output file handler
  FILE* pFpBs = NULL;
#if defined(COMPARE_DATA)
  //For getting the golden file handle
  FILE* fpGolden = NULL;
#endif
#if defined ( STICK_STREAM_SIZE )
  FILE* fTrackStream = fopen ("coding_size.stream", "wb");
#endif
  SFilesSet fs;
  // for configuration file
  CReadConfig cRdCfg;
  int iParsedNum = 1;
  
  memset (&sFbi, 0, sizeof (SFrameBSInfo));
  pPtrEnc->GetDefaultParams (&sSvcParam);
  memset (&fs.sRecFileName[0][0], 0, sizeof (fs.sRecFileName));
  
  FillSpecificParameters (sSvcParam);
  pSrcPic = new SSourcePicture;
  if (pSrcPic == NULL) {
    iRet = 1;
    goto INSIDE_MEM_FREE;
  }
  //fill default pSrcPic
  pSrcPic->iColorFormat = videoFormatI420;
  pSrcPic->uiTimeStamp = 0;
  
  // if configure file exit, reading configure file firstly
  if (bConfigFile) {
    iParsedNum = 2;
    cRdCfg.Openf (argv[1]);
    if (!cRdCfg.ExistFile()) {
      fprintf (stderr, "Specified file: %s not exist, maybe invalid path or parameter settting.\n",
               cRdCfg.GetFileName().c_str());
      iRet = 1;
      goto INSIDE_MEM_FREE;
    }
    iRet = ParseConfig (cRdCfg, pSrcPic, sSvcParam, fs);
    if (iRet) {
      fprintf (stderr, "parse svc parameter config file failed.\n");
      iRet = 1;
      goto INSIDE_MEM_FREE;
    }
  }
  pPtrEnc->SetOption (ENCODER_OPTION_TRACE_LEVEL, &g_LevelSetting);
  //finish reading the configurations
  iSourceWidth = pSrcPic->iPicWidth;
  iSourceHeight = pSrcPic->iPicHeight;
  kiPicResSize = iSourceWidth * iSourceHeight * 3 >> 1;
  
  pYUV = new uint8_t [kiPicResSize];
  if (pYUV == NULL) {
    iRet = 1;
    goto INSIDE_MEM_FREE;
  }
  
  //update pSrcPic
  pSrcPic->iStride[0] = iSourceWidth;
  pSrcPic->iStride[1] = pSrcPic->iStride[2] = pSrcPic->iStride[0] >> 1;
  
  pSrcPic->pData[0] = pYUV;
  pSrcPic->pData[1] = pSrcPic->pData[0] + (iSourceWidth * iSourceHeight);
  pSrcPic->pData[2] = pSrcPic->pData[1] + (iSourceWidth * iSourceHeight >> 2);
  
  //update sSvcParam
  sSvcParam.iPicWidth = 0;
  sSvcParam.iPicHeight = 0;
  for (int iLayer = 0; iLayer < sSvcParam.iSpatialLayerNum; iLayer++) {
    SSpatialLayerConfig* pDLayer = &sSvcParam.sSpatialLayers[iLayer];
    sSvcParam.iPicWidth = WELS_MAX (sSvcParam.iPicWidth, pDLayer->iVideoWidth);
    sSvcParam.iPicHeight = WELS_MAX (sSvcParam.iPicHeight, pDLayer->iVideoHeight);
  }
  //if target output resolution is not set, use the source size
  sSvcParam.iPicWidth = (!sSvcParam.iPicWidth) ? iSourceWidth : sSvcParam.iPicWidth;
  sSvcParam.iPicHeight = (!sSvcParam.iPicHeight) ? iSourceHeight : sSvcParam.iPicHeight;
  
  iTotalFrameMax = (int32_t)fs.uiFrameToBeCoded;
  //  sSvcParam.bSimulcastAVC = true;
  if (cmResultSuccess != pPtrEnc->InitializeExt (&sSvcParam)) { // SVC encoder initialization
    fprintf (stderr, "SVC encoder Initialize failed\n");
    iRet = 1;
    goto INSIDE_MEM_FREE;
  }
  for (int iLayer = 0; iLayer < MAX_DEPENDENCY_LAYER; iLayer++) {
    if (fs.sRecFileName[iLayer][0] != 0) {
      SDumpLayer sDumpLayer;
      sDumpLayer.iLayer = iLayer;
      sDumpLayer.pFileName = fs.sRecFileName[iLayer];
      if (cmResultSuccess != pPtrEnc->SetOption (ENCODER_OPTION_DUMP_FILE, &sDumpLayer)) {
        fprintf (stderr, "SetOption ENCODER_OPTION_DUMP_FILE failed!\n");
        iRet = 1;
        goto INSIDE_MEM_FREE;
      }
    }
  }
  // Inactive with sink with output file handler
  if (fs.strBsFile.length() > 0) {
    pFpBs = fopen (fs.strBsFile.c_str(), "wb");
    if (pFpBs == NULL) {
      fprintf (stderr, "Can not open file (%s) to write bitstream!\n", fs.strBsFile.c_str());
      iRet = 1;
      goto INSIDE_MEM_FREE;
    }
  }
  
#if defined(COMPARE_DATA)
  //For getting the golden file handle
  if ((fpGolden = fopen (argv[3], "rb")) == NULL) {
    fprintf (stderr, "Unable to open golden sequence file, check corresponding path!\n");
    iRet = 1;
    goto INSIDE_MEM_FREE;
  }
#endif
  
  pFileYUV = fopen (fs.strSeqFile.c_str(), "rb");
  if (pFileYUV != NULL) {
#if defined(_WIN32) || defined(_WIN64)
#if _MSC_VER >= 1400
    if (!_fseeki64 (pFileYUV, 0, SEEK_END)) {
      int64_t i_size = _ftelli64 (pFileYUV);
      _fseeki64 (pFileYUV, 0, SEEK_SET);
      iTotalFrameMax = WELS_MAX ((int32_t) (i_size / kiPicResSize), iTotalFrameMax);
    }
#else
    if (!fseek (pFileYUV, 0, SEEK_END)) {
      int64_t i_size = ftell (pFileYUV);
      fseek (pFileYUV, 0, SEEK_SET);
      iTotalFrameMax = WELS_MAX ((int32_t) (i_size / kiPicResSize), iTotalFrameMax);
    }
#endif
#else
    if (!fseeko (pFileYUV, 0, SEEK_END)) {
      int64_t i_size = ftello (pFileYUV);
      fseeko (pFileYUV, 0, SEEK_SET);
      iTotalFrameMax = WELS_MAX ((int32_t) (i_size / kiPicResSize), iTotalFrameMax);
    }
#endif
  } else {
    fprintf (stderr, "Unable to open source sequence file (%s), check corresponding path!\n",
             fs.strSeqFile.c_str());
    iRet = 1;
    goto INSIDE_MEM_FREE;
  }
  
  iFrameIdx = 0;
  while (iFrameIdx < iTotalFrameMax && (((int32_t)fs.uiFrameToBeCoded <= 0)
                                        || (iFrameIdx < (int32_t)fs.uiFrameToBeCoded))) {
    
#ifdef ONLY_ENC_FRAMES_NUM
    // Only encoded some limited frames here
    if (iActualFrameEncodedCount >= ONLY_ENC_FRAMES_NUM) {
      break;
    }
#endif//ONLY_ENC_FRAMES_NUM
    bool bCanBeRead = false;
    bCanBeRead = (fread (pYUV, 1, kiPicResSize, pFileYUV) == kiPicResSize);
    
    if (!bCanBeRead)
      break;
    // To encoder this frame
    iStart = WelsTime();
    pSrcPic->uiTimeStamp = WELS_ROUND (iFrameIdx * (1000 / sSvcParam.fMaxFrameRate));
    int iEncFrames = pPtrEnc->EncodeFrame (pSrcPic, &sFbi);
    iTotal += WelsTime() - iStart;
    ++ iFrameIdx;
    if (videoFrameTypeSkip == sFbi.eFrameType) {
      continue;
    }
    
    if (iEncFrames == cmResultSuccess) {
      int iLayer = 0;
      int iFrameSize = 0;
      while (iLayer < sFbi.iLayerNum) {
        SLayerBSInfo* pLayerBsInfo = &sFbi.sLayerInfo[iLayer];
        if (pLayerBsInfo != NULL) {
          int iLayerSize = 0;
          int iNalIdx = pLayerBsInfo->iNalCount - 1;
          do {
            iLayerSize += pLayerBsInfo->pNalLengthInByte[iNalIdx];
            -- iNalIdx;
          } while (iNalIdx >= 0);
#if defined(COMPARE_DATA)
          //Comparing the result of encoder with golden pData
          {
            unsigned char* pUCArry = new unsigned char [iLayerSize];
            
            fread (pUCArry, 1, iLayerSize, fpGolden);
            
            for (int w = 0; w < iLayerSize; w++) {
              if (pUCArry[w] != pLayerBsInfo->pBsBuf[w]) {
                fprintf (stderr, "error @frame%d/layer%d/byte%d!!!!!!!!!!!!!!!!!!!!!!!!\n", iFrameIdx, iLayer, w);
                //fprintf(stderr, "%x - %x\n", pUCArry[w], pLayerBsInfo->pBsBuf[w]);
                break;
              }
            }
            fprintf (stderr, "frame%d/layer%d comparation completed!\n", iFrameIdx, iLayer);
            
            delete [] pUCArry;
          }
#endif
          fwrite (pLayerBsInfo->pBsBuf, 1, iLayerSize, pFpBs); // write pure bit stream into file
          iFrameSize += iLayerSize;
        }
        ++ iLayer;
      }
#if defined (STICK_STREAM_SIZE)
      if (fTrackStream) {
        fwrite (&iFrameSize, 1, sizeof (int), fTrackStream);
      }
#endif//STICK_STREAM_SIZE
      ++ iActualFrameEncodedCount; // excluding skipped frame time
    } else {
      fprintf (stderr, "EncodeFrame(), ret: %d, frame index: %d.\n", iEncFrames, iFrameIdx);
    }
    
  }
  
  if (iActualFrameEncodedCount > 0) {
    double dElapsed = iTotal / 1e6;
    printf ("Width:\t\t%d\nHeight:\t\t%d\nFrames:\t\t%d\nencode time:\t%f sec\nFPS:\t\t%f fps\n",
            sSvcParam.iPicWidth, sSvcParam.iPicHeight,
            iActualFrameEncodedCount, dElapsed, (iActualFrameEncodedCount * 1.0) / dElapsed);
#if defined (WINDOWS_PHONE)
    g_fFPS = (iActualFrameEncodedCount * 1.0f) / (float) dElapsed;
    g_dEncoderTime = dElapsed;
    g_iEncodedFrame = iActualFrameEncodedCount;
#endif
  }
INSIDE_MEM_FREE:
  if (pFpBs) {
    fclose (pFpBs);
    pFpBs = NULL;
  }
#if defined (STICK_STREAM_SIZE)
  if (fTrackStream) {
    fclose (fTrackStream);
    fTrackStream = NULL;
  }
#endif
#if defined (COMPARE_DATA)
  if (fpGolden) {
    fclose (fpGolden);
    fpGolden = NULL;
  }
#endif
  // Destruction memory introduced in this routine
  if (pFileYUV != NULL) {
    fclose (pFileYUV);
    pFileYUV = NULL;
  }
  if (pYUV) {
    delete[] pYUV;
    pYUV = NULL;
  }
  if (pSrcPic) {
    delete pSrcPic;
    pSrcPic = NULL;
  }
  return iRet;
}

//  Merge from Heifei's Wonder.  Lock process to a single core
void LockToSingleCore() {
#ifdef HAVE_PROCESS_AFFINITY
  //for 2005 compiler, change "DWORD" to "DWORD_PTR"
  ULONG_PTR ProcessAffMask = 0, SystemAffMask = 0;
  HANDLE hProcess = GetCurrentProcess();

  GetProcessAffinityMask (hProcess, &ProcessAffMask, &SystemAffMask);
  if (ProcessAffMask > 1) {
    // more than one CPU core available. Fix to only one:
    if (ProcessAffMask & 2) {
      ProcessAffMask = 2;
    } else {
      ProcessAffMask = 1;
    }
    // Lock process to a single CPU core
    SetProcessAffinityMask (hProcess, ProcessAffMask);
  }

  // set high priority to avoid interrupts during test
  SetPriorityClass (hProcess, REALTIME_PRIORITY_CLASS);
#endif
  return ;
}

int32_t CreateSVCEncHandle (ISVCEncoder** ppEncoder) {
  int32_t ret = 0;
  ret = WelsCreateSVCEncoder (ppEncoder);
  return ret;
}

void DestroySVCEncHandle (ISVCEncoder* pEncoder) {
  if (pEncoder) {
    WelsDestroySVCEncoder (pEncoder);

  }
}