#ifndef WELSENCUTIL_H_
#define WELSENCUTIL_H_

#include <iostream>
using namespace std;

typedef struct LayerpEncCtx_s {
  int32_t       iDLayerQp;
  SSliceArgument  sSliceArgument;
} SLayerPEncCtx;

typedef struct tagFilesSet {
  string strBsFile;
  string strSeqFile;    // for cmd lines
  string strLayerCfgFile[MAX_DEPENDENCY_LAYER];
  char   sRecFileName[MAX_DEPENDENCY_LAYER][MAX_FNAME_LEN];
  uint32_t uiFrameToBeCoded;
} SFilesSet;

#endif