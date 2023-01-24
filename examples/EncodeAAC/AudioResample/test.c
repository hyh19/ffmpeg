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

// 创建重采样上下文
static SwrContext *create_swr_context() {
    // https://bit.ly/3Wtl4ze
    SwrContext *swr_ctx = swr_alloc_set_opts(NULL,
            AV_CH_LAYOUT_STEREO, // 输出的声道布局
            AV_SAMPLE_FMT_S16,   // 输出的采样位深
            44100,               // 输出的采样频率
            AV_CH_LAYOUT_STEREO, // 输入的声道布局
            AV_SAMPLE_FMT_FLT,   // 输入的采样位深
            44100,               // 输入的采样频率
            0, NULL);

    if (!swr_ctx) {
        printf("Error, Failed to alloc swr context!\n");
        return NULL;
    }

    // https://bit.ly/3XAKpsh
    if (swr_init(swr_ctx) < 0) {
        swr_free(&swr_ctx);
        printf("Error, Failed to init swr context!\n");
        return NULL;
    }

    return swr_ctx;
}

// 分配重采样的输入/输出缓冲区
static int alloc_resample_buffers(uint8_t ***src_data, int *src_linesize, uint8_t ***dst_data, int *dst_linesize) {
    int ret;

    // 输入缓冲区 https://bit.ly/3R2cLcq
    ret = av_samples_alloc_array_and_samples(src_data, // 缓冲区地址
            src_linesize,                        // 缓冲区大小
            2,                                   // 声道个数
            512,                                 // 单个声道的采样个数 4096 字节 / 4 字节（32 位）/ 2 声道= 512 采样/声道
            AV_SAMPLE_FMT_FLT,                   // 采样位深
            0);
    if (ret < 0) {
        goto __ERROR;
    }

    // 输出缓冲区
    av_samples_alloc_array_and_samples(dst_data, // 缓冲区地址
            dst_linesize,                        // 缓冲区大小 512 采样/声道 * 2 字节（16 位）* 2 声道 = 2048 字节
            2,                                   // 声道个数
            512,                                 // 单个声道的采样个数
            AV_SAMPLE_FMT_S16,                   // 采样位深
            0);
    if (ret < 0) {
        goto __ERROR;
    }

    return 0;

    __ERROR:

    printf("Error, Failed to alloc resample buffers!\n");

    return -1;
}

// 释放重采样的输入/输出缓冲区
static void free_resample_buffers(uint8_t **src_data, uint8_t **dst_data) {
    if (src_data) {
        av_freep(&src_data[0]);
    }
    av_freep(src_data);

    if (dst_data) {
        av_freep(&dst_data[0]);
    }
    av_freep(dst_data);
}

// 创建编码上下文（绑定编码器）
AVCodecContext *create_codec_context() {
    // avcodec_find_encoder(AV_CODEC_ID_AAC); https://bit.ly/3iWQyAb
    AVCodec *codec = avcodec_find_encoder_by_name("libfdk_aac");

    if (!codec) {
        printf("Error, Failed to find encoder!\n");
        return NULL;
    }

    // https://bit.ly/3R25DwF
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        printf("Error, Failed to alloc codec context!\n");
        return NULL;
    }

    codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;       // 输入的采样位深
    codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO; // 输入的声道布局
    codec_ctx->channels = 2;                         // 输入的声道个数
    codec_ctx->sample_rate = 44100;                  // 输入的采样频率
    codec_ctx->bit_rate = 0;                         // AAC_LC: 128K, AAC HE: 64K, AAC HE V2: 32K
    codec_ctx->profile = FF_PROFILE_AAC_HE;

    // https://bit.ly/3WBBeGN
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&codec_ctx);
        printf("Error, Failed to open encoder!\n");
        return NULL;
    }

    return codec_ctx;
}

// 创建音频帧（缓冲区）
static AVFrame *create_frame() {
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        printf("Error, Failed to alloc frame!\n");
        return NULL;
    }

    frame->nb_samples = 512;                     // 单个声道的采样个数
    frame->format = AV_SAMPLE_FMT_S16;           // 采样位深
    frame->channel_layout = AV_CH_LAYOUT_STEREO; // 声道布局

    av_frame_get_buffer(frame, 0); // 缓冲区大小 = 512 采样 * 2 字节（32 位）* 2 声道 = 2048 字节
    if (!frame->data[0]) {
        av_frame_free(&frame);
        printf("Error, Failed to get frame buffer!\n");
        return NULL;
    }

    return frame;
}

