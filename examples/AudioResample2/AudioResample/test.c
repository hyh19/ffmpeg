#include "test.h"

static int rec_status = 0;

void set_status(int status) {
    rec_status = status;
}

// 打开输入设备
static AVFormatContext *open_input_device() {
    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;
    AVInputFormat *ifmt = av_find_input_format("avfoundation");
    // ffmpeg -f avfoundation -list_devices true -i ""
    char *device = ":2";

    int ret = 0;
    if ((ret = avformat_open_input(&fmt_ctx, device, ifmt, &options)) < 0) {
        char error[1024] = {0,};
        av_strerror(ret, error, 1024);
        fprintf(stderr, "Error, Failed to open input device, [%d] %s\n", ret, error);
        return NULL;
    }

    return fmt_ctx;
}

// 创建重采样上下文
static SwrContext *create_swr_context() {
    SwrContext *swr_ctx = NULL;

    swr_ctx = swr_alloc_set_opts(NULL, // 重采样上下文
            AV_CH_LAYOUT_STEREO,       // 输出的声道布局
            AV_SAMPLE_FMT_S16,         // 输出的采样格式（采样位深/大小）
            44100,                     // 输出的采样频率
            AV_CH_LAYOUT_STEREO,       // 输入的声道布局
            AV_SAMPLE_FMT_FLT,         // 输入的采样格式（采样位深/大小）
            44100,                     // 输入的采样频率
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
static void alloc_resample_buffer(uint8_t ***src_data, int *src_linesize, uint8_t ***dst_data, int *dst_linesize) {
    // 输入缓冲区
    av_samples_alloc_array_and_samples(src_data, // 缓冲区地址
            src_linesize,                        // 缓冲区大小
            2,                                   // 声道个数
            512,                                 // 单声道采样个数 4096字节 / 4字节（32位）= 1024 / 2（声道数）= 512 个采样
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
    // 重采样输入缓冲区
    uint8_t **src_data = NULL;
    int src_linesize = 0;

    // 重采样输出缓冲区
    uint8_t **dst_data = NULL;
    int dst_linesize = 0;

    alloc_resample_buffer(&src_data, &src_linesize, &dst_data, &dst_linesize);

    AVPacket pkt;

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

// 打开编码器
AVCodecContext* open_encoder(){
    // avcodec_find_encoder(AV_CODEC_ID_AAC);
    AVCodec *codec = avcodec_find_encoder_by_name("libfdk_aac");
    
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    
    codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;          // 输入音频的采样大小
    codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;    // 输入音频的声道布局
    codec_ctx->channels = 2;                            // 输入音频的声道个数
    codec_ctx->sample_rate = 44100;                     // 输入音频的采样频率
    codec_ctx->bit_rate = 0;                            // AAC_LC: 128K, AAC HE: 64K, AAC HE V2: 32K
    codec_ctx->profile = FF_PROFILE_AAC_HE;
    
    if(avcodec_open2(codec_ctx, codec, NULL)<0){
        return NULL;
    }
    
    return codec_ctx;
}

// 创建音频帧（缓冲区）
AVFrame* create_frame(){
    AVFrame *frame = av_frame_alloc();
    
    if(!frame){
        // TODO
    }
    
    frame->nb_samples     = 512;                // 采样个数
    frame->format         = AV_SAMPLE_FMT_S16;  // 采样位深
    frame->channel_layout = AV_CH_LAYOUT_STEREO; // 声道布局
    
    av_frame_get_buffer(frame, 0); // 缓冲区大小 = 512 采样 * 2 字节 * 2 声道 = 2048 字节
    
    if(!frame->buf[0]){
        // TODO
    }
    
    return frame;
}

// 编码数据
void encode(AVCodecContext *ctx,
            AVFrame *frame,
            AVPacket *pkt,
            FILE *outfile){
    int ret;
    // 发送数据到编码器 https://bit.ly/3XzIV1A
    if ((ret = avcodec_send_frame(ctx, frame)) == 0) {
        // 获取编码后的音频数据，如果成功，需要重复获取，直到编码结束或失败为止
    // https://bit.ly/3DaPcsu
        while((ret = avcodec_receive_packet(ctx, pkt)) == 0){
            fwrite(pkt->data, 1, pkt->size, outfile);
            fflush(outfile);
        }
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            return;
        } else if( ret < 0){
            printf("Error, Failed to receive packet!\n");
            exit(-1);
        }
    }
    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
        return;
    } else if( ret < 0){
        printf("Error, Failed to send frame!\n");
        exit(-1);
    }
}

static
void read_data_and_encode(AVFormatContext *fmt_ctx, //
                          AVCodecContext *c_ctx,
                          SwrContext* swr_ctx,
                          FILE *outfile) {
    // 重采样输入缓冲区
    uint8_t **src_data = NULL;
    int src_linesize = 0;

    // 重采样输出缓冲区
    uint8_t **dst_data = NULL;
    int dst_linesize = 0;

    alloc_resample_buffer(&src_data, &src_linesize, &dst_data, &dst_linesize);

    // 音频输入数据
    AVFrame *frame = create_frame();
    // 采样数据包
    AVPacket sample_pkt;
    
    // 编码数据包
    AVPacket *encoded_pkt = av_packet_alloc();
    if(!encoded_pkt){
        // TODO
    }

    // 从输入设备读取数据
    while (av_read_frame(fmt_ctx, &sample_pkt) == 0 &&
            rec_status) {
        av_log(NULL, AV_LOG_INFO,
                "packet size is %d (%p)\n",
                sample_pkt.size, (void *) sample_pkt.data);

        // 拷贝数据到重采样输入缓冲区
        memcpy((void *) src_data[0], (void *) sample_pkt.data, sample_pkt.size);

        // 开始重采样
        swr_convert(swr_ctx,                 // 重采样上下文
                dst_data,                    // 重采样输出缓冲区
                512,                         // 输出的单声道采样个数
                (const uint8_t **) src_data, // 重采样输入缓冲区
                512);                        // 输入的单声道采样个数

        // 拷贝重采样后的数据到音频编码帧缓存区
        memcpy((void *)frame->data[0], dst_data[0], dst_linesize);
        
        // 开始编码
        encode(c_ctx, frame, encoded_pkt, outfile);

        av_packet_unref(&sample_pkt);
    }
    // 强制将编码器缓冲区中的数据编码输出
    encode(c_ctx, NULL, encoded_pkt, outfile);
    
    free_resample_buffer(src_data, dst_data);
    
    // 释放 AVFrame 和 AVPacket
    av_frame_free(&frame);
    av_packet_free(&encoded_pkt);
}

// 录制音频
void rec_audio() {
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();

    AVFormatContext *fmt_ctx = open_input_device();
    if (!fmt_ctx) {
        goto __ERROR;
    }

    SwrContext *swr_ctx = create_swr_context();
    if (!swr_ctx) {
        goto __ERROR;
    }
    
    //打开编码器上下文
    AVCodecContext* c_ctx = open_encoder();


    const char *filename = "/Users/hyh/Downloads/audio.aac";
    FILE *outfile = fopen(filename, "wb+");
    if (!outfile) {
        printf("Error, Failed to open file!\n");
        goto __ERROR;
    }

    // read_data_and_resample(fmt_ctx, swr_ctx, outfile);
    read_data_and_encode(fmt_ctx, c_ctx, swr_ctx, outfile);
    
    __ERROR:

    if (outfile) {
        fclose(outfile);
    }
    
    if(c_ctx){
        avcodec_free_context(&c_ctx);
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
