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
    const char *device = ":default";

    ifmt = av_find_input_format("avfoundation");
    
    ret = avformat_open_input(&ifmt_ctx, device, ifmt, &options);
    if (ret < 0) {
        char error[1024] = {0,};
        av_strerror(ret, error, 1024);
        fprintf(stderr, "Error, Failed to open input device, [%d] %s\n", ret, error);
        return NULL;
    }

    return ifmt_ctx;
}

static SwrContext *create_swr_ctx(void) {
    SwrContext *swr_ctx = NULL;

    swr_ctx = swr_alloc_set_opts(NULL, // 重采样上下文
            AV_CH_LAYOUT_STEREO,       // 输出的声道布局
            AV_SAMPLE_FMT_S16,         // 输出的采样格式（采样位深/大小）
            44100,                     // 输出的采样频率
            AV_CH_LAYOUT_MONO,         // 输入的声道布局 4
            AV_SAMPLE_FMT_FLT,         // 输入的采样格式（采样位深/大小）
            48000,                     // 输入的采样频率
            0, NULL);

    if (!swr_ctx) {
        printf("Error, Failed to alloc swr context!\n");
    }

    if (swr_init(swr_ctx) < 0) {
        printf("Error, Failed to init swr context!\n");
    }

    return swr_ctx;
}

// 分配重采样输入/输出缓冲区
static void alloc_resample_buffers(uint8_t ***src_data, int *src_linesize, uint8_t ***dst_data, int *dst_linesize) {
    // 输入缓冲区
    av_samples_alloc_array_and_samples(src_data, // 缓冲区地址
            src_linesize,                        // 缓冲区大小
            1,                                   // 声道个数
            512,                                 // 单声道采样个数 2048字节 / 4字节（32位）= 1024 / 1（声道数）= 512 个采样
            AV_SAMPLE_FMT_FLT,                   // 采样格式
            0);

    // 输出缓冲区
    av_samples_alloc_array_and_samples(dst_data, // 缓冲区地址
            dst_linesize,                        // 缓冲区大小
            2,                                   // 声道个数
            512,                                 // 单声道采样个数
            AV_SAMPLE_FMT_S16,                   // 采样格式
            0);
}

// 释放重采样输入/输出缓冲区
static void free_resample_buffer(uint8_t **src_data, uint8_t **dst_data) {
    if (src_data) {
        av_freep(&src_data[0]);
    }
    av_freep(src_data);
    if (dst_data) {
        av_freep(&dst_data[0]);
    }
    av_freep(dst_data);
}

// 读数据 -> 重采样 -> 写文件
static void read_data_and_resample(AVFormatContext *fmt_ctx, SwrContext *swr_ctx, FILE *outfile) {
    uint8_t **src_data = NULL, **dst_data = NULL;
    int src_linesize = 0, dst_linesize = 0;
    AVPacket pkt;

    alloc_resample_buffers(&src_data, &src_linesize, &dst_data, &dst_linesize);

    // 从输入设备读取数据
    while (av_read_frame(fmt_ctx, &pkt) == 0 &&
            rec_status) {
        av_log(NULL, AV_LOG_INFO,
                "packet size is %d (%p)\n",
                pkt.size, (void *) pkt.data);

        // 拷贝数据到重采样输入缓冲区
        memcpy((void *) src_data[0], (void *) pkt.data, pkt.size);

        // 开始重采样
        swr_convert(swr_ctx,                 // 重采样上下文
                dst_data,                    // 重采样输出缓冲区
                512,                         // 输出的单声道采样个数
                (const uint8_t **) src_data, // 重采样输入缓冲区
                512);                        // 输入的单声道采样个数

        fwrite(dst_data[0], 1, (size_t) dst_linesize, outfile);
        fflush(outfile);

        av_packet_unref(&pkt);
    }
    
    free_resample_buffer(src_data, dst_data);
}

// 录制音频
void rec_audio(void) {
    AVFormatContext *ifmt_ctx = NULL;
    SwrContext *swr_ctx = NULL;
    const char *filename = NULL;
    FILE *outfile = NULL;
    
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();

    ifmt_ctx = create_fmt_ctx();
    if (!ifmt_ctx) {
        goto __ERROR;
    }

    swr_ctx = create_swr_ctx();
    if (!swr_ctx) {
        goto __ERROR;
    }

    filename = "/Users/hyh/Downloads/resample.pcm";
    outfile = fopen(filename, "wb+");
    if (!outfile) {
        printf("Error, Failed to open file!\n");
        goto __ERROR;
    }

    read_data_and_resample(ifmt_ctx, swr_ctx, outfile);

    __ERROR:

    if (outfile) {
        fclose(outfile);
    }

    if (swr_ctx) {
        swr_free(&swr_ctx);
    }

    if (ifmt_ctx) {
        avformat_close_input(&ifmt_ctx);
    }

    av_log(NULL, AV_LOG_DEBUG, "finish!\n");

    return;
}