// 编码数据
static int encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt, FILE *outfile) {
    int ret;
    // 发送数据到编码器 https://bit.ly/3XzIV1A
    if ((ret = avcodec_send_frame(ctx, frame)) == 0) {
        // 获取编码后的数据 https://bit.ly/3DaPcsu
        while ((ret = avcodec_receive_packet(ctx, pkt)) == 0) {
            fwrite(pkt->data, 1, (size_t) pkt->size, outfile);
            fflush(outfile);
        }
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            printf("Error, Failed to receive packet!\n");
            return -1;
        }
    } else {
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            printf("Error, Failed to send frame!\n");
            return -1;
        }
    }
    return 0;
}

// 读取数据并编码
static int read_data_and_encode(AVFormatContext *fmt_ctx, AVCodecContext *codec_ctx, SwrContext *swr_ctx, FILE *outfile) {
    int ret = 0;

    // 重采样的输入缓冲区
    uint8_t **src_data = NULL;
    int src_linesize = 0;

    // 重采样的输出缓冲区
    uint8_t **dst_data = NULL;
    int dst_linesize = 0;

    if (alloc_resample_buffers(&src_data, &src_linesize, &dst_data, &dst_linesize) < 0) {
        ret = -1;
        goto __ERROR;
    }

    AVFrame *frame = create_frame();
    if (!frame) {
        ret = -1;
        goto __ERROR;
    }

    // 编码数据包
    AVPacket *encode_pkt = av_packet_alloc();
    if (!encode_pkt) {
        ret = -1;
        printf("Error, Failed to alloc encode packet!\n");
        goto __ERROR;
    }

    // 采样数据包
    AVPacket sample_pkt;

    // 从输入设备读取数据 https://bit.ly/3HoAVK3
    while (av_read_frame(fmt_ctx, &sample_pkt) == 0 &&
            rec_status) {
        av_log(NULL, AV_LOG_INFO,
                "packet size is %d (%p)\n",
                sample_pkt.size, (void *) sample_pkt.data);

        // 拷贝数据到重采样输入缓冲区
        memcpy((void *) src_data[0], (void *) sample_pkt.data, sample_pkt.size);

        av_packet_unref(&sample_pkt);

        // 开始重采样 https://bit.ly/3H3SzT1
        // 512 单个声道的采样个数
        if (swr_convert(swr_ctx, dst_data, 512, (const uint8_t **) src_data, 512) < 0) {
            ret = -1;
            printf("Error, Failed to convert sample!\n");
            goto __ERROR;
        }
        // 拷贝重采样后的数据到音频编码帧缓存区
        memcpy((void *) frame->data[0], dst_data[0], dst_linesize);

        // 开始编码
        if (encode(codec_ctx, frame, encode_pkt, outfile) < 0) {
            ret = -1;
            printf("Error, Failed to encode data!\n");
            goto __ERROR;
        }
    }
    // 强制将编码器缓冲区剩余的数据编码输出
    if (encode(codec_ctx, frame, encode_pkt, outfile) < 0) {
        ret = -1;
        printf("Error, Failed to encode data!\n");
        goto __ERROR;
    }

    __ERROR:

    if (encode_pkt) {
        av_packet_free(&encode_pkt);
    }

    if (frame) {
        av_frame_free(&frame);
    }

    free_resample_buffers(src_data, dst_data);

    return ret;
}

// 录制音频
void rec_audio() {
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();

    AVFormatContext *fmt_ctx = create_fmt_context();
    if (!fmt_ctx) {
        goto __ERROR;
    }

    SwrContext *swr_ctx = create_swr_context();
    if (!swr_ctx) {
        goto __ERROR;
    }

    AVCodecContext *codec_ctx = create_codec_context();
    if (!codec_ctx) {
        goto __ERROR;
    }

    const char *filename = "/Users/hyh/Downloads/audio.aac";
    FILE *outfile = fopen(filename, "wb+");
    if (!outfile) {
        printf("Error, Failed to open file!\n");
        goto __ERROR;
    }

    read_data_and_encode(fmt_ctx, codec_ctx, swr_ctx, outfile);

    __ERROR:

    if (outfile) {
        fclose(outfile);
    }

    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
    }

    if (swr_ctx) {
        swr_free(&swr_ctx);
    }

    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
    }

    av_log(NULL, AV_LOG_DEBUG, "finish!\n");

    return;
}
