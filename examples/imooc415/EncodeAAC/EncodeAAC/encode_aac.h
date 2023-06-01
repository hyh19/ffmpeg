#ifndef encode_aac_h
#define encode_aac_h

#include <stdio.h>
#include "libavutil/avutil.h"
#include "libavutil/time.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"

void set_status(int status);
void rec_audio(void);

#endif /* encode_aac_h */
