// DAORecomp's log macros, onto Xenia's logger (fmt-style, as DAORecomp's are). See
// stdafx.h in this folder.

#pragma once

#include "xenia/base/logging.h"

// DAORecomp's trace level is off unless asked for; Xenia's debug level is its nearest.
#define LOGT(...) XELOGD(__VA_ARGS__)
#define LOGI(...) XELOGI(__VA_ARGS__)
#define LOGW(...) XELOGW(__VA_ARGS__)
#define LOGE(...) XELOGE(__VA_ARGS__)
