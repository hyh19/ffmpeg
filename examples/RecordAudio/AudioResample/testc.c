//
//  testc.c
//  myapp
//
//  Created by lichao on 2020/1/30.
//  Copyright © 2020年 lichao. All rights reserved.
//

#include "testc.h"

static int rec_status = 0;

void set_status(int status) {
    rec_status = status;
}

void rec_audio() {
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();

    AVFormatContext *fmt_ctx = NULL;

    // ffmpeg -f avfoundation -list_devices true -i ""
    // [[video device]:[audio device]]
    char *device = ":2";

    AVInputFormat *in_fmt = av_find_input_format("avfoundation");

    AVDictionary *options = NULL;

    int ret = 0;
    char error[1024] = {0,};

    if ((ret = avformat_open_input(&fmt_ctx, device, in_fmt, &options)) < 0) {
        av_strerror(ret, error, 1024);
        fprintf(stderr, "Failed to open audio device, [%d] %s\n", ret, error);
        return;
    }

    AVPacket pkt;

    // create file
    FILE *out_file = fopen("/Users/hyh/Downloads/audio.pcm", "wb+");

    // read data from device
    while (av_read_frame(fmt_ctx, &pkt) == 0 &&
            rec_status) {
        av_log(NULL, AV_LOG_INFO,
                "packet size is %d (%p)\n",
                pkt.size, (void *) pkt.data);

        // write file
        fwrite(pkt.data, 1, (size_t) pkt.size, out_file);
        fflush(out_file);

        av_packet_unref(&pkt);
    }

    fclose(out_file);

    // close device and release ctx
    avformat_close_input(&fmt_ctx);

    av_log(NULL, AV_LOG_DEBUG, "finish!\n");
}
