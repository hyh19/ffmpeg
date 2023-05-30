#include "test.h"

static int rec_status = 0;

void set_status(int status) {
    rec_status = status;
}

// 创建重采样上下文
SwrContext *create_swr_ctx(void) {
    SwrContext *swr_ctx = NULL;

    swr_ctx = swr_alloc_set_opts(NULL,
            AV_CH_LAYOUT_STEREO, // 输出的 Channel布局
            AV_SAMPLE_FMT_S16,   // 输出的采样格式（采样位深/大小）
            44100,               // 输出的采样频率
            AV_CH_LAYOUT_MONO,   // 输入的 Channel布局 4
            AV_SAMPLE_FMT_FLT,   // 输入的采样格式（采样位深/大小）
            48000,               // 输入的采样频率
            0, NULL);

    if (!swr_ctx) {
        exit(1);
    }

    if (swr_init(swr_ctx) < 0) {
        exit(1);
    }

    return swr_ctx;
}

void rec_audio(void) {
    int ret = 0;
    char error[1024] = {0,};
    AVInputFormat *in_fmt = NULL;
    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;
    // ffmpeg -f avfoundation -list_devices true -i ""
    // [[video device]:[audio device]]
    const char *device = ":default";
    AVPacket pkt;
    FILE *out_file = NULL;
    SwrContext *swr_ctx = NULL;
    uint8_t **src_data = NULL; // 重采样输入缓冲区
    int src_linesize = 0;
    uint8_t **dst_data = NULL; // 重采样输出缓冲区
    int dst_linesize = 0;
    
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();

    in_fmt = av_find_input_format("avfoundation");

    ret = avformat_open_input(&fmt_ctx, device, in_fmt, &options);
    if (ret < 0) {
        av_strerror(ret, error, 1024);
        fprintf(stderr, "Failed to open audio device, [%d] %s\n", ret, error);
        return;
    }

    // create file
    out_file = fopen("/Users/hyh/Downloads/resample.pcm", "wb+");

    swr_ctx = create_swr_ctx();

    // 创建输入缓冲区
    av_samples_alloc_array_and_samples(&src_data, // 缓冲区地址
            &src_linesize,     // 缓冲区大小
            1,                 // 通道个数
            512,               // 单通道采样个数 2048字节 / 4字节（32位）= 512 / 1（通道数）= 512 个采样
            AV_SAMPLE_FMT_FLT, // 采样格式
            0);

    // 创建输出缓冲区
    av_samples_alloc_array_and_samples(&dst_data, // 缓冲区地址
            &dst_linesize,     // 缓冲区大小
            2,                 // 通道个数
            512,               // 单通道采样个数
            AV_SAMPLE_FMT_S16, // 采样格式
            0);

    // read data from device
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
                512,                         // 输出的单通道采样个数
                (const uint8_t **) src_data, // 重采样输入缓冲区
                512);                        // 输入的单通道采样个数

        // write file
        fwrite(dst_data[0], 1, (size_t) dst_linesize, out_file);
        fflush(out_file);

        av_packet_unref(&pkt);
    }

    fclose(out_file);

    // 释放重采样输入/输出缓冲区
    if (src_data) {
        av_freep(&src_data[0]);
    }
    av_freep(src_data);

    if (dst_data) {
        av_freep(&dst_data[0]);
    }
    av_freep(dst_data);

    // 释放重采样上下文
    swr_free(&swr_ctx);

    // close device and release ctx
    avformat_close_input(&fmt_ctx);

    av_log(NULL, AV_LOG_DEBUG, "finish!\n");
}
