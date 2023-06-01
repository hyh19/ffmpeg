#include "encode_aac.h"

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
    if (!ifmt) {
        printf("Error, Failed to find input format!\n");
        return NULL;
    }

    ret = avformat_open_input(&ifmt_ctx, device, ifmt, &options);
    // https://bit.ly/3ZZnkkD
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

static SwrContext *create_swr_ctx(void) {
    // https://bit.ly/3Wtl4ze
    SwrContext *swr_ctx = swr_alloc_set_opts(NULL,
            AV_CH_LAYOUT_STEREO, // 输出的声道布局
            AV_SAMPLE_FMT_S16,   // 输出的采样位深
            44100,               // 输出的采样频率
            AV_CH_LAYOUT_MONO,   // 输入的声道布局
            AV_SAMPLE_FMT_FLT,   // 输入的采样位深
            48000,               // 输入的采样频率
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
    int ret = 0;

    // 输入缓冲区 https://bit.ly/3R2cLcq
    ret = av_samples_alloc_array_and_samples(src_data, // 缓冲区地址
            src_linesize,                              // 缓冲区大小
            1,                                         // 声道个数
            512,                                       // 单个声道的采样个数 2048 字节 / 4 字节（32 位）/ 1 声道= 512 采样/声道
            AV_SAMPLE_FMT_FLT,                         // 采样位深
            0);
    if (ret < 0) {
        goto __ERROR;
    }

    // 输出缓冲区
    ret = av_samples_alloc_array_and_samples(dst_data, // 缓冲区地址
            dst_linesize,                              // 缓冲区大小 512 采样/声道 * 2 字节（16 位）* 2 声道 = 2048 字节
            2,                                         // 声道个数
            512,                                       // 单个声道的采样个数
            AV_SAMPLE_FMT_S16,                         // 采样位深
            0);
    if (ret < 0) {
        goto __ERROR;
    }

    __ERROR:

    if (ret < 0) {
        printf("Error, Failed to alloc resample buffers!\n");
    }

    return ret;
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

AVCodecContext *create_codec_ctx(void) {
    AVCodec *codec = NULL;
    AVCodecContext *c = NULL;
    
    // avcodec_find_encoder(AV_CODEC_ID_AAC); https://bit.ly/3iWQyAb
    codec = avcodec_find_encoder_by_name("libfdk_aac");
    if (!codec) {
        printf("Error, Failed to find encoder!\n");
        return NULL;
    }

    // https://bit.ly/3R25DwF
    c = avcodec_alloc_context3(codec);
    if (!c) {
        printf("Error, Failed to alloc codec context!\n");
        return NULL;
    }

    c->sample_fmt = AV_SAMPLE_FMT_S16;       // 输入的采样位深
    c->channel_layout = AV_CH_LAYOUT_STEREO; // 输入的声道布局
    // codec_ctx->channels = 2;                      // 输入的声道个数
    c->sample_rate = 44100;                  // 输入的采样频率
    // codec_ctx->bit_rate = 0;                      // AAC_LC: 128K, AAC HE: 64K, AAC HE V2: 32K
    c->profile = FF_PROFILE_AAC_HE;

    // https://bit.ly/3WBBeGN
    if (avcodec_open2(c, codec, NULL) < 0) {
        avcodec_free_context(&c);
        printf("Error, Failed to open codec!\n");
        return NULL;
    }

    return c;
}

static AVFrame *create_frame(void) {
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
    int ret = 0;
    
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        return ret;
    }
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            printf("Error, Failed to receive packet!\n");
            return ret;
        }
        fwrite(pkt->data, 1, (size_t) pkt->size, outfile);
        fflush(outfile);
        av_packet_unref(pkt);
    }

    return ret;
}

// 读取数据并编码
static int read_data_and_encode(AVFormatContext *fmt_ctx, SwrContext *swr_ctx, AVCodecContext *codec_ctx, FILE *outfile) {
    int ret = 0;
    uint8_t **src_data = NULL, **dst_data = NULL;
    int src_linesize = 0, dst_linesize = 0;
    AVFrame *frame = NULL;
    AVPacket *sample_pkt = NULL, *encode_pkt = NULL;

    ret = alloc_resample_buffers(&src_data, &src_linesize, &dst_data, &dst_linesize);
    if (ret < 0) {
        goto __ERROR;
    }

    frame = create_frame();
    if (!frame) {
        ret = -1;
        goto __ERROR;
    }
    
    // 采样数据包
    sample_pkt = av_packet_alloc();
    if (!sample_pkt) {
        ret = -1;
        printf("Error, Failed to alloc sample packet!\n");
        goto __ERROR;
    }

    // 编码数据包
    encode_pkt = av_packet_alloc();
    if (!encode_pkt) {
        ret = -1;
        printf("Error, Failed to alloc encode packet!\n");
        goto __ERROR;
    }

    // 从输入设备读取数据 https://bit.ly/3HoAVK3
    while (av_read_frame(fmt_ctx, sample_pkt) == 0 &&
            rec_status) {
        av_log(NULL, AV_LOG_INFO,
                "packet size is %d (%p)\n",
                sample_pkt->size, (void *) sample_pkt->data);

        // 拷贝数据到重采样输入缓冲区
        memcpy((void *) src_data[0], (void *) sample_pkt->data, sample_pkt->size);

        av_packet_unref(sample_pkt);

        // 开始重采样 https://bit.ly/3H3SzT1
        // 512 单个声道的采样个数
        ret = swr_convert(swr_ctx, dst_data, 512, (const uint8_t **) src_data, 512);
        if (ret < 0) {
            printf("Error, Failed to convert sample!\n");
            goto __ERROR;
        }
        
        // 拷贝重采样后的数据到音频编码帧缓存区
        memcpy((void *) frame->data[0], dst_data[0], dst_linesize);

        // 开始编码
        ret = encode(codec_ctx, frame, encode_pkt, outfile);
        if (ret < 0) {
            printf("Error, Failed to encode data!\n");
            goto __ERROR;
        }
    }
    // 强制将编码器缓冲区剩余的数据编码输出
    encode(codec_ctx, NULL, encode_pkt, outfile);

    __ERROR:

    if (sample_pkt) {
        av_packet_free(&sample_pkt);
    }
    
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
void rec_audio(void) {
    AVFormatContext *fmt_ctx = NULL;
    SwrContext *swr_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const char *filename = "/Users/hyh/Downloads/audio.aac";
    FILE *outfile = NULL;
    
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();

    fmt_ctx = create_fmt_ctx();
    if (!fmt_ctx) {
        goto __ERROR;
    }

    swr_ctx = create_swr_ctx();
    if (!swr_ctx) {
        goto __ERROR;
    }

    codec_ctx = create_codec_ctx();
    if (!codec_ctx) {
        goto __ERROR;
    }

    outfile = fopen(filename, "wb+");
    if (!outfile) {
        printf("Error, Failed to open file!\n");
        goto __ERROR;
    }

    read_data_and_encode(fmt_ctx, swr_ctx, codec_ctx, outfile);

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
