#include "test.h"

static int rec_status = 0;

void set_status(int status) {
    rec_status = status;
}

static AVFormatContext *create_fmt_ctx(void) {
    int ret = 0;
    AVInputFormat *ifmt = NULL;
    AVFormatContext *ifmt_ctx = NULL;
    AVDictionary *options = NULL;
    // ffmpeg -f avfoundation -list_devices true -i ""
    const char *device_desc = ":default";

    ifmt = av_find_input_format("avfoundation");
    if (!ifmt) {
        printf("Error, Failed to find input format!\n");
        return NULL;
    }

    // https://bit.ly/3ZZnkkD
    ret = avformat_open_input(&ifmt_ctx, device_desc, ifmt, &options);
    if (ret < 0) {
        printf("Error, Failed to open input format!\n");
        // TODO
        // char error[1024] = {0,};
        // av_strerror(ret, error, 1024);
        // fprintf(stderr, "[%d] %s\n", ret, error);
        return NULL;
    }

    return ifmt_ctx;
}

static int read_data(AVFormatContext *ifmt_ctx, FILE *outfile) {
    int ret = 0;
    AVPacket pkt;
    
    // https://bit.ly/3HoAVK3
    while ((ret = av_read_frame(ifmt_ctx, &pkt)) == 0 &&
            rec_status) {
        av_log(NULL, AV_LOG_INFO,
                "packet size is %d (%p)\n",
                pkt.size, (void *) pkt.data);

        fwrite(pkt.data, 1, (size_t) pkt.size, outfile);
        fflush(outfile);

        av_packet_unref(&pkt);
    }
    if (ret < 0 && ret != AVERROR_EOF) {
        printf("Error, Failed to read frame!\n");
    }
    
    return ret;
}

void rec_audio(void) {
    AVFormatContext *ifmt_ctx = NULL;
    const char *filename = "/Users/hyh/Downloads/audio.pcm";
    FILE *outfile = NULL;
    
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();

    ifmt_ctx = create_fmt_ctx();
    if (!ifmt_ctx) {
        goto __ERROR;
    }
    
    outfile = fopen(filename, "wb+");
    if (!outfile) {
        printf("Error, Failed to open file!\n");
        goto __ERROR;
    }

    if (read_data(ifmt_ctx, outfile) < 0) {
        goto __ERROR;
    }

    __ERROR:

    if (outfile) {
        fclose(outfile);
    }

    if (ifmt_ctx) {
        avformat_close_input(&ifmt_ctx);
    }

    av_log(NULL, AV_LOG_DEBUG, "finish!\n");
}
