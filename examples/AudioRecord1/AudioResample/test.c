#include "test.h"

static int rec_status = 0;

void set_status(int status) {
    rec_status = status;
}

// 创建输入上下文（绑定输入设备）
static AVFormatContext *create_fmt_context() {
    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;

    AVInputFormat *fmt = av_find_input_format("avfoundation");
    if (!fmt) {
        printf("Error, Failed to find input format!\n");
        return NULL;
    }

    // ffmpeg -f avfoundation -list_devices true -i ""
    char *device = ":2";

    int ret;
    // https://bit.ly/3ZZnkkD
    if ((ret = avformat_open_input(&fmt_ctx, device, fmt, &options)) < 0) {
        printf("Error, Failed to open input format!\n");
        // TODO
        // char error[1024] = {0,};
        // av_strerror(ret, error, 1024);
        // fprintf(stderr, "[%d] %s\n", ret, error);
        return NULL;
    }

    return fmt_ctx;
}

// 读取数据
static int read_data(AVFormatContext *fmt_ctx, FILE *outfile) {
    int ret = 0;
    
    AVPacket pkt;
    
    // 从输入设备读取数据 https://bit.ly/3HoAVK3
    while ((ret = av_read_frame(fmt_ctx, &pkt)) == 0 &&
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

void rec_audio() {
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();

    AVFormatContext *fmt_ctx = create_fmt_context();
    if (!fmt_ctx) {
        goto __ERROR;
    }
    
    const char *filename = "/Users/hyh/Downloads/audio.pcm";
    FILE *outfile = fopen(filename, "wb+");
    if (!outfile) {
        printf("Error, Failed to open file!\n");
        goto __ERROR;
    }

    if (read_data(fmt_ctx, outfile) < 0) {
        goto __ERROR;
    }

    __ERROR:

    if (outfile) {
        fclose(outfile);
    }

    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
    }

    av_log(NULL, AV_LOG_DEBUG, "finish!\n");
}
