#include "test.h"

static int rec_status = 0;

void set_status(int status) {
    rec_status = status;
}

static AVFormatContext *create_fmt_context(void) {
    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;

    // show available devices
    // ffmpeg -devices
    AVInputFormat *fmt = av_find_input_format("avfoundation");
    if (!fmt) {
        printf("Error, Failed to find input format!\n");
        return NULL;
    }

    // https://ffmpeg.org/ffmpeg-devices.html#avfoundation
    av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "framerate", "30", 0);
    // show available pixel formats
    // ffmpeg -pix_fmts
    av_dict_set(&options, "pixel_format", "nv12", 0);

    // https://ffmpeg.org/ffmpeg-devices.html#avfoundation
    // Print the list of AVFoundation supported devices and exit:
    // ffmpeg -f avfoundation -list_devices true -i ""
    char *device = "default";

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

static AVCodecContext *create_codec_context(void) {
    // https://bit.ly/3iWQyAb
    AVCodec *codec = avcodec_find_encoder_by_name("libx264");

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

    // SPS/PPS
    codec_ctx->profile = FF_PROFILE_H264_HIGH_444;
    codec_ctx->level = 50; // 表示 LEVEL 是 5.0
    
    // 分辫率
    codec_ctx->width = 640;
    codec_ctx->height = 480;
    
    // GOP
    codec_ctx->gop_size = 250;
    codec_ctx->keyint_min = 25;  // Optional
    
    // B 帧数据
    codec_ctx->max_b_frames = 3; // Optional
    codec_ctx->has_b_frames = 1; // Optional
    
    // 参考帧的数量
    codec_ctx->refs = 3;         // Optional
    
    // 输入的 YUV 格式
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    
    // 码率
    codec_ctx->bit_rate = 600000; // 600 kbps
        
    // 帧率
    codec_ctx->time_base = (AVRational){1, 25}; // 帧与帧之间的时间间隔
    codec_ctx->framerate = (AVRational){25, 1}; // 每秒 25 帧

    // https://bit.ly/3WBBeGN
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&codec_ctx);
        printf("Error, Failed to open codec!\n");
        return NULL;
    }

    return codec_ctx;
}

static AVFrame *create_frame(void) {
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        printf("Error, Failed to alloc frame!\n");
        return NULL;
    }

    frame->width = 640;
    frame->height = 480;
    frame->format = AV_PIX_FMT_YUV420P;

    av_frame_get_buffer(frame, 0);
    if (!frame->data[0]) {
        av_frame_free(&frame);
        printf("Error, Failed to get frame buffer!\n");
        return NULL;
    }

    return frame;
}

static int encode(AVCodecContext *c, AVFrame *frame, AVPacket *pkt, FILE *outfile) {
    int ret = 0;
    
    // 发送数据到编码器 https://bit.ly/3XzIV1A
    ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        return ret;
    }
    
    while (ret >= 0) {
        // 获取编码后的数据 https://bit.ly/3DaPcsu
        ret = avcodec_receive_packet(c, pkt);
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
static int read_data_and_encode(AVFormatContext *fmt_ctx, AVCodecContext *codec_ctx, FILE *outfile) {
    int ret = 0;

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
    
    int base = 0;

    // 从输入设备读取数据 https://bit.ly/3HoAVK3
    while (av_read_frame(fmt_ctx, &sample_pkt) == 0 &&
            rec_status) {
        av_log(NULL, AV_LOG_INFO,
                "packet size is %d (%p)\n",
                sample_pkt.size, (void *) sample_pkt.data);

        // YYYYYYYYUVVU NV12
        // YYYYYYYYUUVV YUV420
        memcpy(frame->data[0], sample_pkt.data, 307200); // Copy Y data
        // 307200 之后是 UV
        for(int i=0; i < 307200/4; ++i){
            frame->data[1][i] = sample_pkt.data[307200+i*2];
            frame->data[2][i] = sample_pkt.data[307201+i*2];
        }
        
        fwrite(frame->data[0], 1, 307200, outfile);
        fwrite(frame->data[1], 1, 307200/4, outfile);
        fwrite(frame->data[2], 1, 307200/4, outfile);
        
        frame->pts = base++;

        // 开始编码
        if (encode(codec_ctx, frame, encode_pkt, outfile) < 0) {
            ret = -1;
            printf("Error, Failed to encode data!\n");
            goto __ERROR;
        }
        
        av_packet_unref(&sample_pkt);
    }
    // 强制将编码器缓冲区剩余的数据编码输出
    encode(codec_ctx, NULL, encode_pkt, outfile);

    __ERROR:

    if (encode_pkt) {
        av_packet_free(&encode_pkt);
    }

    if (frame) {
        av_frame_free(&frame);
    }

    return ret;
}

static FILE *open_file(const char *filename) {
    FILE *f = fopen(filename, "wb+");
    if (!f) {
        printf("Error, Failed to open file!\n");
        return NULL;
    }
    return f;
}

void rec_video(void) {
    rec_status = 1;

    av_log_set_level(AV_LOG_DEBUG);

    avdevice_register_all();
    
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    FILE *outfile = NULL;

    fmt_ctx = create_fmt_context();
    if (!fmt_ctx) {
        goto __ERROR;
    }

    codec_ctx = create_codec_context();
    if (!codec_ctx) {
        goto __ERROR;
    }

    outfile = open_file("/Users/hyh/Downloads/video.h264");
    if (!outfile) {
        goto __ERROR;
    }

    read_data_and_encode(fmt_ctx, codec_ctx, outfile);

    __ERROR:

    if (outfile) {
        fclose(outfile);
    }

    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
    }

    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
    }

    av_log(NULL, AV_LOG_DEBUG, "finish!\n");
}
