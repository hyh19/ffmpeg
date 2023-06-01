#include "audio_record.h"

static int rec_status = 0;

void set_status(int status) {
    rec_status = status;
}

void rec_audio(void) {
    int ret = 0;
    char error[1024] = {0,};
    AVInputFormat *ifmt = NULL;
    AVFormatContext *ifmt_ctx = NULL;
    AVDictionary *options = NULL;
    // ffmpeg -f avfoundation -list_devices true -i ""
    // [[video device]:[audio device]]
    const char *device_desc = ":default";
    AVPacket pkt;
    FILE *outfile = NULL;
    
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();

    ifmt = av_find_input_format("avfoundation");

    ret = avformat_open_input(&ifmt_ctx, device_desc, ifmt, &options);
    if (ret < 0) {
        av_strerror(ret, error, 1024);
        fprintf(stderr, "Failed to open audio device, [%d] %s\n", ret, error);
        return;
    }

    // create file
    outfile = fopen("/Users/hyh/Downloads/audio.pcm", "wb+");

    // read data from device
    while (av_read_frame(ifmt_ctx, &pkt) == 0 &&
            rec_status) {
        av_log(NULL, AV_LOG_INFO,
                "packet size is %d (%p)\n",
                pkt.size, (void *) pkt.data);

        // write file
        fwrite(pkt.data, 1, (size_t) pkt.size, outfile);
        fflush(outfile);
        av_packet_unref(&pkt);
    }

    fclose(outfile);

    // close device and release ctx
    avformat_close_input(&ifmt_ctx);

    av_log(NULL, AV_LOG_DEBUG, "finish!\n");
}
