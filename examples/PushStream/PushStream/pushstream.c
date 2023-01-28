#include "pushstream.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <librtmp/rtmp.h>

static int status = 1;

void set_status(int state) {
    status = state;
}

// 打开 flv 文件
static FILE *open_flv(const char *filename) {
    FILE *fp = NULL;

    fp = fopen(filename, "rb");
    if (!fp) {
        printf("Error, failed to open flv: %s!\n", filename);
        return NULL;
    }

    fseek(fp, 9, SEEK_SET); // 跳过 9 字节的 FLV Header
    fseek(fp, 4, SEEK_CUR); // 跳过 4 字节的 PreTagSize

    return fp;
}

// 连接 RTMP 服务器
static RTMP *connect_rtmp_server(char *url) {
    RTMP *rtmp = NULL;

    // 1、创建 RTMP 对象，并进行初始化。
    rtmp = RTMP_Alloc();
    if (!rtmp) {
        printf("Error, failed to alloc RTMP object!\n");
        goto __ERROR;
    }

    RTMP_Init(rtmp);

    // 2、设置 RTMP 服务器地址和连接超时
    rtmp->Link.timeout = 10;
    RTMP_SetupURL(rtmp, url);

    // 3、设置为推流，未调用该函数则为拉流。
    RTMP_EnableWrite(rtmp);

    // 4、建立连接
    if (!RTMP_Connect(rtmp, NULL)) {
        printf("Error, failed to connect to RTMP server!\n");
        goto __ERROR;
    }

    // 5、Create stream
    RTMP_ConnectStream(rtmp, 0);

    return rtmp;

    __ERROR:

    // 释放资源
    if (rtmp) {
        RTMP_Close(rtmp);
        RTMP_Free(rtmp);
    }

    return NULL;
}

// 分配 RTMPPacket 空间
static RTMPPacket *alloc_packet() {
    RTMPPacket *packet = NULL;

    packet = malloc(sizeof(RTMPPacket));

    if (!packet || !RTMPPacket_Alloc(packet, 64 * 1024)) {
        if (packet) {
            free(packet);
        }
        printf("Error, failed to alloc RTMPPacket!\n");
        return NULL;
    }

    RTMPPacket_Reset(packet);

    packet->m_hasAbsTimestamp = 0;
    packet->m_nChannel = 0x4;

    return packet;
}

static int read_u8(FILE *fp, unsigned int *u8) {
    unsigned int tmp;
    if (fread(&tmp, 1, 1, fp) != 1) {
        printf("Failed to read_u8!\n");
        return -1;
    }
    *u8 = tmp & 0xFF;
    return 0;
}

static int read_u24(FILE *fp, unsigned int *u24) {
    unsigned int tmp;
    if (fread(&tmp, 1, 3, fp) != 3) {
        printf("Failed to read_u24!\n");
        return -1;
    }
    *u24 = ((tmp >> 16) & 0xFF) | ((tmp << 16) & 0xFF0000) | (tmp & 0xFF00);
    return 0;
}

static int read_u32(FILE *fp, unsigned int *u32) {
    unsigned int tmp;
    if (fread(&tmp, 1, 4, fp) != 4) {
        printf("Failed to read_u32!\n");
        return -1;
    }
    *u32 = ((tmp >> 24) & 0xFF) | ((tmp >> 8) & 0xFF00) | \
           ((tmp << 8) & 0xFF0000) | ((tmp << 24) & 0xFF000000);
    return 0;
}

static int read_ts(FILE *fp, unsigned int *ts) {
    unsigned int tmp;
    if (fread(&tmp, 1, 4, fp) != 4) {
        printf("Failed to read_ts!\n");
        return -1;
    }
    *ts = ((tmp >> 16) & 0xFF) | ((tmp << 16) & 0xFF0000) | (tmp & 0xFF00) | (tmp & 0xFF000000);
    return 0;
}

// 读取 flv 文件的数据，并写入 RTMPPacket 对象。
static int read_data(FILE *flv, RTMPPacket *packet) {
    int ret = -1;
    size_t data_size = 0;

    unsigned int tt;
    unsigned int tag_data_size;
    unsigned int ts;
    unsigned int stream_id;
    unsigned int tag_pre_size;

    if (read_u8(flv, &tt)) {
        goto __ERROR;
    }

    if (read_u24(flv, &tag_data_size)) {
        goto __ERROR;
    }

    if (read_ts(flv, &ts)) {
        goto __ERROR;
    }

    if (read_u24(flv, &stream_id)) {
        goto __ERROR;
    }

    printf("tag header, ts: %u, tt: %d, tag_data_size: %d\n", ts, tt, tag_data_size);

    data_size = fread(packet->m_body, 1, tag_data_size, flv);
    if (tag_data_size != data_size) {
        printf("Failed to read tag body from flv (data_size=%zu, tds=%d)!\n",
                data_size,
                tag_data_size);
        goto __ERROR;
    }

    packet->m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet->m_nTimeStamp = ts;
    packet->m_packetType = (uint8_t) tt;
    packet->m_nBodySize = tag_data_size;

    read_u32(flv, &tag_pre_size);

    ret = 0;

    __ERROR:

    return ret;
}

// 向 RTMP 服务器推流
static void send_data(FILE *flv, RTMP *rtmp) {
    // 1、创建 RTMPPacket 对象
    RTMPPacket *packet = NULL;
    packet = alloc_packet();
    packet->m_nInfoField2 = rtmp->m_stream_id;

    unsigned int pre_ts = 0;

    while (1) {
        // 2、从 flv 文件中读取数据
        if (read_data(flv, packet)) {
            printf("Error, failed to read data from flv!\n");
            break;
        }

        // 3、判断 RTMP 连接是否正常
        if (!RTMP_IsConnected(rtmp)) {
            printf("Error, failed to connect to RTMP server!\n");
            break;
        }

        unsigned int diff = packet->m_nTimeStamp - pre_ts;
        usleep(diff * 1000);

        // 4、发送数据到服务器
        RTMP_SendPacket(rtmp, packet, 0);

        pre_ts = packet->m_nTimeStamp;
    }
}

// 推流
void publish_stream() {
    const char *filename = "/Users/hyh/Downloads/input.flv";
    char *url = "rtmp://localhost/live/room";

    // 1、打开 flv 文件
    FILE *flv = open_flv(filename);

    // 2、连接 RTMP 服务器
    RTMP *rtmp = connect_rtmp_server(url);

    // 3、推流
    send_data(flv, rtmp);

    // 4、释放资源
    if (rtmp) {
        RTMP_Close(rtmp);
        RTMP_Free(rtmp);
    }
}
