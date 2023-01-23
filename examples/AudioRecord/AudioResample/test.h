#ifndef test_h
#define test_h

#include <stdio.h>
#include "libavutil/avutil.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"

void set_status(int status);
void rec_audio(void);

#endif /* test_h */
