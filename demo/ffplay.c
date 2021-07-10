/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * simple media player based on the FFmpeg libraries
 */

#include "config.h"
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/fifo.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavutil/bprint.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#if CONFIG_AVFILTER
# include "libavfilter/avfilter.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
#endif

#include <SDL.h>
#include <SDL_thread.h>

#include "cmdutils.h"

#include <assert.h>
// 程序名称
const char program_name[] = "ffplay";
// 程序创建的年份
const int program_birth_year = 2003;

// AVFifoBuffer的大小限制, 不超过15MB, 如果超过15MB, 则停止读取
#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
// AVFifoBuffer的帧数量限制, V/A/T均不超过25帧的缓冲
#define MIN_FRAMES 25

#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
// SDL音频输出的最小缓冲区大小, 以符号为单位
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
// 计算实际缓冲区大小时要注意不要导致音频回调过于频繁
// 设置该值会计算出每次SDL回调获取的采样点数量, 这样设置后, SDL回调的次数就不会很快
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
// SDL 音量控制的步长(dB)
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
// 如果低于最小AV同步阈值，则不会进行AV同步校正
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
// 如果高于最大AV同步阈值，则进行AV同步校正
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
// 如果帧持续时间长于此时间，则将不会复制该帧以补偿AV同步
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
// 如果误差太大，则不会进行AV校正
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
// 最大音频速度变化以获得正确的同步
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
// 基于缓冲区充满度的实时源的外部时钟速度调整常数
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
// 音频非主时钟时, 我们使用大约 AUDIO_DIFF_AVG_NB A-V 差异来计算平均值
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

#define USE_ONEPASS_SUBTITLE_RENDER 1

static unsigned sws_flags = SWS_BICUBIC;

// 在AVPacket后增加了一个序列标识
typedef struct MyAVPacketList {
    AVPacket *pkt;
    int serial;
} MyAVPacketList;

// 包队列
typedef struct PacketQueue {
    // FIFO缓冲
    AVFifoBuffer *pkt_list;
    // 包数量
    int nb_packets;
    // 包队列大小
    int size;
    // 包队列总时长
    int64_t duration;
    // 终止请求的标志
    int abort_request;
    int serial;
    // SDL锁
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

typedef struct AudioParams {
    // 采样率
    int freq;
    // 通道数
    int channels;
    // 通道布局, 单声道/立体声等
    int64_t channel_layout;
    // 数据格式, 例如 AV_SAMPLE_FMT_S16 等
    enum AVSampleFormat fmt;
    // 采样符号的字节数, 根据 fmt, channels得出, 计算函数: av_samples_get_buffer_size()
    // 对于采样符号子结数, 只要取一个符号大小就可以了
    int frame_size;
    // 每秒采样数量, 根据 fmt, channels, freq 得出, 计算函数: av_samples_get_buffer_size()
    // 对于每秒采样的字节数量, 获取 freq 个符号的字节数即可
    int bytes_per_sec;
} AudioParams;

// 时钟源
typedef struct Clock {
    /* clock base */
    // 时钟基准
    double pts;           
    /* clock base minus time at which we updated the clock */
    // 时钟基准减去我们更新时钟的时间
    double pts_drift;     
    double last_updated;
    // 速度
    double speed;
    /* clock is based on a packet with this serial */
    // 时钟基于此串行的数据包
    int serial;           
    // 暂停标识
    int paused;
    /* pointer to the current packet queue serial, used for obsolete clock detection */
    // 指向当前数据包队列序列的指针，用于过时的时钟检测
    int *queue_serial;    
} Clock;

/* Common struct for handling all types of decoded data and allocated render buffers. */
// 处理所有类型的解码数据和分配的渲染缓冲区的通用结构
typedef struct Frame {
    // 原始音频或视频数据
    AVFrame *frame;
    // 字幕
    AVSubtitle sub;
    // 序列
    int serial;
    // 帧的展示时间戳记
    double pts;           /* presentation timestamp for the frame */
    // 该帧估计的持续时间
    double duration;      /* estimated duration of the frame */
    // 帧在输入文件中的字节位置
    int64_t pos;          /* byte position of the frame in the input file */
    // 宽度(视频帧)
    int width;
    // 高度(视频帧)
    int height;
    // 帧的数据格式
    int format;
    // SAR(Sample Aspect Ratio, 视频的采样宽高比)
    AVRational sar;
    // 
    int uploaded;
    // 
    int flip_v;
} Frame;

typedef struct FrameQueue {
    // 帧队列, 最大16
    Frame queue[FRAME_QUEUE_SIZE];
    // 读索引
    int rindex;
    // 写索引
    int windex;
    // 当前可用帧数量
    int size;
    // 最大帧数量
    int max_size;
    // 
    int keep_last;
    // rindex的帧是否被读取过
    int rindex_shown;
    // SDL锁
    SDL_mutex *mutex;
    // 条件互斥
    SDL_cond *cond;
    // 包队列
    PacketQueue *pktq;
} FrameQueue;

// 同步源
enum {
    /* default choice */
    // 音频为主同步源
    AV_SYNC_AUDIO_MASTER,
    // 视频为主同步源 
    AV_SYNC_VIDEO_MASTER,
    /* synchronize to an external clock */
    // 外部时钟为主同步源
    AV_SYNC_EXTERNAL_CLOCK, 
};

// 解码器结构
typedef struct Decoder {
    // 当前解码的包
    AVPacket *pkt;
    // 包队列
    PacketQueue *queue;
    // 解码器上下文
    AVCodecContext *avctx;
    // 包序列
    int pkt_serial;
    // 结束标志
    int finished;
    int packet_pending;
    SDL_cond *empty_queue_cond;
    // 起始包PTS
    int64_t start_pts;
    // 起始包的时基
    AVRational start_pts_tb;
    // 下一个包的PTS
    int64_t next_pts;
    // 下一个包的时基
    AVRational next_pts_tb;
    SDL_Thread *decoder_tid;
} Decoder;

// 状态存储
typedef struct VideoState {
    SDL_Thread *read_tid;
    AVInputFormat *iformat;
    int abort_request;
    int force_refresh;
    int paused;
    int last_paused;
    int queue_attachments_req;
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    int read_pause_return;
    AVFormatContext *ic;
    int realtime;

    Clock audclk;
    Clock vidclk;
    Clock extclk;

    FrameQueue pictq;
    FrameQueue subpq;
    FrameQueue sampq;

    Decoder auddec;
    Decoder viddec;
    Decoder subdec;

    int audio_stream;

    int av_sync_type;

    double audio_clock;
    int audio_clock_serial;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    PacketQueue audioq;
    int audio_hw_buf_size;
    uint8_t *audio_buf;
    uint8_t *audio_buf1;
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size;
    int audio_buf_index; /* in bytes */
    int audio_write_buf_size;
    int audio_volume;
    int muted;
    struct AudioParams audio_src;
#if CONFIG_AVFILTER
    struct AudioParams audio_filter_src;
#endif
    struct AudioParams audio_tgt;
    struct SwrContext *swr_ctx;
    int frame_drops_early;
    int frame_drops_late;

    enum ShowMode {
        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
    } show_mode;
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
    RDFTContext *rdft;
    int rdft_bits;
    FFTSample *rdft_data;
    int xpos;
    double last_vis_time;
    SDL_Texture *vis_texture;
    SDL_Texture *sub_texture;
    SDL_Texture *vid_texture;

    int subtitle_stream;
    AVStream *subtitle_st;
    PacketQueue subtitleq;

    // 视频当前播放帧的时间
    double frame_timer;
    double frame_last_returned_time;
    double frame_last_filter_delay;
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;

    // // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    // 一帧的最大持续时间-在此之上，我们将跳转视为时间戳不连续
    double max_frame_duration;      
    struct SwsContext *img_convert_ctx;
    struct SwsContext *sub_convert_ctx;
    int eof;

    char *filename;
    int width, height, xleft, ytop;
    int step;

#if CONFIG_AVFILTER
    int vfilter_idx;
    AVFilterContext *in_video_filter;   // the first filter in the video chain
    AVFilterContext *out_video_filter;  // the last filter in the video chain
    AVFilterContext *in_audio_filter;   // the first filter in the audio chain
    AVFilterContext *out_audio_filter;  // the last filter in the audio chain
    AVFilterGraph *agraph;              // audio filter graph
#endif

    int last_video_stream, last_audio_stream, last_subtitle_stream;

    SDL_cond *continue_read_thread;
} VideoState;

/* options specified by the user */
// 如果用户制定了 format, 则使用av_find_input_format()找出来对应的 AVInputFormat
// 如果没有制定 -f , 那么该指针为空, 因此在avformat_open_input()的 fmt 参数也是空
static AVInputFormat *file_iformat;

// 输入文件/URL/设备的名字
static const char *input_filename;

// 窗口名字, 如果 "title" 不存在, 那么窗口与 input_filename 相同
static const char *window_title;

// 默认宽度, 如果 screen_width 有设置, 则等于 screen_width
static int default_width  = 640;

// 默认高度, 如果 screen_height 有设置, 则等于 screen_height
static int default_height = 480;

// 屏幕宽度, 参数 -x
static int screen_width  = 0;

// 屏幕高度, 参数 -y
static int screen_height = 0;

// 窗口左边位置
static int screen_left = SDL_WINDOWPOS_CENTERED;

// 窗口右边位置
static int screen_top = SDL_WINDOWPOS_CENTERED;

// 禁止音频, 选项 -an
static int audio_disable;

// 禁止视频, 选项 -vn, 如果有音频, 则以默认分辨率, show_mode为rdft方式输出渲染
// 如果不显示任何渲染, 则应使用 -nodisp 参数
static int video_disable;

// 禁止字幕, 选项 -sn
static int subtitle_disable;

// 期望的流
// 视频: &wanted_stream_spec[AVMEDIA_TYPE_VIDEO]
// 音频: &wanted_stream_spec[AVMEDIA_TYPE_AUDIO]
// 字幕: &wanted_stream_spec[AVMEDIA_TYPE_SUBTITLE]
static const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = {0};

// seek的长度, 以字节为单位, 参数 -bytes
// !! 该参数看起来不起作用...
static int seek_by_bytes = -1;

// seek的间隔, 参数 -seek_interval
static float seek_interval = 10;

// 无显示, 和 -vn不同, 该参数完全不输出任何渲染那. 参数 -nodisp
static int display_disable;

// 无窗体, 参数 -noborder
static int borderless;

// 窗体始终在最上层
static int alwaysontop;

// 开始播放时的音量, 参数: -volume
static int startup_volume = 100;

// 是否显示状态, 参数 -stats
static int show_status = -1;

// 默认同步源, 参数 -sync, 选项:
// audio : 音频
// video : 视频
// ext : 外部
// 例子: ffplay -sync ext ....
static int av_sync_type = AV_SYNC_AUDIO_MASTER;

// 开始时间, 单位: 秒, 参数 -ss
static int64_t start_time = AV_NOPTS_VALUE;

// 播放的时长, 单位: 秒, 参数 -t
static int64_t duration = AV_NOPTS_VALUE;

// 是否使用不符合规范的优化? 参数 -fast
static int fast = 0;

// 生成PTS? 
static int genpts = 0;
static int lowres = 0;
// 让解码器重新排序 pts(自动)
static int decoder_reorder_pts = -1;

// 播放完成后自动退出, 参数 -autoexit
static int autoexit;

// 播放完成后按任意按键退出(不会等待播放完成), 参数 -exitonkeydown
static int exit_on_keydown;
static int exit_on_mousedown;

// 以当前参数设置下播放的循环次数, 参数 -loop
// 例如: -loop 10
// 注意: 如果参数是 -loop -1 , 则表示无限循环
static int loop = 1;

// 如果CPU性能不足是否丢掉帧? 参数 -framedrop
static int framedrop = -1;

// 是否不限制输入buffer的大小? 
static int infinite_buffer = -1;

// 展示方式, 参数 -showmode
// video : 视频
// waves : 波形
// rdft  : 光谱
static enum ShowMode show_mode = SHOW_MODE_NONE;

// 音频解码器名称, 参数 -acodec
static const char *audio_codec_name;

// 字幕解码器名称, 参数 -scodec
static const char *subtitle_codec_name;

// 视频解码器名称, 参考 -vcodec
static const char *video_codec_name;

// DFT速度
double rdftspeed = 0.02;
static int64_t cursor_last_shown;
static int cursor_hidden = 0;
#if CONFIG_AVFILTER
static const char **vfilters_list = NULL;
static int nb_vfilters = 0;
static char *afilters = NULL;
#endif
static int autorotate = 1;
static int find_stream_info = 1;

// 效果器线程数
static int filter_nbthreads = 0;

/* current context */
static int is_full_screen;
static int64_t audio_callback_time;

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_RendererInfo renderer_info = {0};
static SDL_AudioDeviceID audio_dev;

static const struct TextureFormatEntry {
    enum AVPixelFormat format;
    int texture_fmt;
} sdl_texture_format_map[] = {
    { AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
    { AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },
    { AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 },
    { AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 },
    { AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
    { AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
    { AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
    { AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
    { AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },
    { AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 },
    { AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
    { AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
    { AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
    { AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
    { AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
    { AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
    { AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
    { AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
    { AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
    { AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN },
};

#if CONFIG_AVFILTER
static int opt_add_vfilter(void *optctx, const char *opt, const char *arg)
{
    GROW_ARRAY(vfilters_list, nb_vfilters);
    vfilters_list[nb_vfilters - 1] = arg;
    return 0;
}
#endif

static inline
int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                   enum AVSampleFormat fmt2, int64_t channel_count2)
{
    /* If channel count == 1, planar and non-planar formats are the same */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

static inline
int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
    if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
        return channel_layout;
    else
        return 0;
}

static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList pkt1;

    if (q->abort_request)
       return -1;

    if (av_fifo_space(q->pkt_list) < sizeof(pkt1)) {
        if (av_fifo_grow(q->pkt_list, sizeof(pkt1)) < 0)
            return -1;
    }

    pkt1.pkt = pkt;
    pkt1.serial = q->serial;

    av_fifo_generic_write(q->pkt_list, &pkt1, sizeof(pkt1), NULL);
    q->nb_packets++;
    q->size += pkt1.pkt->size + sizeof(pkt1);
    q->duration += pkt1.pkt->duration;
    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal(q->cond);
    return 0;
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacket *pkt1;
    int ret;

    pkt1 = av_packet_alloc();
    if (!pkt1) {
        av_packet_unref(pkt);
        return -1;
    }
    av_packet_move_ref(pkt1, pkt);

    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt1);
    SDL_UnlockMutex(q->mutex);

    if (ret < 0)
        av_packet_free(&pkt1);

    return ret;
}

static int packet_queue_put_nullpacket(PacketQueue *q, AVPacket *pkt, int stream_index)
{
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}

/* packet queue handling */
static int packet_queue_init(PacketQueue *q)
{
    // 清空包队列结构体
    memset(q, 0, sizeof(PacketQueue));
    // 分配FIFO缓冲区描述
    q->pkt_list = av_fifo_alloc(sizeof(MyAVPacketList));
    if (!q->pkt_list)
        return AVERROR(ENOMEM);
    // 初始化临界锁
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    // 初始化临界条件
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    // 终止请求设置为1
    q->abort_request = 1;
    return 0;
}

static void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList pkt1;

    SDL_LockMutex(q->mutex);
    while (av_fifo_size(q->pkt_list) >= sizeof(pkt1)) {
        av_fifo_generic_read(q->pkt_list, &pkt1, sizeof(pkt1), NULL);
        av_packet_free(&pkt1.pkt);
    }
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    q->serial++;
    SDL_UnlockMutex(q->mutex);
}

static void packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q);
    av_fifo_freep(&q->pkt_list);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

static void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);

    q->abort_request = 1;

    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
}

static void packet_queue_start(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    q->serial++;
    SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{
    MyAVPacketList pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        if (av_fifo_size(q->pkt_list) >= sizeof(pkt1)) {
            av_fifo_generic_read(q->pkt_list, &pkt1, sizeof(pkt1), NULL);
            q->nb_packets--;
            q->size -= pkt1.pkt->size + sizeof(pkt1);
            q->duration -= pkt1.pkt->duration;
            av_packet_move_ref(pkt, pkt1.pkt);
            if (serial)
                *serial = pkt1.serial;
            av_packet_free(&pkt1.pkt);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

static int decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond) {
    // 初始化Decoder结构体
    memset(d, 0, sizeof(Decoder));
    // 为解码器分配AVPacket, 注意没有AVPacket分配内存, 也不会分配内存
    // AVPacket的数据将来自 av_read_frame()
    d->pkt = av_packet_alloc();
    if (!d->pkt)
        return AVERROR(ENOMEM);
    // 保存音频/视频/字幕输入流解码器上下文
    d->avctx = avctx;
    // 保存AVPacket队列
    d->queue = queue;
    // 保存AVPacket队列为空时的SDL临界条件
    d->empty_queue_cond = empty_queue_cond;
    // 设置开始展示时间为0
    d->start_pts = AV_NOPTS_VALUE;
    // 初始化包序列为 -1
    d->pkt_serial = -1;
    return 0;
}

static int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN);

    // 死循环
    for (;;) {
        // 检查队列中的包序列是否和当前解码器的相同
        if (d->queue->serial == d->pkt_serial) {
            // 循环接收解码后的数据
            do {
                // 如果收到终止请求,则立刻返回
                if (d->queue->abort_request)
                    return -1;

                // 音视频区分处理
                switch (d->avctx->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                        // 获取解码后的画面: AVFrame
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            // 如果成功获取到, 并且 decoder_reorder_pts 是 -1 , 则更新 frame->pts
                            // best_effort_timestamp: 在流时基中使用各种启发式方法估计的帧时间戳
                            if (decoder_reorder_pts == -1) {
                                frame->pts = frame->best_effort_timestamp;
                            // 否则使用AVPacket中的PTS
                            } else if (!decoder_reorder_pts) {
                                frame->pts = frame->pkt_dts;
                            }
                        }
                        break;
                    case AVMEDIA_TYPE_AUDIO:
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            AVRational tb = (AVRational){1, frame->sample_rate};
                            if (frame->pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
                            else if (d->next_pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
                            if (frame->pts != AV_NOPTS_VALUE) {
                                d->next_pts = frame->pts + frame->nb_samples;
                                d->next_pts_tb = tb;
                            }
                        }
                        break;
                }
                if (ret == AVERROR_EOF) {
                    d->finished = d->pkt_serial;
                    avcodec_flush_buffers(d->avctx);
                    return 0;
                }
                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (d->queue->nb_packets == 0)
                SDL_CondSignal(d->empty_queue_cond);
            if (d->packet_pending) {
                d->packet_pending = 0;
            } else {
                int old_serial = d->pkt_serial;
                if (packet_queue_get(d->queue, d->pkt, 1, &d->pkt_serial) < 0)
                    return -1;
                if (old_serial != d->pkt_serial) {
                    avcodec_flush_buffers(d->avctx);
                    d->finished = 0;
                    d->next_pts = d->start_pts;
                    d->next_pts_tb = d->start_pts_tb;
                }
            }
            if (d->queue->serial == d->pkt_serial)
                break;
            av_packet_unref(d->pkt);
        } while (1);

        if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            int got_frame = 0;
            ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, d->pkt);
            if (ret < 0) {
                ret = AVERROR(EAGAIN);
            } else {
                if (got_frame && !d->pkt->data) {
                    d->packet_pending = 1;
                }
                ret = got_frame ? 0 : (d->pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }
            av_packet_unref(d->pkt);
        } else {
            if (avcodec_send_packet(d->avctx, d->pkt) == AVERROR(EAGAIN)) {
                av_log(d->avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                d->packet_pending = 1;
            } else {
                av_packet_unref(d->pkt);
            }
        }
    }
}

static void decoder_destroy(Decoder *d) {
    av_packet_free(&d->pkt);
    avcodec_free_context(&d->avctx);
}

static void frame_queue_unref_item(Frame *vp)
{
    av_frame_unref(vp->frame);
    avsubtitle_free(&vp->sub);
}

static int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last)
{
    int i;
    // 清空FrameQueue结构体
    memset(f, 0, sizeof(FrameQueue));
    // 初始化帧队列的临界锁
    if (!(f->mutex = SDL_CreateMutex())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    // 初始化帧队列的临界条件
    if (!(f->cond = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    // 为帧队列设置包队列
    f->pktq = pktq;
    // 设置队列的最大大小
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    // 设置是否保持最后一个??
    f->keep_last = !!keep_last;
    // 分配帧描述结构体空间, 注意: 这些AVFrame此时还没有分配空间
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

static void frame_queue_destory(FrameQueue *f)
{
    int i;
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_frame_free(&vp->frame);
    }
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}

static void frame_queue_signal(FrameQueue *f)
{
    SDL_LockMutex(f->mutex);
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

static Frame *frame_queue_peek(FrameQueue *f)
{
    // 提取一帧未显示的, 如果rindex显示过, 那么 f->rindex_shown 为 1, 跳过
    // 如果跳过rindex刚好到末尾, 则取0号帧
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static Frame *frame_queue_peek_next(FrameQueue *f)
{
    // 提取下一帧未显示的, 如果rindex显示过, 那么 f->rindex_shown 为 1, 跳过一个未显示的帧
    // 如果跳过rindex刚好到末尾, 则取0号帧
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

// 获取最近一帧, 无论是否显示过
static Frame *frame_queue_peek_last(FrameQueue *f)
{
    return &f->queue[f->rindex];
}

// 从视频帧队列中获取一个可写的描述
static Frame *frame_queue_peek_writable(FrameQueue *f)
{
    /* wait until we have space to put a new frame */
    // 持有锁并检查渲染队列的状态, 如果队列中帧的数量超过了队列的最大容量
    // 则认为队列已满, 此时如果不是终止状态, 则持续等待临界条件, 无超时
    SDL_LockMutex(f->mutex);
    while (f->size >= f->max_size &&
           !f->pktq->abort_request) {
        // 此处的等待无超时
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    // 如果队列不满, 达到了可写状态, 但终止状态有效则放弃当前帧到队列的保存
    if (f->pktq->abort_request)
        return NULL;

    // 然会一个可用的帧描述
    return &f->queue[f->windex];
}

static Frame *frame_queue_peek_readable(FrameQueue *f)
{
    /* wait until we have a readable a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size - f->rindex_shown <= 0 &&
           !f->pktq->abort_request) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

// 
static void frame_queue_push(FrameQueue *f)
{
    if (++f->windex == f->max_size)
        f->windex = 0;
    SDL_LockMutex(f->mutex);
    f->size++;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

// 跳过一帧
static void frame_queue_next(FrameQueue *f)
{
    // 如果帧队列的模式是始终最后一帧(视频)
    // 如果rindex没有被读取过, 则设置为rindex已经读取, 然后返回
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1;
        return;
    }
    // 如果rindex已经读取过, 则弹出当前帧
    frame_queue_unref_item(&f->queue[f->rindex]);
    // 增加rindex索引到下一帧, 如果下一帧刚好最后一帧, 则直接从头(0)开始计
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    SDL_LockMutex(f->mutex);
    // 可显示的画面数量减1
    f->size--;
    // 向等待的线程发送信号
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/* return the number of undisplayed frames in the queue */
// 返回队列中未显示的帧数
static int frame_queue_nb_remaining(FrameQueue *f)
{
    return f->size - f->rindex_shown;
}

/* return last shown position */
static int64_t frame_queue_last_pos(FrameQueue *f)
{
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1;
}

static void decoder_abort(Decoder *d, FrameQueue *fq)
{
    packet_queue_abort(d->queue);
    frame_queue_signal(fq);
    SDL_WaitThread(d->decoder_tid, NULL);
    d->decoder_tid = NULL;
    packet_queue_flush(d->queue);
}

static inline void fill_rectangle(int x, int y, int w, int h)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    if (w && h)
        SDL_RenderFillRect(renderer, &rect);
}

static int realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture)
{
    Uint32 format;
    int access, w, h;
    if (!*texture || SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format) {
        void *pixels;
        int pitch;
        if (*texture)
            SDL_DestroyTexture(*texture);
        if (!(*texture = SDL_CreateTexture(renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
            return -1;
        if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
            return -1;
        if (init_texture) {
            if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
                return -1;
            memset(pixels, 0, pitch * new_height);
            SDL_UnlockTexture(*texture);
        }
        av_log(NULL, AV_LOG_VERBOSE, "Created %dx%d texture with %s.\n", new_width, new_height, SDL_GetPixelFormatName(new_format));
    }
    return 0;
}

static void calculate_display_rect(SDL_Rect *rect,
                                   int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                   int pic_width, int pic_height, AVRational pic_sar)
{
    AVRational aspect_ratio = pic_sar;
    int64_t width, height, x, y;

    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0)
        aspect_ratio = av_make_q(1, 1);

    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(pic_width, pic_height));

    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
    if (width > scr_width) {
        width = scr_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
    }
    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;
    rect->x = scr_xleft + x;
    rect->y = scr_ytop  + y;
    rect->w = FFMAX((int)width,  1);
    rect->h = FFMAX((int)height, 1);
}

static void get_sdl_pix_fmt_and_blendmode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode)
{
    int i;
    *sdl_blendmode = SDL_BLENDMODE_NONE;
    *sdl_pix_fmt = SDL_PIXELFORMAT_UNKNOWN;
    if (format == AV_PIX_FMT_RGB32   ||
        format == AV_PIX_FMT_RGB32_1 ||
        format == AV_PIX_FMT_BGR32   ||
        format == AV_PIX_FMT_BGR32_1)
        *sdl_blendmode = SDL_BLENDMODE_BLEND;
    for (i = 0; i < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; i++) {
        if (format == sdl_texture_format_map[i].format) {
            *sdl_pix_fmt = sdl_texture_format_map[i].texture_fmt;
            return;
        }
    }
}

static int upload_texture(SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx) {
    int ret = 0;
    Uint32 sdl_pix_fmt;
    SDL_BlendMode sdl_blendmode;
    get_sdl_pix_fmt_and_blendmode(frame->format, &sdl_pix_fmt, &sdl_blendmode);
    if (realloc_texture(tex, sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_ARGB8888 : sdl_pix_fmt, frame->width, frame->height, sdl_blendmode, 0) < 0)
        return -1;
    switch (sdl_pix_fmt) {
        case SDL_PIXELFORMAT_UNKNOWN:
            /* This should only happen if we are not using avfilter... */
            *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
                frame->width, frame->height, frame->format, frame->width, frame->height,
                AV_PIX_FMT_BGRA, sws_flags, NULL, NULL, NULL);
            if (*img_convert_ctx != NULL) {
                uint8_t *pixels[4];
                int pitch[4];
                if (!SDL_LockTexture(*tex, NULL, (void **)pixels, pitch)) {
                    sws_scale(*img_convert_ctx, (const uint8_t * const *)frame->data, frame->linesize,
                              0, frame->height, pixels, pitch);
                    SDL_UnlockTexture(*tex);
                }
            } else {
                av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                ret = -1;
            }
            break;
        case SDL_PIXELFORMAT_IYUV:
            if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0) {
                ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0], frame->linesize[0],
                                                       frame->data[1], frame->linesize[1],
                                                       frame->data[2], frame->linesize[2]);
            } else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0) {
                ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height                    - 1), -frame->linesize[0],
                                                       frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
                                                       frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2]);
            } else {
                av_log(NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
                return -1;
            }
            break;
        default:
            if (frame->linesize[0] < 0) {
                ret = SDL_UpdateTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
            } else {
                ret = SDL_UpdateTexture(*tex, NULL, frame->data[0], frame->linesize[0]);
            }
            break;
    }
    return ret;
}

static void set_sdl_yuv_conversion_mode(AVFrame *frame)
{
#if SDL_VERSION_ATLEAST(2,0,8)
    SDL_YUV_CONVERSION_MODE mode = SDL_YUV_CONVERSION_AUTOMATIC;
    if (frame && (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUYV422 || frame->format == AV_PIX_FMT_UYVY422)) {
        if (frame->color_range == AVCOL_RANGE_JPEG)
            mode = SDL_YUV_CONVERSION_JPEG;
        else if (frame->colorspace == AVCOL_SPC_BT709)
            mode = SDL_YUV_CONVERSION_BT709;
        else if (frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M || frame->colorspace == AVCOL_SPC_SMPTE240M)
            mode = SDL_YUV_CONVERSION_BT601;
    }
    SDL_SetYUVConversionMode(mode);
#endif
}

static void video_image_display(VideoState *is)
{
    Frame *vp;
    Frame *sp = NULL;
    SDL_Rect rect;

    vp = frame_queue_peek_last(&is->pictq);
    if (is->subtitle_st) {
        if (frame_queue_nb_remaining(&is->subpq) > 0) {
            sp = frame_queue_peek(&is->subpq);

            if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000)) {
                if (!sp->uploaded) {
                    uint8_t* pixels[4];
                    int pitch[4];
                    int i;
                    if (!sp->width || !sp->height) {
                        sp->width = vp->width;
                        sp->height = vp->height;
                    }
                    if (realloc_texture(&is->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
                        return;

                    for (i = 0; i < sp->sub.num_rects; i++) {
                        AVSubtitleRect *sub_rect = sp->sub.rects[i];

                        sub_rect->x = av_clip(sub_rect->x, 0, sp->width );
                        sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
                        sub_rect->w = av_clip(sub_rect->w, 0, sp->width  - sub_rect->x);
                        sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

                        is->sub_convert_ctx = sws_getCachedContext(is->sub_convert_ctx,
                            sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
                            sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
                            0, NULL, NULL, NULL);
                        if (!is->sub_convert_ctx) {
                            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                            return;
                        }
                        if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)pixels, pitch)) {
                            sws_scale(is->sub_convert_ctx, (const uint8_t * const *)sub_rect->data, sub_rect->linesize,
                                      0, sub_rect->h, pixels, pitch);
                            SDL_UnlockTexture(is->sub_texture);
                        }
                    }
                    sp->uploaded = 1;
                }
            } else
                sp = NULL;
        }
    }

    calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);

    if (!vp->uploaded) {
        if (upload_texture(&is->vid_texture, vp->frame, &is->img_convert_ctx) < 0)
            return;
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
    }

    set_sdl_yuv_conversion_mode(vp->frame);
    SDL_RenderCopyEx(renderer, is->vid_texture, NULL, &rect, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : 0);
    set_sdl_yuv_conversion_mode(NULL);
    if (sp) {
#if USE_ONEPASS_SUBTITLE_RENDER
        SDL_RenderCopy(renderer, is->sub_texture, NULL, &rect);
#else
        int i;
        double xratio = (double)rect.w / (double)sp->width;
        double yratio = (double)rect.h / (double)sp->height;
        for (i = 0; i < sp->sub.num_rects; i++) {
            SDL_Rect *sub_rect = (SDL_Rect*)sp->sub.rects[i];
            SDL_Rect target = {.x = rect.x + sub_rect->x * xratio,
                               .y = rect.y + sub_rect->y * yratio,
                               .w = sub_rect->w * xratio,
                               .h = sub_rect->h * yratio};
            SDL_RenderCopy(renderer, is->sub_texture, sub_rect, &target);
        }
#endif
    }
}

static inline int compute_mod(int a, int b)
{
    return a < 0 ? a%b + b : a%b;
}

static void video_audio_display(VideoState *s)
{
    int i, i_start, x, y1, y, ys, delay, n, nb_display_channels;
    int ch, channels, h, h2;
    int64_t time_diff;
    int rdft_bits, nb_freq;

    for (rdft_bits = 1; (1 << rdft_bits) < 2 * s->height; rdft_bits++)
        ;
    nb_freq = 1 << (rdft_bits - 1);

    /* compute display index : center on currently output samples */
    channels = s->audio_tgt.channels;
    nb_display_channels = channels;
    if (!s->paused) {
        int data_used= s->show_mode == SHOW_MODE_WAVES ? s->width : (2*nb_freq);
        n = 2 * channels;
        delay = s->audio_write_buf_size;
        delay /= n;

        /* to be more precise, we take into account the time spent since
           the last buffer computation */
        if (audio_callback_time) {
            time_diff = av_gettime_relative() - audio_callback_time;
            delay -= (time_diff * s->audio_tgt.freq) / 1000000;
        }

        delay += 2 * data_used;
        if (delay < data_used)
            delay = data_used;

        i_start= x = compute_mod(s->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
        if (s->show_mode == SHOW_MODE_WAVES) {
            h = INT_MIN;
            for (i = 0; i < 1000; i += channels) {
                int idx = (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
                int a = s->sample_array[idx];
                int b = s->sample_array[(idx + 4 * channels) % SAMPLE_ARRAY_SIZE];
                int c = s->sample_array[(idx + 5 * channels) % SAMPLE_ARRAY_SIZE];
                int d = s->sample_array[(idx + 9 * channels) % SAMPLE_ARRAY_SIZE];
                int score = a - d;
                if (h < score && (b ^ c) < 0) {
                    h = score;
                    i_start = idx;
                }
            }
        }

        s->last_i_start = i_start;
    } else {
        i_start = s->last_i_start;
    }

    if (s->show_mode == SHOW_MODE_WAVES) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        /* total height for one channel */
        h = s->height / nb_display_channels;
        /* graph height / 2 */
        h2 = (h * 9) / 20;
        for (ch = 0; ch < nb_display_channels; ch++) {
            i = i_start + ch;
            y1 = s->ytop + ch * h + (h / 2); /* position of center line */
            for (x = 0; x < s->width; x++) {
                y = (s->sample_array[i] * h2) >> 15;
                if (y < 0) {
                    y = -y;
                    ys = y1 - y;
                } else {
                    ys = y1;
                }
                fill_rectangle(s->xleft + x, ys, 1, y);
                i += channels;
                if (i >= SAMPLE_ARRAY_SIZE)
                    i -= SAMPLE_ARRAY_SIZE;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);

        for (ch = 1; ch < nb_display_channels; ch++) {
            y = s->ytop + ch * h;
            fill_rectangle(s->xleft, y, s->width, 1);
        }
    } else {
        if (realloc_texture(&s->vis_texture, SDL_PIXELFORMAT_ARGB8888, s->width, s->height, SDL_BLENDMODE_NONE, 1) < 0)
            return;

        if (s->xpos >= s->width)
            s->xpos = 0;
        nb_display_channels= FFMIN(nb_display_channels, 2);
        if (rdft_bits != s->rdft_bits) {
            av_rdft_end(s->rdft);
            av_free(s->rdft_data);
            s->rdft = av_rdft_init(rdft_bits, DFT_R2C);
            s->rdft_bits = rdft_bits;
            s->rdft_data = av_malloc_array(nb_freq, 4 *sizeof(*s->rdft_data));
        }
        if (!s->rdft || !s->rdft_data){
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate buffers for RDFT, switching to waves display\n");
            s->show_mode = SHOW_MODE_WAVES;
        } else {
            FFTSample *data[2];
            SDL_Rect rect = {.x = s->xpos, .y = 0, .w = 1, .h = s->height};
            uint32_t *pixels;
            int pitch;
            for (ch = 0; ch < nb_display_channels; ch++) {
                data[ch] = s->rdft_data + 2 * nb_freq * ch;
                i = i_start + ch;
                for (x = 0; x < 2 * nb_freq; x++) {
                    double w = (x-nb_freq) * (1.0 / nb_freq);
                    data[ch][x] = s->sample_array[i] * (1.0 - w * w);
                    i += channels;
                    if (i >= SAMPLE_ARRAY_SIZE)
                        i -= SAMPLE_ARRAY_SIZE;
                }
                av_rdft_calc(s->rdft, data[ch]);
            }
            /* Least efficient way to do this, we should of course
             * directly access it but it is more than fast enough. */
            if (!SDL_LockTexture(s->vis_texture, &rect, (void **)&pixels, &pitch)) {
                pitch >>= 2;
                pixels += pitch * s->height;
                for (y = 0; y < s->height; y++) {
                    double w = 1 / sqrt(nb_freq);
                    int a = sqrt(w * sqrt(data[0][2 * y + 0] * data[0][2 * y + 0] + data[0][2 * y + 1] * data[0][2 * y + 1]));
                    int b = (nb_display_channels == 2 ) ? sqrt(w * hypot(data[1][2 * y + 0], data[1][2 * y + 1]))
                                                        : a;
                    a = FFMIN(a, 255);
                    b = FFMIN(b, 255);
                    pixels -= pitch;
                    *pixels = (a << 16) + (b << 8) + ((a+b) >> 1);
                }
                SDL_UnlockTexture(s->vis_texture);
            }
            SDL_RenderCopy(renderer, s->vis_texture, NULL, NULL);
        }
        if (!s->paused)
            s->xpos++;
    }
}

static void stream_component_close(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        decoder_abort(&is->auddec, &is->sampq);
        SDL_CloseAudioDevice(audio_dev);
        decoder_destroy(&is->auddec);
        swr_free(&is->swr_ctx);
        av_freep(&is->audio_buf1);
        is->audio_buf1_size = 0;
        is->audio_buf = NULL;

        if (is->rdft) {
            av_rdft_end(is->rdft);
            av_freep(&is->rdft_data);
            is->rdft = NULL;
            is->rdft_bits = 0;
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        decoder_abort(&is->viddec, &is->pictq);
        decoder_destroy(&is->viddec);
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        decoder_abort(&is->subdec, &is->subpq);
        decoder_destroy(&is->subdec);
        break;
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->audio_st = NULL;
        is->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_st = NULL;
        is->video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_st = NULL;
        is->subtitle_stream = -1;
        break;
    default:
        break;
    }
}

static void stream_close(VideoState *is)
{
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
    SDL_WaitThread(is->read_tid, NULL);

    /* close each stream */
    if (is->audio_stream >= 0)
        stream_component_close(is, is->audio_stream);
    if (is->video_stream >= 0)
        stream_component_close(is, is->video_stream);
    if (is->subtitle_stream >= 0)
        stream_component_close(is, is->subtitle_stream);

    avformat_close_input(&is->ic);

    packet_queue_destroy(&is->videoq);
    packet_queue_destroy(&is->audioq);
    packet_queue_destroy(&is->subtitleq);

    /* free all pictures */
    frame_queue_destory(&is->pictq);
    frame_queue_destory(&is->sampq);
    frame_queue_destory(&is->subpq);
    SDL_DestroyCond(is->continue_read_thread);
    sws_freeContext(is->img_convert_ctx);
    sws_freeContext(is->sub_convert_ctx);
    av_free(is->filename);
    if (is->vis_texture)
        SDL_DestroyTexture(is->vis_texture);
    if (is->vid_texture)
        SDL_DestroyTexture(is->vid_texture);
    if (is->sub_texture)
        SDL_DestroyTexture(is->sub_texture);
    av_free(is);
}

static void do_exit(VideoState *is)
{
    if (is) {
        stream_close(is);
    }
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    uninit_opts();
#if CONFIG_AVFILTER
    av_freep(&vfilters_list);
#endif
    avformat_network_deinit();
    if (show_status)
        printf("\n");
    SDL_Quit();
    av_log(NULL, AV_LOG_QUIET, "%s", "");
    exit(0);
}

static void sigterm_handler(int sig)
{
    exit(123);
}

static void set_default_window_size(int width, int height, AVRational sar)
{
    SDL_Rect rect;
    int max_width  = screen_width  ? screen_width  : INT_MAX;
    int max_height = screen_height ? screen_height : INT_MAX;
    if (max_width == INT_MAX && max_height == INT_MAX)
        max_height = height;
    calculate_display_rect(&rect, 0, 0, max_width, max_height, width, height, sar);
    default_width  = rect.w;
    default_height = rect.h;
}

static int video_open(VideoState *is)
{
    int w,h;

    // 修正窗口大小, 如果是VIDEO模式, 则大小用指定的
    // 如果是RDFT模式, 则窗口大小使用默认参数, 即 640 x 480
    w = screen_width ? screen_width : default_width;
    h = screen_height ? screen_height : default_height;

    // 如果未设置标题, 那么默认以输入文件为标题
    if (!window_title)
        window_title = input_filename;
    // 设置窗口标题
    SDL_SetWindowTitle(window, window_title);

    // 设置窗口尺寸和视频尺寸相同
    SDL_SetWindowSize(window, w, h);
    // 设置窗口在桌面的显示位置
    SDL_SetWindowPosition(window, screen_left, screen_top);
    // 如果是全屏显示, 则设置窗口属性为: SDL_WINDOW_FULLSCREEN_DESKTOP
    if (is_full_screen)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    // 显示窗口
    SDL_ShowWindow(window);

    // 将窗口状态保存到 VideoState
    is->width  = w;
    is->height = h;

    return 0;
}

/* display the current picture, if any */
static void video_display(VideoState *is)
{
    // 如果画面宽度为0, 则打开视频输出, 这可能是第一次显示画面
    if (!is->width)
        video_open(is);

    // 设置渲染背景色为黑色不透明
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    // 清楚渲染器视口的所有像素
    SDL_RenderClear(renderer);
    // 如果有音频流且显示模式不是VIDEO(而是RDFT)
    if (is->audio_st && is->show_mode != SHOW_MODE_VIDEO)
        // 则执行音频DFT实时渲染
        video_audio_display(is);
    // 如果有视频流, 且处于非RDFT的模式, 则执行视频渲染
    else if (is->video_st)
        // 显示视频画面
        video_image_display(is);
    // 调用SDL接口对渲染完成的画面进行展示
    SDL_RenderPresent(renderer);
}

// 获取当前时钟源的 PTS
static double get_clock(Clock *c)
{
    // 如果包队列序列和时钟序列不符, 则直接返回错误
    if (*c->queue_serial != c->serial)
        return NAN;
    // 如果是暂停状态, pts不会改变, 直接返回
    if (c->paused) {
        return c->pts;
    } else {
        // 如果是播放状态, 返回的是当前 pts与真实时间的差(pts_drift) + 当前时间, 也就是应该播放的PTS
        // 此时是1倍速的时间, 但此时要通过减去 (当前时间 - 最后一次更新时钟的时间) * 速度变快的比例, 也就是变速的部分
        // 也就是得出当前真正应该播放的PTS是哪一帧
        double time = av_gettime_relative() / 1000000.0;
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

static void set_clock_at(Clock *c, double pts, int serial, double time)
{
    // 当前时钟源的PTS
    c->pts = pts;
    // 最后一次更新时钟源的实际时间
    c->last_updated = time;
    // 计算本次更新, PTS和实际时间的差异(Drift)
    c->pts_drift = c->pts - time;
    // 更新序列编号
    c->serial = serial;
}

static void set_clock(Clock *c, double pts, int serial)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, serial, time);
}

static void set_clock_speed(Clock *c, double speed)
{
    set_clock(c, get_clock(c), c->serial);
    c->speed = speed;
}

static void init_clock(Clock *c, int *queue_serial)
{
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock(c, NAN, -1);
}

static void sync_clock_to_slave(Clock *c, Clock *slave)
{
    // 获取当前时钟源的 PTS
    double clock = get_clock(c);
    // 获取 slave 时钟源的 PTS
    double slave_clock = get_clock(slave);
    // 如果 slave 时钟源的 PTS 无效并且当前始终无效
    // 或者当前时钟减去 slave 时钟的差异超过 AV_NOSYNC_THRESHOLD , 则设置当前始终为 slave 的值
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(c, slave_clock, slave->serial);
}

static int get_master_sync_type(VideoState *is) {
    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    } else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

/* get the current master clock value */
static double get_master_clock(VideoState *is)
{
    double val;
    // 根据 VideoState 的配置获取对应的始终源
    switch (get_master_sync_type(is)) {
        case AV_SYNC_VIDEO_MASTER:
            val = get_clock(&is->vidclk);
            break;
        case AV_SYNC_AUDIO_MASTER:
            val = get_clock(&is->audclk);
            break;
        default:
            // 默认用外部时钟
            val = get_clock(&is->extclk);
            break;
    }
    return val;
}

// 检查外部时钟的速度
static void check_external_clock_speed(VideoState *is) {
    // 如果视频流存在, 并且队列中的包小于一定的数量
    // 或者如果音频流存在, 并且队列中的包小于一定的数量
   if (is->video_stream >= 0 && is->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES ||
       is->audio_stream >= 0 && is->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) {
        // 则降低外部时钟的速度, 这是因为AVPacket队列消耗得比较快, 所以降低速度是包队列保持相对稳定
       set_clock_speed(&is->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
    // 如果流不存在, 或者流对应的包队列中的包数量过多, 则加快外部始终的速度
   } else if ((is->video_stream < 0 || is->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
              (is->audio_stream < 0 || is->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
       set_clock_speed(&is->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
   } else {
       double speed = is->extclk.speed;
       if (speed != 1.0)
           set_clock_speed(&is->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
   }
}

/* seek in the stream */
static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes)
{
    // 如果没有处于seek操作当中
    if (!is->seek_req) {
        // 设置 seek 的目标位置
        is->seek_pos = pos;
        // 设置 seek 的相对范围
        is->seek_rel = rel;
        // 不使用按字节 seek 的设置
        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
        // 如果强制按字节 seek 再设置回标志
        if (seek_by_bytes)
            is->seek_flags |= AVSEEK_FLAG_BYTE;
        // 设置 seek 标志有效, 等待下一次读取循环执行 seek 操作
        is->seek_req = 1;
        SDL_CondSignal(is->continue_read_thread);
    }
}

/* pause or resume the video */
// 反转暂停/播放状态, 转为播放时, 则需要更新时钟
static void stream_toggle_pause(VideoState *is)
{
    // 如果是暂停状态
    if (is->paused) {
        // 获取当前始终的值(us), 计算秒数, 并且减去视频最后一次的更新时间, 将插值设置为 frame_timer
        is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
        // 如果暂停操作此前未发生错误, 则设置视频始终状态为运行
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
    }
    // 不管是否是暂停状态都应更新外部时钟
    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
    // 反转 播放/暂停 状态标志
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

// 单纯的在播放/暂停两种状态的切换, 那么 is->step 不需要设置
// 因为从暂停->播放的操作中, 画面是不变的, 这和 seek 操作有明显区别
static void toggle_pause(VideoState *is)
{
    stream_toggle_pause(is);
    // 注意和 seek 时执行的 step_to_next_frame() 的区别
    is->step = 0;
}

// 反转静音状态
static void toggle_mute(VideoState *is)
{
    // 仅更新标志位即可
    is->muted = !is->muted;
}

// 更新音量函数
static void update_volume(VideoState *is, int sign, double step)
{
    // 如果原有音量有值, 则获取其值, 除以 SDL_MIX_MAXVOLUME 的到百分比
    // 然后取对数乘以20的到dB值, 这是一个负数
    // 然后使用的负数除以log(10)得到dB的百分比
    // 如果原有音量没有值, 则该值为-1000.00
    double volume_level = is->audio_volume ? (20 * log(is->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    // 然后使用这个数值加上符号和和调节步长, 然后除以20作为指数转换回百分比, 然后乘以 SDL_MIX_MAXVOLUME
    // 如上操作得到的是SDL的音量数值
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    // 如果新音量的数值和原有的音量值相同, 则设置为原有音量, 否则更新新 is->audio_volume 的值
    // 在更新新的音量值之要 clip 到 0 ~ SDL_MIX_MAXVOLUME 的范围
    is->audio_volume = av_clip(is->audio_volume == new_volume ? (is->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

static void step_to_next_frame(VideoState *is)
{
    /* if the stream is paused unpause it, then step */
    // 如果流是暂停状态, 则回复播放, 出一帧
    if (is->paused)
        // 先从 pause 状态回复到 play 状态
        stream_toggle_pause(is);
    // 标志播放一帧, 此时关注:
    //     video_refresh(...) {
    //         if (is->video_st) {
    //             if (frame_queue_nb_remaining(&is->pictq) > 1) {
    //                 ...
    //             } else {
    //                 ...
    //             if (is->step && !is->paused)
    //                 // 此时刚好渲染完成一帧, 然后再次 toggle 为暂停状态
    //                 stream_toggle_pause(is);
    //     ... ...
    is->step = 1;
}

static double compute_target_delay(double delay, VideoState *is)
{
    // 同步阈值, 差值
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    // 更新延迟以跟随主同步源, 如果主时钟不是视频时钟源
    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        // 如果视频时钟是从属时钟源, 我们会尝试通过复制或删除帧来纠正较大的延迟
        // 首先计算视频时钟和主时钟的延迟, 这个延迟可能是正的也可能是负的
        diff = get_clock(&is->vidclk) - get_master_clock(is);

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        // 跳过或重复帧, 我们考虑延迟来计算阈值, 我仍然不知道这是否是最好的猜测
        // 同步阈值从两个数值中取最大的
        //     第一个数值: AV_SYNC_THRESHOLD_MIN, 这是最小的同步延迟, 如果低于这个延迟, 则不会进行矫正
        //     第二个数值: 从 AV_SYNC_THRESHOLD_MAX 和 计算的delay中选择一个
        //         对于第二个数值, 如果delay比较小, 则选择delay, 如果delay太大, 达到超过 AV_SYNC_THRESHOLD_MAX
        //         则认为delay太大尝试按照 AV_SYNC_THRESHOLD_MAX 来
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        // 如果diff是个合法的数字, 并且它的绝对值小于max_frame_duration, 那么说明它是个合理的延迟时间
        if (!isnan(diff) && fabs(diff) < is->max_frame_duration) {
            // 如果diff小于负数的 sync_threshold 这说明视频时钟落后主时钟的时间太多
            if (diff <= -sync_threshold)
                // 此时延迟应取 delay + diff 和 0 两者的最大者
                // 这里分两种情况, 此时的diff是个负数:
                //     如果它加上delay(正数)大于0, 则表示经过 (delay(正数) + diff(负数))后的到的小延迟可以让diff变为0
                //     如果它加上delay(正数)小于0, 则表示经过 delay(正数) + diff(负数))后的到的延迟是负数, 修正为0后
                //         即哪怕是不延迟立刻播放也只能让视频时钟源对主时钟源进行一次追赶
                delay = FFMAX(0, delay + diff);
            // 如果diff的时间是大于同步延迟, 并且delay的时长是必将长的, 设置大于 AV_SYNC_FRAMEDUP_THRESHOLD
            // 则不会对帧进行复制
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                // 此时diff是非常大的, 因此视频帧远早于音频帧, 因此光delay是不够的, 要把时钟源的差值也算进去
                delay = delay + diff;
            // 如果视频时钟是大于同步阈值的, 这说明时钟源过快, 因此delay按两倍算
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }
    // 其它情况, 返回delay原值

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
            delay, -diff);

    // 返回经过计算后的延迟, 该数值总是大于等于0的
    return delay;
}

// 算名义上两帧间的播放时长
static double vp_duration(VideoState *is, Frame *vp, Frame *nextvp) {
    // 如果两帧的序列相同, 则执行计算, 否则直接返回 0.0
    if (vp->serial == nextvp->serial) {
        // 计算播放时长即两帧的 PTS 的差
        double duration = nextvp->pts - vp->pts;
        // 如果差不是一个数字, 或者差小于等于0, 又或者值大于整个视频的播放时长
        // 如果 vp 和 nextvp 是同一帧, 则直接取vp的时长就可以了
        if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
            // 则直接返回当前帧自己的播放时长
            return vp->duration;
        else
            // 如果通过 PTS 计算的播放时长是合理的, 则直接返回
            return duration;
    } else {
        return 0.0;
    }
}

static void update_video_pts(VideoState *is, double pts, int64_t pos, int serial) {
    /* update current video pts */
    // 更新当前时钟源为当前画面的PTS
    set_clock(&is->vidclk, pts, serial);
    // 同步视频时钟源头到外部时钟源
    sync_clock_to_slave(&is->extclk, &is->vidclk);
}

/* called to display each frame */
// 视频刷新流程
static void video_refresh(void *opaque, double *remaining_time)
{
    // 配额为 VideoState
    VideoState *is = opaque;
    // 当前时间
    double time;

    // 两帧解码后的视频帧
    Frame *sp, *sp2;

    // 如果是播放状态, 并且主时钟为外部时钟, 并且还有真实时间的参考
    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        check_external_clock_speed(is);

    // 如果没有禁止显示渲染, 并且显示模式不是VIDEO, 则是光谱渲染, 这是"恰好"音频流也是有的
    // 那么符合光谱渲染的情况
    if (!display_disable && is->show_mode != SHOW_MODE_VIDEO && is->audio_st) {
        // 的到当前时间(以秒为单位)
        time = av_gettime_relative() / 1000000.0;
        // 如果是处于强制刷新的情况或者流的最后回访时间加上DFT速度小于当前时间
        // 这说明此时渲染的帧是此前时间点本应该渲染的, 因此要立刻执行渲染
        if (is->force_refresh || is->last_vis_time + rdftspeed < time) {
            video_display(is);
            // 记录最后一次DFT渲染时间
            is->last_vis_time = time;
        }
        // 计算下次重入本函数(video_refresh())的时候需要主动等待的时间, 该时间可能为0或者正数
        // 如果刚好为0, 那么下次渲染将立刻进入 video_refresh()
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + rdftspeed - time);
    }

    if (is->video_st) {
retry:
        // 如果 seek 正在进行, 则可能有一段时间, 画面队列中并没有可显示的画面
        if (frame_queue_nb_remaining(&is->pictq) == 0) {
            // nothing to do, no picture to display in the queue
        } else {
            // 此时有两种情况, 正常播放, 拿到的画面都是连续的
            // 还有一种情况, 拿到的画面是seek后画面队列中的第一帧
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            // 从画面队列中取出最近显示过的画面
            lastvp = frame_queue_peek_last(&is->pictq);
            // 提取一帧将要显示的画面 
            vp = frame_queue_peek(&is->pictq);
            // 注意: 如果是第一张画面, 则 lastvp 和 vp应是同一个

            // 如果这两个画面不是一个序列, 那么 直接渲染一帧最新的未显示过图像
            if (vp->serial != is->videoq.serial) {
                frame_queue_next(&is->pictq);
                goto retry;
            }
            // 如果最近显示过的画面和最近未显示的画面不属于同一个序列, 则更新 frame_timer 为当前时间
            if (lastvp->serial != vp->serial)
                is->frame_timer = av_gettime_relative() / 1000000.0;

            // 对于刷新线程, 如果处于暂停状态, 则直接跳到显示, 显示当前帧
            if (is->paused)
                goto display;

            /* compute nominal last_duration */
            // 计算名义上的last_duration, 也就是最近一帧的理论播放时长
            // 也就是计算理论上上一张显示过的画面和当前画面的理论应播放时长
            last_duration = vp_duration(is, lastvp, vp);
            // 计算当前画面理论上的播放延迟, 返回值总是大于等于0, 如果是正常播放, 那么它应该等于 1 / FPS
            // 要么立刻渲染, 使视频的时钟源尽快追赶上音频时钟源
            // 要么等待足够长的delay, 等待主时钟源追赶上视频时钟源
            delay = compute_target_delay(last_duration, is);

            // 获取当前的时间
            time= av_gettime_relative()/1000000.0;
            // 如果当前时间小于定时器加上延迟, 这说明上一画面的显示时刻未满, 则重复显示上一帧
            if (time < is->frame_timer + delay) {
                // 修正延迟时间, 尝试在一定的延迟后, 到达上一画面的展示时间点
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                // 重复显示上一画面
                goto display;
            }

            // 说明上一帧显示结束, 增加 frame_timer 的延迟
            is->frame_timer += delay;
            // 如果delay是大于0的, 并且 frame_timer 远落后于当前时间(大于: AV_SYNC_THRESHOLD_MAX)
            // 则直接修正 frame_timer 为当前时刻, 这种情况在播放第一帧或者已经暂停很就以后的情况
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            SDL_LockMutex(is->pictq.mutex);
            // 如果当前画面的展示时机有效
            if (!isnan(vp->pts))
                // 更新视频时钟源
                update_video_pts(is, vp->pts, vp->pos, vp->serial);
            SDL_UnlockMutex(is->pictq.mutex);

            // 如果还有未显示的画面, 考虑下一个画面的情况
            if (frame_queue_nb_remaining(&is->pictq) > 1) {
                // 获取将要显示的第二个未显示的画面, 比当前未显示的还大一个
                Frame *nextvp = frame_queue_peek_next(&is->pictq);
                // 计算当前未显示画面到下第二个未显示画面的理论播放时长
                duration = vp_duration(is, vp, nextvp);
                // 如果未处于 seek 状态, 并且允许丢帧, 并且符合丢帧条件:
                //     如果当前时间大于第二个未显示画面加上播放时长, 这说明下一帧的播放时间过早, 已经错过了
                //     错过显示的时间, 肯能是解码性能过低导致的, 因此考虑是否丢掉下一帧为显示的图像
                if(!is->step && (framedrop>0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration){
                    // 如果判定丢掉下一帧, 统计丢帧的总体数量 
                    is->frame_drops_late++;
                    frame_queue_next(&is->pictq);
                    // 然后跳到 retry再次计算
                    goto retry;
                }
            }

            if (is->subtitle_st) {
                while (frame_queue_nb_remaining(&is->subpq) > 0) {
                    sp = frame_queue_peek(&is->subpq);

                    if (frame_queue_nb_remaining(&is->subpq) > 1)
                        sp2 = frame_queue_peek_next(&is->subpq);
                    else
                        sp2 = NULL;

                    if (sp->serial != is->subtitleq.serial
                            || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                            || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                    {
                        if (sp->uploaded) {
                            int i;
                            for (i = 0; i < sp->sub.num_rects; i++) {
                                AVSubtitleRect *sub_rect = sp->sub.rects[i];
                                uint8_t *pixels;
                                int pitch, j;

                                if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
                                    for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                        memset(pixels, 0, sub_rect->w << 2);
                                    SDL_UnlockTexture(is->sub_texture);
                                }
                            }
                        }
                        frame_queue_next(&is->subpq);
                    } else {
                        break;
                    }
                }
            }

            // 设置rindex跳到下一帧画面
            frame_queue_next(&is->pictq);
            // 设置强制渲染为1, 因为此时有必要显示画面的
            is->force_refresh = 1;

            // 如果 is->step 为1, 这说明刚执行完 seek
            // 此时刚好渲染完成一帧, 然后再次 toggle 为暂停状态
            if (is->step && !is->paused)
                stream_toggle_pause(is);
        }
display:
        /* display picture */
        // 显示画面, 需要满足4个条件: 
        //     1. 如果显示未禁止
        //     2. 强制刷新有效
        //     3. 显示模式是VIDEO
        //     4. 当前画面可正常显示
        if (!display_disable && is->force_refresh && is->show_mode == SHOW_MODE_VIDEO && is->pictq.rindex_shown)
            video_display(is);
    }
    // 重置强制刷新的状态
    is->force_refresh = 0;
    // 如果需要展示状态
    if (show_status) {
        // Log输出前的缓冲区
        AVBPrint buf;
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize;
        double av_diff;

        // 获取当前时间
        cur_time = av_gettime_relative();
        // 如果最近的时间为空(第一帧), 或者本次刷新距离上次刷新的时间过长(超过30ms), 则执行进一步操作
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            // 如果有音频数据, 则获取音频队列长度
            if (is->audio_st)
                aqsize = is->audioq.size;
            // 如果有视频数据, 则获取视频队列长度
            if (is->video_st)
                vqsize = is->videoq.size;
            // 如果有字幕数据, 则获取字幕队列长度
            if (is->subtitle_st)
                sqsize = is->subtitleq.size;
            av_diff = 0;
            // 如果音频与视频都存在, 获取音频与视频时钟差距
            if (is->audio_st && is->video_st)
                av_diff = get_clock(&is->audclk) - get_clock(&is->vidclk);
            // 如果音频流不存在, 则获取视频与主时钟的差异
            else if (is->video_st)
                av_diff = get_master_clock(is) - get_clock(&is->vidclk);
            // 如果视频流不存在, 则获取音频与主时钟的差异
            else if (is->audio_st)
                av_diff = get_master_clock(is) - get_clock(&is->audclk);

            av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
            av_bprintf(&buf,
                      "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
                      // 主时钟
                      get_master_clock(is),
                      // 如果音视频都存在, 则展示"A-V", 否则分别展示主时钟与音视频其中一个流的差异
                      (is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
                      // 展示音视频流的差异
                      av_diff,
                      // 展示所有丢帧的数量, 包括已经丢掉和稍后丢掉的画面数量 
                      is->frame_drops_early + is->frame_drops_late,
                      // 展示音频队列的长度(kB)
                      aqsize / 1024,
                      // 展示视频队列的长度(kB)
                      vqsize / 1024,
                      // 展示字母队列的长度(kB)
                      sqsize,
                      // 展示错误的dts和pts帧数量
                      is->video_st ? is->viddec.avctx->pts_correction_num_faulty_dts : 0,
                      is->video_st ? is->viddec.avctx->pts_correction_num_faulty_pts : 0);

            // 如果 show_status 为 1 , 则展示上述的log内容
            if (show_status == 1 && AV_LOG_INFO > av_log_get_level())
                fprintf(stderr, "%s", buf.str);
            else
                av_log(NULL, AV_LOG_INFO, "%s", buf.str);

            fflush(stderr);
            av_bprint_finalize(&buf, NULL);

            last_time = cur_time;
        }
    }
}

// 插入AVFrame到渲染队列中, 该视频帧将在 video_refresh()中进行处理.
static int queue_picture(VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    // 生成一个帧描述
    Frame *vp;

    // 打印AVFrame的部分信息, 做同步调试的时候很有用
#if defined(DEBUG_SYNC)
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif
    // 从视频帧队列中获取一个可写的描述
    if (!(vp = frame_queue_peek_writable(&is->pictq)))
        return -1;

    // 保存当前视频帧的宽高比
    vp->sar = src_frame->sample_aspect_ratio;
    // 帧是否已经上传过? 否
    vp->uploaded = 0;

    // 保存长宽以及像素格式信息
    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    // 保存当前帧的展示时机
    vp->pts = pts;
    // 保存当前帧的播放时长
    vp->duration = duration;
    // 保存当前帧相对开始帧的偏移位置
    vp->pos = pos;
    // 保存当前帧的序列信息
    vp->serial = serial;

    // 按照视频帧的大小设置窗口的大小
    set_default_window_size(vp->width, vp->height, vp->sar);

    // 转移源AVFrame的引用到vp->frame
    // 注意vp是is->picq.queue数组中的一项AVFrame, 该AVFrame已在 frame_queue_init()中分配过内存了
    av_frame_move_ref(vp->frame, src_frame);
    // 修正队列长度, 并通知等待该队列
    frame_queue_push(&is->pictq);
    return 0;
}

// 获取一帧解码后的图像
static int get_video_frame(VideoState *is, AVFrame *frame)
{
    // 是否有拿到解码图像
    int got_picture;

    // 如果 got_picture 小于0, 则表示未解码出图像, 报错返回
    if ((got_picture = decoder_decode_frame(&is->viddec, frame, NULL)) < 0)
        return -1;

    // 如果成功获取到图像
    if (got_picture) {
        // 画面的展示时机
        double dpts = NAN;

        // 如果画面的战士时基是有效的
        if (frame->pts != AV_NOPTS_VALUE)
            // 则根据视频流的时基计算展示时间(相对有开始时间)
            dpts = av_q2d(is->video_st->time_base) * frame->pts;

        // 获取画面的宽高比
        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

        // 如果需要丢帧的数量大于0
        // 或者有设置CPU性能不足时可以丢帧, 并且获取到的主时钟不是视频时钟源, 则执行丢帧
        // 否则不丢帧, 正常返回即可
        if (framedrop>0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
            // 如果画面的展示时间有效, 则要进行一些处理
            if (frame->pts != AV_NOPTS_VALUE) {
                // 获取当前视频播放时间和主时钟当前时间的差异
                double diff = dpts - get_master_clock(is);
                // 如果差异不是非法的
                // 并且其绝对值小于禁止同步的阈值
                // 并且视频解码包序列和视频时钟源序列相同
                // 并且视频队列中还有未解码的其它数据包
                // 则执行丢帧操作
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - is->frame_last_filter_delay < 0 &&
                    is->viddec.pkt_serial == is->vidclk.serial &&
                    is->videoq.nb_packets) {
                    // 记录丢帧的数量
                    is->frame_drops_early++;
                    // 丢帧
                    av_frame_unref(frame);
                    // 设置为拿到图像
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}

#if CONFIG_AVFILTER
static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut *outputs = NULL, *inputs = NULL;

    if (filtergraph) {
        outputs = avfilter_inout_alloc();
        inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;

        inputs->name        = av_strdup("out");
        inputs->filter_ctx  = sink_ctx;
        inputs->pad_idx     = 0;
        inputs->next        = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    } else {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

static int configure_video_filters(AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame)
{
    // ffmpeg的像素格式和SDL的纹理格式见的对应关系表
    enum AVPixelFormat pix_fmts[FF_ARRAY_ELEMS(sdl_texture_format_map)];
    // 画面重采样的设置
    char sws_flags_str[512] = "";
    // 图像输入缓冲区的参数
    char buffersrc_args[256];
    int ret;
    // 效果器输入上下文, 输出上下文, 以及最终的上下文
    AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
    // 视频输入流的编码参数
    AVCodecParameters *codecpar = is->video_st->codecpar;
    // 获取视频帧率
    AVRational fr = av_guess_frame_rate(is->ic, is->video_st, NULL);
    // 配置字典
    AVDictionaryEntry *e = NULL;
    // 用于记录ffmpeg支持的当前SDL渲染上下文中支持的纹理对应项的索引
    int nb_pix_fmts = 0;
    int i, j;

    // 遍历SDL支持的纹理格式
    for (i = 0; i < renderer_info.num_texture_formats; i++) {
        // 遍历和ffmpeg的格式映射表
        for (j = 0; j < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; j++) {
            // 如果当前渲染上下文中的SDL纹理格式刚好在映射表中, 记录并保存这些支持的格式
            if (renderer_info.texture_formats[i] == sdl_texture_format_map[j].texture_fmt) {
                pix_fmts[nb_pix_fmts++] = sdl_texture_format_map[j].format;
                break;
            }
        }
    }
    // 标记表结尾
    pix_fmts[nb_pix_fmts] = AV_PIX_FMT_NONE;

    // 通过重采样配置字典生成配置字符串
    // 如果字典中有"sws_flags", 则将其值设置到参数字符串的"flags"字段, 这里是特别的设置
    // 字典中的其余设置, 均正常设置即可, 各项设置间使用':'分隔
    while ((e = av_dict_get(sws_dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        if (!strcmp(e->key, "sws_flags")) {
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
        } else
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    }
    // 如果配置字符串长度有效, 则设置结尾符号
    if (strlen(sws_flags_str))
        sws_flags_str[strlen(sws_flags_str)-1] = '\0';

    // 将配置参数字符串记录下来
    graph->scale_sws_opts = av_strdup(sws_flags_str);

    // 设置其余参数
    //     video_size(画面尺寸) = <画面宽度> * <画面高度>
    //     pix_fmt(像素格式) = 解码视频帧的格式
    //     time_base(效果器的时基参数) = 解码视频帧所对应的视频流的时基
    //     pixel_aspect(画面比例) = <画面宽度> / <画面高度>
    snprintf(buffersrc_args, sizeof(buffersrc_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame->width, frame->height, frame->format,
             is->video_st->time_base.num, is->video_st->time_base.den,
             codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1));
    // 如果帧率是有效的, 则进一步设置帧率: frame_rate
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

    // 使用配置字符串创建效果器输入, 如果创建失败, 则返回报错
    if ((ret = avfilter_graph_create_filter(&filt_src,
                                            avfilter_get_by_name("buffer"),
                                            "ffplay_buffer", buffersrc_args, NULL,
                                            graph)) < 0)
        goto fail;
    // 如果创建效果器管线图成功, 创建另一个效果器输出
    ret = avfilter_graph_create_filter(&filt_out,
                                       avfilter_get_by_name("buffersink"),
                                       "ffplay_buffersink", NULL, NULL, graph);
    if (ret < 0)
        goto fail;

    // 将二进制选项设置为整数列表, 这里单独设置"pix_fmts"
    if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts,  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;

    // 保存输出效果器为 last_filter
    last_filter = filt_out;

/* Note: this macro adds a filter before the lastly added filter, so the
 * processing order of the filters is in reverse */
// 注意：此宏在最后添加的过滤器之前添加了过滤器，因此过滤器的处理顺序相反
// avfilter_link() 负责建立效果器间的连接
// 注意link的步骤, 此时 laster_filter, 由于link过程是反向的
// 因此新的filter会串在out前面, 然后此时last_filter更新为新的filter
// 所有插入完成后, last_filter等于最后一个插入的效果器, 而out此时已经处于管线的末尾
#define INSERT_FILT(name, arg) do {                                          \
    AVFilterContext *filt_ctx;                                               \
                                                                             \
    ret = avfilter_graph_create_filter(&filt_ctx,                            \
                                       avfilter_get_by_name(name),           \
                                       "ffplay_" name, arg, NULL, graph);    \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    ret = avfilter_link(filt_ctx, 0, last_filter, 0);                        \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    last_filter = filt_ctx;                                                  \
} while (0)

    // 如果支持自动旋转
    if (autorotate) {
        // 首先获取视频输入流的宽高比例
        double theta  = get_rotation(is->video_st);

        if (fabs(theta - 90) < 1.0) {
            // 如果角度的绝对值减去90小于1, 则表示转90度, 此时: src -> transpose(clock表示正向) -> sink
            INSERT_FILT("transpose", "clock");
        } else if (fabs(theta - 180) < 1.0) {
            // 如果角度的绝对值减去180小于1, 则表示旋转90度, 此时: src -> hflip -> vflip -> sink
            INSERT_FILT("hflip", NULL);
            INSERT_FILT("vflip", NULL);
        } else if (fabs(theta - 270) < 1.0) {
            // 如果角度的绝对值减去270小于1, 则表示旋转270度, 此时: src -> transpose(cclock表示反向) -> sink
            INSERT_FILT("transpose", "cclock");
        } else if (fabs(theta) > 1.0) {
            // 如果从元数据中得到的数值和90/180/270差异大于1度, 则使用"rotate"滤镜
            // 此时: src - > rotate(theta * 2 * PI / 360) -> sink
            // 也就是: src - > rotate(theta * PI / 180) -> sink
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            INSERT_FILT("rotate", rotate_buf);
        }
    }
    // 构造渲染器管线图, 由于此前 INSERT_FILT() 的每个效果器都已经通过 avfilter_link() 建立了连接
    // 因此, 此时仅需链接最后一个插入的filter, 也就是last_filter和filt_src就可以完成效果器管线图的构造
    if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0)
        goto fail;
    // 分别记录收入和输出的效果器引用
    is->in_video_filter  = filt_src;
    is->out_video_filter = filt_out;

fail:
    return ret;
}

// 音频效果器配置, 从一个输入效果器名字开始, 设置好 src/sink 
static int configure_audio_filters(VideoState *is, const char *afilters, int force_output_format)
{
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    int sample_rates[2] = { 0, -1 };
    int64_t channel_layouts[2] = { 0, -1 };
    int channels[2] = { 0, -1 };
    AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
    char aresample_swr_opts[512] = "";
    AVDictionaryEntry *e = NULL;
    char asrc_args[256];
    int ret;

    avfilter_graph_free(&is->agraph);
    if (!(is->agraph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);
    is->agraph->nb_threads = filter_nbthreads;

    while ((e = av_dict_get(swr_opts, "", e, AV_DICT_IGNORE_SUFFIX)))
        av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    if (strlen(aresample_swr_opts))
        aresample_swr_opts[strlen(aresample_swr_opts)-1] = '\0';
    av_opt_set(is->agraph, "aresample_swr_opts", aresample_swr_opts, 0);

    ret = snprintf(asrc_args, sizeof(asrc_args),
                   "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
                   is->audio_filter_src.freq, av_get_sample_fmt_name(is->audio_filter_src.fmt),
                   is->audio_filter_src.channels,
                   1, is->audio_filter_src.freq);
    if (is->audio_filter_src.channel_layout)
        snprintf(asrc_args + ret, sizeof(asrc_args) - ret,
                 ":channel_layout=0x%"PRIx64,  is->audio_filter_src.channel_layout);

    ret = avfilter_graph_create_filter(&filt_asrc,
                                       avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                       asrc_args, NULL, is->agraph);
    if (ret < 0)
        goto end;


    ret = avfilter_graph_create_filter(&filt_asink,
                                       avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                       NULL, NULL, is->agraph);
    if (ret < 0)
        goto end;

    if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts,  AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;
    if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;

    if (force_output_format) {
        channel_layouts[0] = is->audio_tgt.channel_layout;
        channels       [0] = is->audio_tgt.channel_layout ? -1 : is->audio_tgt.channels;
        sample_rates   [0] = is->audio_tgt.freq;
        if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_layouts", channel_layouts,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_counts" , channels       ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "sample_rates"   , sample_rates   ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
    }


    if ((ret = configure_filtergraph(is->agraph, afilters, filt_asrc, filt_asink)) < 0)
        goto end;

    is->in_audio_filter  = filt_asrc;
    is->out_audio_filter = filt_asink;

end:
    if (ret < 0)
        avfilter_graph_free(&is->agraph);
    return ret;
}
#endif  /* CONFIG_AVFILTER */

// 音频解码线程
static int audio_thread(void *arg)
{
    // VideoState 在 SDL_CreateThread()创建本线程时作为音频解码线程的参数
    VideoState *is = arg;
    AVFrame *frame = av_frame_alloc();
    Frame *af;
#if CONFIG_AVFILTER
    int last_serial = -1;
    int64_t dec_channel_layout;
    int reconfigure;
#endif
    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    do {
        // 从音频解码器中取得一帧已经解码完成的数据, 如果失败, 则推出
        if ((got_frame = decoder_decode_frame(&is->auddec, frame, NULL)) < 0)
            goto the_end;

        // 如果成功获取了一帧, 即 got_frame 是大于0的
        if (got_frame) {
                // 获取解码出的音频帧的时基, 该时基 = 1 / 采样率
                tb = (AVRational){1, frame->sample_rate};
                // 如果ffmpeg版本支持效果器
#if CONFIG_AVFILTER
                // 获取解码后音频帧中有效的通道布局
                dec_channel_layout = get_valid_channel_layout(frame->channel_layout, frame->channels);

                // 根据效果器源和输出判定效果器能否接受解码出的音频帧
                reconfigure =
                    cmp_audio_fmts(is->audio_filter_src.fmt, is->audio_filter_src.channels,
                                   frame->format, frame->channels)    ||
                    is->audio_filter_src.channel_layout != dec_channel_layout ||
                    is->audio_filter_src.freq           != frame->sample_rate ||
                    is->auddec.pkt_serial               != last_serial;

                // 如果解码后的音频帧配置与效果器要求的不同, 则需要进行重新配置效果器
                if (reconfigure) {
                    char buf1[1024], buf2[1024];
                    // 获取效果器的输入端的通道布局名称
                    av_get_channel_layout_string(buf1, sizeof(buf1), -1, is->audio_filter_src.channel_layout);
                    // 获取解码后音频帧中有效的通道布局名称
                    av_get_channel_layout_string(buf2, sizeof(buf2), -1, dec_channel_layout);

                    // 打印重新配置的一些信息
                    av_log(NULL, AV_LOG_DEBUG,
                           "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                           is->audio_filter_src.freq, is->audio_filter_src.channels, av_get_sample_fmt_name(is->audio_filter_src.fmt), buf1, last_serial,
                           frame->sample_rate, frame->channels, av_get_sample_fmt_name(frame->format), buf2, is->auddec.pkt_serial);

                    // 配置效果器输入段的参数
                    // 采样格式
                    is->audio_filter_src.fmt            = frame->format;
                    // 通道数
                    is->audio_filter_src.channels       = frame->channels;
                    // 通道布局
                    is->audio_filter_src.channel_layout = dec_channel_layout;
                    // 采样频率
                    is->audio_filter_src.freq           = frame->sample_rate;
                    // 包序列
                    last_serial                         = is->auddec.pkt_serial;

                    // 再次配置音频效果器
                    if ((ret = configure_audio_filters(is, afilters, 1)) < 0)
                        goto the_end;
                }

            // 将解码后的音频帧添加到效果器输入
            if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
                goto the_end;

            // 从效果器输出中获取一帧处理后的音频数据, 正常情况下, av_buffersink_get_frame_flags() 应返回大于0的数据长度
            while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0) {
                tb = av_buffersink_get_time_base(is->out_audio_filter);
#endif
                // 如果配置了效果器, 这里获取到的是效果器输出的
                // 如果没有配置效果器, 这里获取到的是解码后的
                if (!(af = frame_queue_peek_writable(&is->sampq)))
                    goto the_end;

                // 检查音频帧中的PTS是否有效, 如果有效, 则记录该帧的展示时间
                af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                // 记录该帧在流中的位置, 该值仅在解码时被读取, 编码时不必理会
                af->pos = frame->pkt_pos;
                // 包序列
                af->serial = is->auddec.pkt_serial;
                af->duration = av_q2d((AVRational){frame->nb_samples, frame->sample_rate});

                av_frame_move_ref(af->frame, frame);
                frame_queue_push(&is->sampq);

#if CONFIG_AVFILTER
                if (is->audioq.serial != is->auddec.pkt_serial)
                    break;
            }
            if (ret == AVERROR_EOF)
                is->auddec.finished = is->auddec.pkt_serial;
#endif
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
 the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&is->agraph);
#endif
    av_frame_free(&frame);
    return ret;
}

static int decoder_start(Decoder *d, int (*fn)(void *), const char *thread_name, void* arg)
{
    // 启动包队列
    packet_queue_start(d->queue);
    // 启动输入流的解码线程
    d->decoder_tid = SDL_CreateThread(fn, thread_name, arg);
    if (!d->decoder_tid) {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateThread(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}

static int video_thread(void *arg)
{
    // 配额为 VideoState
    VideoState *is = arg;
    // 视频输入流解码后的视频帧(仅AVFrame, 不含任何数据)
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;

    // 获取视频输入流的时基
    AVRational tb = is->video_st->time_base;
    // 计算帧率, 例如fps是25, 该数值为: 1/25
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

    // 是否支持视频效果器(滤镜, 以下均简称效果器)
#if CONFIG_AVFILTER
    // FilterGraph, 效果器管线
    AVFilterGraph *graph = NULL;
    // 效果器上下文
    AVFilterContext *filt_out = NULL, *filt_in = NULL;

    // 最终的宽度
    int last_w = 0;
    // 最终高度
    int last_h = 0;
    // 最终的像素格式
    enum AVPixelFormat last_format = -2;
    // 最终处理完成后的序列
    int last_serial = -1;
    // 效果器序列
    int last_vfilter_idx = 0;
#endif

    // 如果视频帧描述结构 alloc 失败, 直接返回
    if (!frame)
        return AVERROR(ENOMEM);

    // 主循环
    for (;;) {
        // 试图解码一帧图像
        ret = get_video_frame(is, frame);
        // 如果是解码失败, 则返回负值, 直接跳出循环
        if (ret < 0)
            goto the_end;
        // 如果是没有得到图像(例如包队列当中无AVPacket, 或者暂时无法解出AVFrame), 返回的 got_picture 都是0
        if (!ret)
            continue;

        // 此时已经得到了解码后的画面
#if CONFIG_AVFILTER
        // 如果支持滤镜, 并且最终的输出画面和解码画面格式/尺寸等不同, 则需要进一步处理
        if (   last_w != frame->width
            || last_h != frame->height
            || last_format != frame->format
            || last_serial != is->viddec.pkt_serial
            || last_vfilter_idx != is->vfilter_idx) {
            av_log(NULL, AV_LOG_DEBUG,
                   "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                   last_w, last_h,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                   frame->width, frame->height,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(frame->format), "none"), is->viddec.pkt_serial);
            // 释放原有的效果器管线
            avfilter_graph_free(&graph);
            // 创建新的效果器管线
            graph = avfilter_graph_alloc();
            if (!graph) {
                ret = AVERROR(ENOMEM);
                goto the_end;
            }
            // 获取并保存渲染管线可用的线程数量
            graph->nb_threads = filter_nbthreads;
            // 重新配置视频效果器渲染管线
            if ((ret = configure_video_filters(graph, is, vfilters_list ? vfilters_list[is->vfilter_idx] : NULL, frame)) < 0) {
                // 如果创建效果器管线失败, 这通知SDL退出
                SDL_Event event;
                event.type = FF_QUIT_EVENT;
                event.user.data1 = is;
                SDL_PushEvent(&event);
                goto the_end;
            }
            // 创建效果器管线成功后, 获取输入输出效果器的引用
            filt_in  = is->in_video_filter;
            filt_out = is->out_video_filter;
            // 获解码后的视频帧的宽高
            last_w = frame->width;
            last_h = frame->height;
            // 获解码后的视频帧的像素格式
            last_format = frame->format;
            // 获视频输入流对应的解码器的包序列
            last_serial = is->viddec.pkt_serial;
            // 获取效果器的索引信息
            last_vfilter_idx = is->vfilter_idx;
            // 获取帧率并保存
            frame_rate = av_buffersink_get_frame_rate(filt_out);
        }

        // 将解码后的视频帧添加到收入效果器中
        ret = av_buffersrc_add_frame(filt_in, frame);
        if (ret < 0)
            goto the_end;

        // 如果添加成功, 进一步处理
        while (ret >= 0) {
            // 获取添加到效果器的时间(效果器的处理需要一定的时间, 由此产生的延迟是需要考虑的)
            is->frame_last_returned_time = av_gettime_relative() / 1000000.0;

            // 试图从输出效果器中得一帧处理后的画面
            // 如果返回0表示还未产生
            ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
            if (ret < 0) {
                if (ret == AVERROR_EOF)
                    is->viddec.finished = is->viddec.pkt_serial;
                ret = 0;
                break;
            }
            // 获取当前时间, 也是拿到输出效果器的视频帧的实际时间, 并减去最后一次返回的时间, 计算渲染管线的延迟
            is->frame_last_filter_delay = av_gettime_relative() / 1000000.0 - is->frame_last_returned_time;
            // 如果渲染器的延迟大于不可同步的时间差的十分之一, 则设置效果器延迟为0
            if (fabs(is->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
                is->frame_last_filter_delay = 0;
            // 获取输出效果器的时基, 以输出效果器的时基为准
            tb = av_buffersink_get_time_base(filt_out);
#endif
            // 获取时长信息, 如果帧率是有效的, 则当前视频帧的播放时长应为帧率的倒数
            duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
            // 计算视频帧的展示时机, 计算方式就是用时基的小数乘以当前帧的PTS即可
            // 如果视频帧曾经经过效果器管线, 则此时的时基是以输出效果器为准而计算的
            // 如果视频帧是直接解码出来的, 则时基是按照视频输入流的时基而定的
            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            // 将画面插入视频渲染队列
            ret = queue_picture(is, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
            av_frame_unref(frame);
#if CONFIG_AVFILTER
            if (is->videoq.serial != is->viddec.pkt_serial)
                break;
        }
#endif

        if (ret < 0)
            goto the_end;
    }
 the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&graph);
#endif
    av_frame_free(&frame);
    return 0;
}

static int subtitle_thread(void *arg)
{
    VideoState *is = arg;
    Frame *sp;
    int got_subtitle;
    double pts;

    for (;;) {
        if (!(sp = frame_queue_peek_writable(&is->subpq)))
            return 0;

        if ((got_subtitle = decoder_decode_frame(&is->subdec, NULL, &sp->sub)) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->sub.format == 0) {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = is->subdec.pkt_serial;
            sp->width = is->subdec.avctx->width;
            sp->height = is->subdec.avctx->height;
            sp->uploaded = 0;

            /* now we can update the picture count */
            frame_queue_push(&is->subpq);
        } else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
        }
    }
    return 0;
}

/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
// 如果 sync_type 是视频或外部主时钟，则返回所需的样本数以获得更好的同步
static int synchronize_audio(VideoState *is, int nb_samples)
{
    int wanted_nb_samples = nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        // 获取音频始终和主时钟的差异
        diff = get_clock(&is->audclk) - get_master_clock(is);

        // 如果差异不是非法的数值, 并且其绝对值在可同步的阈值内, 则可以进行同步
        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            // 根据当前的diff和以前的平均值做一个累加
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                // 没有足够的措施来进行正确的估计
                is->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                // 估计 A-V 差异
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                // 如果差异太大, 则重新计算 wanted_nb_samples , 并约束到一个合理的范围内
                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                        diff, avg_diff, wanted_nb_samples - nb_samples,
                        is->audio_clock, is->audio_diff_threshold);
            }
        } else {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum       = 0;
        }
    }

    return wanted_nb_samples;
}

/**
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
// 解码一帧音频并返回其未压缩的大小
// 处理后的音频帧被解码、转换（如果需要）并存储在 is->audio_buf 中
// 大小以字节为单位由返回值给出
static int audio_decode_frame(VideoState *is)
{
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;

    // 对于音频解码线程, 如果当前处于暂停状态, 则不再返回解码后的数据
    // 这将导致 AVPacket 很快被填满, 并且导致上层播放静音数据
    if (is->paused)
        return -1;

    // 获取所有刻度的音频数据
    do {
#if defined(_WIN32)
        while (frame_queue_nb_remaining(&is->sampq) == 0) {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        // 如果队列中无数据可获取, 则直接返回
        if (!(af = frame_queue_peek_readable(&is->sampq)))
            return -1;
        // 跳过队列中所有序列不正确的数据
        frame_queue_next(&is->sampq);
    } while (af->serial != is->audioq.serial);
    // 获取到音频数据

    // 获取数据的长度(字节为单位)
    data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
                                           af->frame->nb_samples,
                                           af->frame->format, 1);

    // 获取音频数据的通道布局
    dec_channel_layout =
        (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
    // 如果 sync_type 是视频或外部主时钟，则返回所需的样本数以获得更好的同步
    // 如果 sync_type 就是音频, 则直接返回 af->frame->nb_samples
    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

    // 如果音频解码后的数据和音频效果器输入端的数据格式不相同
    // 则需要对音频数据进行重采样
    if (af->frame->format        != is->audio_src.fmt            ||
        dec_channel_layout       != is->audio_src.channel_layout ||
        af->frame->sample_rate   != is->audio_src.freq           ||
        (wanted_nb_samples       != af->frame->nb_samples && !is->swr_ctx)) {
        // 释放重采样上下文
        swr_free(&is->swr_ctx);
        // 设置符合 is->audio_src 格式的重采样上下文
        is->swr_ctx = swr_alloc_set_opts(NULL,
                                         is->audio_tgt.channel_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
                                         dec_channel_layout,           af->frame->format, af->frame->sample_rate,
                                         0, NULL);
        // 初始化音频重采样器
        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_get_sample_fmt_name(af->frame->format), af->frame->channels,
                    is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            swr_free(&is->swr_ctx);
            return -1;
        }
        // 设置新的参数
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels       = af->frame->channels;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = af->frame->format;
    }
    // 如果重采样上下文有效, 则说明需要执行重采样
    if (is->swr_ctx) {
        // 获取输入数据指针地址
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        // 获取输出数据指针地址
        uint8_t **out = &is->audio_buf1;
        // 计算输出的采样点数量, out_count = 原音频采样点数量 / 原音频采样速率 * 目标音频采样频率 + 256
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
        // 输出buffer大小则根据: 重采样输出的通道数量, 输出的采样点数量, 输出的采样点格式进行计算, 以保证有足够的空间保存重采样输出
        int out_size  = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
        int len2;
        // 检查输出大小的数值, 必须是大于等于0的数字
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        // 如果音频不是主时钟2, 则一下两项不相同, 需要进行补偿设置
        if (wanted_nb_samples != af->frame->nb_samples) {
            if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
                                        wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        // 为音频采样输出分配内存空间
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1)
            return AVERROR(ENOMEM);
        // 执行重采样, 设置输出采样点的长度
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        // 如果冲采样结束后, 返回的数据和输出数据长度相同, 则表示: 音频缓冲区可能太小
        // ???
        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }
        // 设置音频回放的buffer为采样后的音频你数据
        is->audio_buf = is->audio_buf1;
        // 重采样数据大小: 音频效果器输出的通道数量 * 以及输出的音频数据每采样点的大小
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    } else {
        // 如果不需要重采样, 则直接将audio_buf设置为解码后的音频数据即可
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
        is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock = NAN;
    is->audio_clock_serial = af->serial;
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               is->audio_clock - last_clock,
               is->audio_clock, audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}

/* prepare a new audio buffer */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    VideoState *is = opaque;
    int audio_size, len1;

    // 获取当前时间
    audio_callback_time = av_gettime_relative();

    // 如果SDL回调要求获取的数据长度大于0
    while (len > 0) {
        // 如果音频缓冲索引(未回放的数据指针)大于输入流的缓冲区大小, 说明 is->audio_buf 数据已经全部拷贝到了 SDL 的输出, 需要重新读取一帧
        if (is->audio_buf_index >= is->audio_buf_size) {
            // 则说明到达需要解码数据的时机, 则解码一帧数据回来
           audio_size = audio_decode_frame(is);
           // 如果解码数据失败(或无包可解码时), 设置音频缓冲为空, 大小为
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf = NULL;
               is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
           } else {
               // 如果展示模式不为视频模式, 则更新音频光谱渲染器需要的数据
               if (is->show_mode != SHOW_MODE_VIDEO)
                   update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
                // 记录本次获取到的解码后音频帧长度
               is->audio_buf_size = audio_size;
           }
           // 设置音频缓冲长度为0, 表示获取到一帧新的数据
           is->audio_buf_index = 0;
        }
        // 获取新读取的有效的音频数据长度
        len1 = is->audio_buf_size - is->audio_buf_index;
        // 如果获取到的长度比需要长度还长, 则设置len1为SDL需要的长度
        if (len1 > len)
            len1 = len;
        // 如果不是静音, 则拷贝 audio_buf + audio_buf_index 到SDL的输出缓冲区
        if (!is->muted && is->audio_buf && is->audio_volume == SDL_MIX_MAXVOLUME)
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        else {
            memset(stream, 0, len1);
            if (!is->muted && is->audio_buf)
                SDL_MixAudioFormat(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, AUDIO_S16SYS, len1, is->audio_volume);
        }
        // 减去已经拷贝的长度
        len -= len1;
        // 更新SDL缓冲区指针的偏移位置
        stream += len1;
        // 更新未写入到SDL音频输出缓冲区的数据指针的位置
        is->audio_buf_index += len1;
    }
    // 记录下次将要写入到缓冲区的数据长度, 该长度等于本次读取解码音频数据的长度减去写入SDL输出后的索引位置
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    // 假设SDL使用的音频驱动程序有两个周期
    if (!isnan(is->audio_clock)) {
        // is->audio_hw_buf_size 是硬件缓冲区的长度, 这里假设有两个
        // 然后 is->audio_hw_buf_size 加上还未写入的长度, 则表示播放完这些数据需要多少长度
        // 然后用这个长度除以每秒回访的大小得到, 这些数据多久能播放完
        // 然后用音频时钟减去这个播放时长得到此时此刻的PTS时间
        // 最后用实际时间, 和PTS时间设置给音频时钟, 可得到 pts_drift 时间
        set_clock_at(&is->audclk, is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec, is->audio_clock_serial, audio_callback_time / 1000000.0);
        // 同步 audclk 的时间到 extclk, 然后以 extclk 为准?
        sync_clock_to_slave(&is->extclk, &is->audclk);
    }
}

static int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
    // 准备音频渲染的参数, wanted_spec是期望的, spec是实际返回的
    SDL_AudioSpec wanted_spec, spec;
    // env变量, 因为可以通过SDL_AUDIO_CHANNELS指定通道数量, 所以要获取一下
    const char *env;
    // 
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    // 如果已经设置了 SDL_AUDIO_CHANNELS 环境变量, 则以 SDL_AUDIO_CHANNELS 的设置为准
    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        // 将 SDL_AUDIO_CHANNELS 的设置转为整数
        wanted_nb_channels = atoi(env);
        // 通过通道数获取通道布局
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    // 如果 SDL_AUDIO_CHANNELS 为制定通道数, 并且通道布局是空的, 或者通道布局有, 通道数量没有, 根据两者其一尝试获取另一个的信息
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    // 通过通道布局再修正一次通道数量
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    // 将通道数量设置给 wanted_spec
    wanted_spec.channels = wanted_nb_channels;
    // 设置采样频率
    wanted_spec.freq = wanted_sample_rate;
    // 如果采样频率小于0, 则报错返回 -1
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }

    // 遍历采样数组, 选择一个刚好大于定于期望采样的采样频率, 例如期望是16000, 则遍历后得到44100, 如果是44100, 遍历后得到44100
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    // 设置期望采样格式为有符号16位
    wanted_spec.format = AUDIO_S16SYS;
    // 期望不静音
    wanted_spec.silence = 0;
    // 设置期望的采样数量, 其值等同 wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC 按照2的幂次对齐
    // 比如 44100 / 30 = 1470, 对齐后为: 1024, 而不是默认的512
    // 如此设置, SDL的回调次数最大也就比30大一点
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    // 设置SDL音频数据的回调
    wanted_spec.callback = sdl_audio_callback;
    // 设置配额数据, 对于ffplay, 这里是: VideoState
    wanted_spec.userdata = opaque;
    // 打开音频设备
    while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
        // 如果打开音频设备失败, 调整参数重试一次
        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        // 重新调整通道数进行重试
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            // 如果始终无法打开音频设备, 则返回报错.
            if (!wanted_spec.freq) {
                av_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    // 如果成功打开了音频设备, 判定设备的采样格式是否还是S16, 如果不是, 则报错返回
    if (spec.format != AUDIO_S16SYS) {
        av_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    // 判断打开的通道数是否和期望的不符合, 如果是, 报错返回
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            av_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    // 保存实际的硬件渲染参数, 格式, 后续解码完成后要参考该配置进行重采样
    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    // 采样率
    audio_hw_params->freq = spec.freq;
    // 通道布局
    audio_hw_params->channel_layout = wanted_channel_layout;
    // 通道数
    audio_hw_params->channels =  spec.channels;
    // 采样点的尺寸, 一般是取一个采样点的大小 = 采样格式大小 * 通道数量
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    // 每秒的采样数据大小 = 采样格式大小 * 通道数量
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    // 返回 spec.size , 该尺寸其实是缓冲区大小
    return spec.size;
}

/* open a given stream. Return 0 if OK */
// 打开给定的媒体流, 可以是video/audio/subtitle, 将所有信息保存到is
static int stream_component_open(VideoState *is, int stream_index)
{
    // 获取封装上下文的引用
    AVFormatContext *ic = is->ic;
    // 当前输入流解码器上下文
    AVCodecContext *avctx;
    // 当前输入流对应的解码器
    const AVCodec *codec;
    // 强制解码器名称
    const char *forced_codec_name = NULL;
    // 解码器配置选项的字典
    AVDictionary *opts = NULL;
    // 上面字典的字典项
    AVDictionaryEntry *t = NULL;

    // 用于音频的采样率, 通道数信息
    int sample_rate, nb_channels;
    // 用于音频的通道布局, 单通道/双通道/2.1声道/5.1声道等
    int64_t channel_layout;
    int ret = 0;
    int stream_lowres = lowres;

    // 检查流索引, 其值应在合理的范围内
    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;

    // 分配一个解码器上下文
    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);

    // 从输入流中拷贝原有的编码器参数到解码器上下文以便可以正确解码输入流中的数据包
    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;
    // 保存输入流中的时基
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    // 查找输入流对应的解码器
    codec = avcodec_find_decoder(avctx->codec_id);

    // 根据不同的类型, 保存流索引/强制解码器名称
    switch(avctx->codec_type){
        case AVMEDIA_TYPE_AUDIO   : is->last_audio_stream    = stream_index; forced_codec_name =    audio_codec_name; break;
        case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; forced_codec_name = subtitle_codec_name; break;
        case AVMEDIA_TYPE_VIDEO   : is->last_video_stream    = stream_index; forced_codec_name =    video_codec_name; break;
    }

    // 如果用户强制制定了解码器, 则替换成用户制定的 AVCodec
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);

    // 如果自动解析的和用户制定的 AVCodec 都无法找到, 则报错返回 EINVAL
    if (!codec) {
        if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with name '%s'\n", forced_codec_name);
        else                   av_log(NULL, AV_LOG_WARNING,
                                      "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    // 保存找到解码器的 ID
    avctx->codec_id = codec->id;
    if (stream_lowres > codec->max_lowres) {
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                codec->max_lowres);
        stream_lowres = codec->max_lowres;
    }
    avctx->lowres = stream_lowres;

    // 如果设置了快速优化选项, 这在解码上下文的旗语中进行标识
    if (fast)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;

    // 从输入流中查找过滤器参数
    opts = filter_codec_opts(codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    // 获取参数中对效果器可使用线程数量的设置, 如果用户默认未设置, 则默认为"auto"
    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret =  AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    // 设置输入流的EOF为0, 表示未到流结尾
    is->eof = 0;
    // 设置输入流的忽略选项为默认忽略
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;

    // 根据不同的流类型保存相应的参数
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        // 如果编译时支持效果器
#if CONFIG_AVFILTER
        {
            AVFilterContext *sink;
            // 获取音频效果器收入频率
            is->audio_filter_src.freq           = avctx->sample_rate;
            // 获取音频效果器收入通道数
            is->audio_filter_src.channels       = avctx->channels;
            // 获取音频效果器收入通道布局
            is->audio_filter_src.channel_layout = get_valid_channel_layout(avctx->channel_layout, avctx->channels);
            // 获取音频效果器收入采样格式
            is->audio_filter_src.fmt            = avctx->sample_fmt;
            // 配置音频效果器, 这是第一次配置, 如果解码后的音频配置和本次从上下文中获取的不一样, 则会再重新配置一次.
            // 在 audio_thread() 有可能进行重配置
            if ((ret = configure_audio_filters(is, afilters, 0)) < 0)
                goto fail;
            // 保存配置过后的效果器的输出上下文
            sink = is->out_audio_filter;
            // 获取效果器输出的采样率
            sample_rate    = av_buffersink_get_sample_rate(sink);
            // 获取效果器输出的通道数
            nb_channels    = av_buffersink_get_channels(sink);
            // 获取效果器输出的通道布局
            channel_layout = av_buffersink_get_channel_layout(sink);
        }
#else
        // 如果编译时不支持效果器, 则直接保存默认配置
        sample_rate    = avctx->sample_rate;
        nb_channels    = avctx->channels;
        channel_layout = avctx->channel_layout;
#endif

        /* prepare audio output */
        // 准备音频输出, 打开SDL音频输出设备, 并保存硬件输出参数到 is->audio_tgt
        if ((ret = audio_open(is, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
            goto fail;
        // 保存硬件输出的缓冲区大小, 这个大小应该为 1 / SDL_AUDIO_MAX_CALLBACKS_PER_SEC ?
        is->audio_hw_buf_size = ret;
        // 保存硬件输出参数到 is->audio_src
        is->audio_src = is->audio_tgt;
        // 由于目前还未为音频输出缓冲区分配内存, 所以大小设置为0;
        is->audio_buf_size  = 0;
        // 音频数据bufer的索引也初始化为0
        is->audio_buf_index = 0;

        /* init averaging filter */
        // exp(log(0.01)/20) = 0.90483741803596
        is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio FIFO fullness,
           we correct audio sync only if larger than this threshold */
        // 由于我们没有足够的音频FIFO精确度，因此仅在大于此阈值的情况下才校正音频同步
        // 这个大小应该为 1 / SDL_AUDIO_MAX_CALLBACKS_PER_SEC ?
        is->audio_diff_threshold = (double)(is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;

        // 保存打开的音频流的索引
        is->audio_stream = stream_index;
        // 保存音频流描述
        is->audio_st = ic->streams[stream_index];

        // 初始化输入音频流解码器
        if ((ret = decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread)) < 0)
            goto fail;
        if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !is->ic->iformat->read_seek) {
            is->auddec.start_pts = is->audio_st->start_time;
            is->auddec.start_pts_tb = is->audio_st->time_base;
        }
        // 启动输入流音频解码线程: audio_thread
        if ((ret = decoder_start(&is->auddec, audio_thread, "audio_decoder", is)) < 0)
            goto out;
        // 暂停SDL音频回放, 此时还没有设置开始播放的标识
        SDL_PauseAudioDevice(audio_dev, 0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        // 保存视频输入流的索引
        is->video_stream = stream_index;
        // 保存视频输入流的描述
        is->video_st = ic->streams[stream_index];

        // 初始化视频解码器, 并设置视频流包队列到解码器
        if ((ret = decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread)) < 0)
            goto fail;
        // 启动视频解码线程: video_thread()
        if ((ret = decoder_start(&is->viddec, video_thread, "video_decoder", is)) < 0)
            goto out;
        // 设置 queue_attachments_req 为 1
        is->queue_attachments_req = 1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        // 保存字幕输入流的索引
        is->subtitle_stream = stream_index;
        // 保存字幕输入流的描述
        is->subtitle_st = ic->streams[stream_index];

        // 初始化字幕解码器, 并设置字幕流包队列到解码器
        if ((ret = decoder_init(&is->subdec, avctx, &is->subtitleq, is->continue_read_thread)) < 0)
            goto fail;
        // 启动字幕解码线程: subtitle_thread()
        if ((ret = decoder_start(&is->subdec, subtitle_thread, "subtitle_decoder", is)) < 0)
            goto out;
        break;
    default:
        break;
    }
    goto out;

fail:
    avcodec_free_context(&avctx);
out:
    av_dict_free(&opts);

    return ret;
}

static int decode_interrupt_cb(void *ctx)
{
    VideoState *is = ctx;
    return is->abort_request;
}

static int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue) {
    return stream_id < 0 ||
           queue->abort_request ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
           queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
}

static int is_realtime(AVFormatContext *s)
{
    if(   !strcmp(s->iformat->name, "rtp")
       || !strcmp(s->iformat->name, "rtsp")
       || !strcmp(s->iformat->name, "sdp")
    )
        return 1;

    if(s->pb && (   !strncmp(s->url, "rtp:", 4)
                 || !strncmp(s->url, "udp:", 4)
                )
    )
        return 1;
    return 0;
}

/* this thread gets the stream from the disk or the network */
// 读取线程, 负责从本地或网络读取流
static int read_thread(void *arg)
{
    // 在 SDL_CreateThread()时, VideoState 作为参数传递给本线程
    VideoState *is = arg;
    
    // 输入格式上下文
    AVFormatContext *ic = NULL;

    // 一些临时变量
    int err, i, ret;

    // 流索引信息
    int st_index[AVMEDIA_TYPE_NB];

    // 从流中读取到的包
    AVPacket *pkt = NULL;

    // 流开始时间
    int64_t stream_start_time;

    // 在播放范围内的包数量
    int pkt_in_play_range = 0;

    // 配置字典
    AVDictionaryEntry *t;

    // 创建一个等待用途的互斥锁
    SDL_mutex *wait_mutex = SDL_CreateMutex();

    // PMT: "Program Map Table"表
    // 如果scan_all_pmts_set为1, 则设置"scan_all_pmts"为1
    int scan_all_pmts_set = 0;

    // 包的时间戳
    int64_t pkt_ts;

    // 检查等待锁是否成功创建
    if (!wait_mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    // 初始化流索引的数组
    memset(st_index, -1, sizeof(st_index));
    // 设置 EOF 状态, 如果为0, 表示未到流结尾
    is->eof = 0;

    // 为流中读取的包描述结构分配空间, 注意: 此时仅分配AVPacket, 此时还没有任何压缩数据
    pkt = av_packet_alloc();
    if (!pkt) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate packet.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    // 分配一个输入格式上下文
    ic = avformat_alloc_context();
    if (!ic) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    
    // 设置解码中断回调, dcoder会不定期的调用该回调, 如果回调返回1, 则decoder流程将结束
    // 在ffplay中, decode 是否结束, 取决与 is->abort_request 的值, 该值在以下情况为1:
    //     * 初始化
    //     * packet_queue_abort() 被调用
    //     * stream_close() 被调用, 也就是流关闭时
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = is;

    // PMTs相关的设置
    // PMT（Program Map Table）：节目映射表，该表的PID是由PAT提供给出的
    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }

    // 打开输入流
    err = avformat_open_input(&ic, is->filename, is->iformat, &format_opts);
    if (err < 0) {
        print_error(is->filename, err);
        ret = -1;
        goto fail;
    }

    // 再次考虑PMTs
    if (scan_all_pmts_set)
        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    // 尝试获取任意一个参数?
    if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    // 保存输入上下文
    is->ic = ic;

    // 设置是否在缺失PTS信息时自动生成
    if (genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

    // 适用于整个流的辅助数据数组(即容器不允许其在数据包之间进行更改)
    av_format_inject_global_side_data(ic);

    // 默认 find_stream_info 是 1 , 因此该部分代码执行
    if (find_stream_info) {
        // 设置流查找时的选项
        // 创建一个字典数组，为s中包含的每个流创建一个字典
        // 每个字典将包含来自codec_opts的选项，这些选项可应用于相应的流编解码器上下文
        AVDictionary **opts = setup_find_stream_info_opts(ic, codec_opts);
        // 获取输入上下文中流的数量
        int orig_nb_streams = ic->nb_streams;

        //  查找流
        err = avformat_find_stream_info(ic, opts);

        // 释放流查找时使用 setup_find_stream_info_opts() 创建的选项数组
        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        // 如果没有找到有效的流, 那么报错并返回
        if (err < 0) {
            av_log(NULL, AV_LOG_WARNING,
                   "%s: could not find codec parameters\n", is->filename);
            ret = -1;
            goto fail;
        }
    }

    // 如果有设置io上下文(在 avformat_open_input()时被设置)
    // 则设置EOF到达标识为0, 这里涉及一个问题:
    // ffplay可能不应该使用avio_feof())测试结束, 但它有时会设置EOF标志
    if (ic->pb)
        ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    // 如果 seek_by_bytes 比0小, 比如-1(初始化值), 则需要做一些设置
    // 如果格式允许时间戳不连续. 那么请注意, 多路复用器始终需要有效的(单调)时间戳, 并且输入流为ogg
    if (seek_by_bytes < 0)
        seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

    //  一帧最大的播放时长
    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    // 如果没有设置标题, 尝试从输入上下文的源数据中提取
    if (!window_title && (t = av_dict_get(ic->metadata, "title", NULL, 0)))
        window_title = av_asprintf("%s - %s", t->value, input_filename);

    // 如果开始时间不是0, 即有要求seek到指定位置, 则进行 seek 操作: avformat_seek_file()
    /* if seeking requested, we execute it */
    if (start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        // 记下要 seek 到的开始时间
        timestamp = start_time;

        /* add the stream start time */
        // 将要求 seek 到的开始时间和流的开始时间相加
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;

        // 直接seek到对应的时间点
        ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                    is->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    // 获取是实时的流, 对于流媒体协议(rtp/rtsp/sdp), 该数会为1
    // 另外如果是rtp://或者udp://开头的url, 该值也是为1
    is->realtime = is_realtime(ic);

    // 如果设置了显示状态的标志, 则对输入流上下文信息进行dump
    if (show_status)
        av_dump_format(ic, 0, is->filename, 0);

    // 开始对流进行检索, 对于每一个流
    for (i = 0; i < ic->nb_streams; i++) {
        // 提取一个流信息
        AVStream *st = ic->streams[i];
        // 获取其类型
        enum AVMediaType type = st->codecpar->codec_type;
        // 设置忽略标志为: 忽略所有
        st->discard = AVDISCARD_ALL;
        // 如果流类型有效, 并且与用户期望的流类型相匹配, 并且流还没有被记录下来, 则记录
        if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic, st, wanted_stream_spec[type]) > 0)
                st_index[type] = i;
    }
    // 对于每一种流类型, 检查他们是否存在对应的索引, 这意味着文件中有相应的流类型
    for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
        if (wanted_stream_spec[i] && st_index[i] == -1) {
            av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", wanted_stream_spec[i], av_get_media_type_string(i));
            st_index[i] = INT_MAX;
        }
    }

    // 如果没有禁止视频, 则查找video流类型对应的索引
    if (!video_disable)
        st_index[AVMEDIA_TYPE_VIDEO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    // 如果没有禁止音频, 则查找audio流类型对应的索引
    if (!audio_disable)
        st_index[AVMEDIA_TYPE_AUDIO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                NULL, 0);
    // 如果没有禁止视频的同时, 也没有禁止字母, 则查找字幕流
    if (!video_disable && !subtitle_disable)
        st_index[AVMEDIA_TYPE_SUBTITLE] =
            av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                                st_index[AVMEDIA_TYPE_SUBTITLE],
                                (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                 st_index[AVMEDIA_TYPE_AUDIO] :
                                 st_index[AVMEDIA_TYPE_VIDEO]),
                                NULL, 0);

    // 保存展示模式
    is->show_mode = show_mode;

    // 如果成功找到了视频流
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        // 获取视频流结构
        AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        // 获取输入视频流中的编码信息
        AVCodecParameters *codecpar = st->codecpar;
        // 获取视频宽高比例, SAR(Sample Aspect Ratio, 视频的采样宽高比)
        AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
        // 如果有宽度信息, 则根据宽高信息和宽高比例设置窗口大小
        if (codecpar->width)
            set_default_window_size(codecpar->width, codecpar->height, sar);
    }

    /* open the streams */
    // 打开音频流(在音频流存在的时候)
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]);
    }

    ret = -1;
    // 打开视频流
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        ret = stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);
    }

    // 判断展示模式是否是无, 如果是无, 判断是打开流是否成功, 如果是无, 则
    if (is->show_mode == SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;

    // 如果是有字幕, 则初始化字幕组件
    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]);
    }

    // 要求音频/视频至少有一个流, 否则报错返回
    if (is->video_stream < 0 && is->audio_stream < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               is->filename);
        ret = -1;
        goto fail;
    }

    // 如果不限制buffer大小未设置, 并且是流媒体, 则甚至不限制buffer大小
    if (infinite_buffer < 0 && is->realtime)
        infinite_buffer = 1;

    // 读取线程的主循环
    for (;;) {
        // 判断是否终止, 如果是, 则退出循环
        if (is->abort_request)
            break;

        // 如果是: (播放 -> 暂停) 或者 (暂停 -> 播放), 则根据情况判断.
        if (is->paused != is->last_paused) {
            // 保存最后的暂停与否的状态
            is->last_paused = is->paused;
            // 如果是从播放变为暂停, 则执行 av_read_pause()
            if (is->paused)
                is->read_pause_return = av_read_pause(ic);
            // 否则执行播放
            else
                av_read_play(ic);
        }
        // 如果支持RTSP的和其它流媒体的解封装, 则执行进一步的操作
#if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
        // 如果是暂停状态, 并且输入流是"rtsp://"协议, 并且输入I/O操作可用时为"mmsh://"则主动延迟10ms
        if (is->paused &&
                (!strcmp(ic->iformat->name, "rtsp") ||
                 (ic->pb && !strncmp(input_filename, "mmsh:", 5)))) {
            /* wait 10 ms to avoid trying to get another packet */
            /* XXX: horrible */
            SDL_Delay(10);
            continue;
        }
#endif
        // 如果有seek请求(无论暂停与否)
        if (is->seek_req) {
            // 记录要 seek 的位置
            int64_t seek_target = is->seek_pos;
            // +-2是由于在生成 seek_pos / seek_rel 变量时未按正确的方向进行舍入
            // seek_rel是相对的seek范围
            int64_t seek_min    = is->seek_rel > 0 ? seek_target - is->seek_rel + 2: INT64_MIN;
            int64_t seek_max    = is->seek_rel < 0 ? seek_target - is->seek_rel - 2: INT64_MAX;
// FIXME the +-2 is due to rounding being not done in the correct direction in generation
//      of the seek_pos/seek_rel variables

            // 对输入流执行 seek 操作
            ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
            // seek 失败则报错返回
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", is->ic->url);
            } else {
                // 如果 seek 操作成功, 并且音频流有效, 则冲刷掉已经读取的包队列中的所有未解码AVPackeet
                if (is->audio_stream >= 0)
                    packet_queue_flush(&is->audioq);
                // 如果 seek 操作成功, 并且字幕流有效, 则冲刷掉已经读取的包队列中的所有未解码AVPackeet
                if (is->subtitle_stream >= 0)
                    packet_queue_flush(&is->subtitleq);
                // 如果 seek 操作成功, 并且视频流有效, 则冲刷掉已经读取的包队列中的所有未解码AVPackeet
                if (is->video_stream >= 0)
                    packet_queue_flush(&is->videoq);
                // 如果 seek 的方式是按照字节进行, 则设置外部始终为 NAN, 否则, 则设置始终为新的 seek 时间
                if (is->seek_flags & AVSEEK_FLAG_BYTE) {
                   set_clock(&is->extclk, NAN, 0);
                } else {
                    // 设置的数值是 seek 的目标时间 / 时基
                   set_clock(&is->extclk, seek_target / (double)AV_TIME_BASE, 0);
                }
            }
            // 复位 seek 请求标志
            is->seek_req = 0;
            // 队列附属请求设置为 1
            is->queue_attachments_req = 1;
            // 到达流结尾的标志设置为1, 因为 seek 可能刚好发生在播放完成
            is->eof = 0;
            // 如果 seek 操作发生在暂停状态, 则 seek 操作完成后, 立刻渲染一帧
            if (is->paused)
                step_to_next_frame(is);
        }
        // 如果队列附属请求为1, 则执行
        if (is->queue_attachments_req) {
            // 如果视频流有效, 流作为附件的图片/“封面”（例如ID3v2中的APIC帧）存储在文件中
            // 除非进行查找，否则从文件读取的前几个数据包中将返回与其关联的第一个（通常是唯一的）数据包
            // 也可以随时在AVStream.attached_pic中对其进行访问
            // 因此要多插入一个 AVPacket ? 
            if (is->video_st && is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                if ((ret = av_packet_ref(pkt, &is->video_st->attached_pic)) < 0)
                    goto fail;
                packet_queue_put(&is->videoq, pkt);
                // 再插入一个空包? 
                packet_queue_put_nullpacket(&is->videoq, pkt, is->video_stream);
            }
            // 重置标志位
            is->queue_attachments_req = 0;
        }

        /* if the queue are full, no need to read more */
        // 如果包队列是满的, 则不需要在进行读取
        // 这里有个地方值得注意, 就是读取线程是否停止读取, 取决于队列是否是满的, 而不是 is->paused 的状态
        if (infinite_buffer<1 &&
              (is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE
            || (stream_has_enough_packets(is->audio_st, is->audio_stream, &is->audioq) &&
                stream_has_enough_packets(is->video_st, is->video_stream, &is->videoq) &&
                stream_has_enough_packets(is->subtitle_st, is->subtitle_stream, &is->subtitleq)))) {
            /* wait 10 ms */
            // 如果包队列都满了, 则不再读取, 这里等待继续读取的条件, 但是该等待有10ms的超时
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        }
        // 如果未处于暂停状态, 并且队列中未渲染的帧数为0, 并且loop不为1, 则意味着要循环播放
        // 还有一个情况是, 播放完成了, 不循环, 也不自动退出, 而帧队列中有没有任何数据可渲染的,
        // 则认为是播放结束, 将一直渲染在最后一帧上
        if (!is->paused &&
            (!is->audio_st || (is->auddec.finished == is->audioq.serial && frame_queue_nb_remaining(&is->sampq) == 0)) &&
            (!is->video_st || (is->viddec.finished == is->videoq.serial && frame_queue_nb_remaining(&is->pictq) == 0))) {
            if (loop != 1 && (!loop || --loop)) {
                // seek 到流的开始位置
                stream_seek(is, start_time != AV_NOPTS_VALUE ? start_time : 0, 0, 0);
            } else if (autoexit) {
                // 如果播放完成后自动推出, 则直接退出
                ret = AVERROR_EOF;
                goto fail;
            }
        }
        // 执行真正的读取操作, 此时才实际的读取一帧, 此时就算状态是暂停, 依然读取
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            // 如果刚好读到流的结尾, 则向响应的 PacketQueue 中插入空包
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof) {
                if (is->video_stream >= 0)
                    packet_queue_put_nullpacket(&is->videoq, pkt, is->video_stream);
                if (is->audio_stream >= 0)
                    packet_queue_put_nullpacket(&is->audioq, pkt, is->audio_stream);
                if (is->subtitle_stream >= 0)
                    packet_queue_put_nullpacket(&is->subtitleq, pkt, is->subtitle_stream);
                // 设置 EOF 标志为1, 表示读到末尾
                is->eof = 1;
            }
            // 如果I/O上下文有效, 检查读取失败是否由I/O导致
            if (ic->pb && ic->pb->error) {
                if (autoexit)
                    goto fail;
                else
                    break;
            }
            // 等待允许继续读取的条件, 该等待有10ms的超时
            // 这里如果是正常播放完成, 则下次循环要看loop是否不为1, 在定是否 seek 到开始从头播放
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        } else {
            is->eof = 0;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = ic->streams[pkt->stream_index]->start_time;

        // 对于包的时间戳, 如果pts无效, 则认为是包的dts
        // 如果pts有效, 那就用包自己的pts
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        // 如果帧的PTS是在有效时长内
        pkt_in_play_range = duration == AV_NOPTS_VALUE ||
                (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                av_q2d(ic->streams[pkt->stream_index]->time_base) -
                (double)(start_time != AV_NOPTS_VALUE ? start_time : 0) / 1000000
                <= ((double)duration / 1000000);
        
        if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
            // 如果包是音频流的(索引为音频流索引), PTS合法, 则插入到音频包队列当中
            packet_queue_put(&is->audioq, pkt);
        } else if (pkt->stream_index == is->video_stream && pkt_in_play_range
                   && !(is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            // 如果包是视频流的(索引为视频流索引), PTS合法, 则插入到视频包队列当中
            packet_queue_put(&is->videoq, pkt);
        } else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
            // 字幕也同样处理
            packet_queue_put(&is->subtitleq, pkt);
        } else {
            av_packet_unref(pkt);
        }
    }

    ret = 0;
 fail:
    // 播放结束, 关闭播放输出
    if (ic && !is->ic)
        avformat_close_input(&ic);

    // 释放包引用
    av_packet_free(&pkt);
    // 如果返回不为0, 要产生一个SDL的退出消息
    if (ret != 0) {
        SDL_Event event;

        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    // 释放等待线程的等待锁, 读取线程此时彻底退出
    SDL_DestroyMutex(wait_mutex);
    return 0;
}

static VideoState *stream_open(const char *filename, AVInputFormat *iformat)
{
    VideoState *is;

    // 分配状态结构体
    is = av_mallocz(sizeof(VideoState));
    // 分配失败则返回
    if (!is)
        return NULL;
    // 初始化流的索引
    is->last_video_stream = is->video_stream = -1;
    is->last_audio_stream = is->audio_stream = -1;
    is->last_subtitle_stream = is->subtitle_stream = -1;

    // 记录打开的文件路径
    is->filename = av_strdup(filename);
    // 检查文件路径不能为空
    if (!is->filename)
        goto fail;

    // 设置输入结构, 如果通过 -f 指出格式, iformat有值, 在opt_format()中创建
    is->iformat = iformat;
    // 设置窗口的坐标, 默认为(0, 0)
    is->ytop    = 0;
    is->xleft   = 0;

    /* start video display */
    // 初始化: 图像/字幕/音频的帧队列
    if (frame_queue_init(&is->pictq, &is->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        goto fail;
    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;

    // 初始化包队列
    if (packet_queue_init(&is->videoq) < 0 ||
        packet_queue_init(&is->audioq) < 0 ||
        packet_queue_init(&is->subtitleq) < 0)
        goto fail;

    if (!(is->continue_read_thread = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        goto fail;
    }

    /* start video display */
    // 初始化: 图像/字幕/音频的帧队列
    if (frame_queue_init(&is->pictq, &is->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        goto fail;
    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;

    // 初始化包队列
    if (packet_queue_init(&is->videoq) < 0 ||
        packet_queue_init(&is->audioq) < 0 ||
        packet_queue_init(&is->subtitleq) < 0)
        goto fail;

    if (!(is->continue_read_thread = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        goto fail;
    }

    // 初始化视频/音频/外部时钟结构体
    init_clock(&is->vidclk, &is->videoq.serial);
    init_clock(&is->audclk, &is->audioq.serial);
    init_clock(&is->extclk, &is->extclk.serial);

    // 设置音频时钟序列
    is->audio_clock_serial = -1;

    // 检查起始音量设置, 如果小于0, 打印警告并设置为0
    if (startup_volume < 0)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", startup_volume);
    // 如果起始音量大于100, 打印警告并设置为100.
    if (startup_volume > 100)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", startup_volume);
    // 限定音量大小到1~100的范围, 这里有必要?
    startup_volume = av_clip(startup_volume, 0, 100);
    // 将音量clip到SDL的音量范围, 从0~100 clip 到 0~128
    startup_volume = av_clip(SDL_MIX_MAXVOLUME * startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
    // 保存 clip() 过的 SDL 音量
    is->audio_volume = startup_volume;
    // 默认设置不静音
    is->muted = 0;
    // 设置音视频同步类型
    is->av_sync_type = av_sync_type;
    // 创建读取线程, 注意线程创建后将立刻启动, 因此此调用后请异步至 read_thread()
    // 注意, VideoState 将作为参数传递个线程
    is->read_tid     = SDL_CreateThread(read_thread, "read_thread", is);
    // 如果SDL线程创建失败了, 则打印错误撤销所有前面的初始化并返回空
    if (!is->read_tid) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
fail:
        // 会完成对 VideoState 的析构
        stream_close(is);
        return NULL;
    }
    return is;
}

static void stream_cycle_channel(VideoState *is, int codec_type)
{
    AVFormatContext *ic = is->ic;
    int start_index, stream_index;
    int old_index;
    AVStream *st;
    AVProgram *p = NULL;
    int nb_streams = is->ic->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO) {
        start_index = is->last_video_stream;
        old_index = is->video_stream;
    } else if (codec_type == AVMEDIA_TYPE_AUDIO) {
        start_index = is->last_audio_stream;
        old_index = is->audio_stream;
    } else {
        start_index = is->last_subtitle_stream;
        old_index = is->subtitle_stream;
    }
    stream_index = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && is->video_stream != -1) {
        p = av_find_program_from_stream(ic, NULL, is->video_stream);
        if (p) {
            nb_streams = p->nb_stream_indexes;
            for (start_index = 0; start_index < nb_streams; start_index++)
                if (p->stream_index[start_index] == stream_index)
                    break;
            if (start_index == nb_streams)
                start_index = -1;
            stream_index = start_index;
        }
    }

    for (;;) {
        if (++stream_index >= nb_streams)
        {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                stream_index = -1;
                is->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = is->ic->streams[p ? p->stream_index[stream_index] : stream_index];
        if (st->codecpar->codec_type == codec_type) {
            /* check that parameters are OK */
            switch (codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (st->codecpar->sample_rate != 0 &&
                    st->codecpar->channels != 0)
                    goto the_end;
                break;
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_SUBTITLE:
                goto the_end;
            default:
                break;
            }
        }
    }
 the_end:
    if (p && stream_index != -1)
        stream_index = p->stream_index[stream_index];
    av_log(NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
           av_get_media_type_string(codec_type),
           old_index,
           stream_index);

    stream_component_close(is, old_index);
    stream_component_open(is, stream_index);
}


static void toggle_full_screen(VideoState *is)
{
    is_full_screen = !is_full_screen;
    SDL_SetWindowFullscreen(window, is_full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void toggle_audio_display(VideoState *is)
{
    int next = is->show_mode;
    do {
        next = (next + 1) % SHOW_MODE_NB;
    } while (next != is->show_mode && (next == SHOW_MODE_VIDEO && !is->video_st || next != SHOW_MODE_VIDEO && !is->audio_st));
    if (is->show_mode != next) {
        is->force_refresh = 1;
        is->show_mode = next;
    }
}

static void refresh_loop_wait_event(VideoState *is, SDL_Event *event) {
    double remaining_time = 0.0;
    // 抽出事件循环，从输入设备收集事件
    SDL_PumpEvents();
    // 检查事件队列中是否有消息，并有选择地返回它们
    // 如果有消息返回, 则while失败, 直接返回, 表示收到1个有效的消息, 或者出错
    // 如果没有消息返回, 则判定是否执行渲染
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        if (!cursor_hidden && av_gettime_relative() - cursor_last_shown > CURSOR_HIDE_DELAY) {
            SDL_ShowCursor(0);
            cursor_hidden = 1;
        }
        // 如果 remaining_time 是正数, 则延迟给定的时间
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        // 设置等待时间为刷新率, 这里假设是100Hz
        remaining_time = REFRESH_RATE;
        // 如果刷新模式不是"无", 并且不是暂停状态或者强制刷新状态, 则进行视频渲染
        if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh))
            // 视频帧渲染, 这里的视频帧不仅指视频流的内容也有DFT的情况
            video_refresh(is, &remaining_time);
        SDL_PumpEvents();
        // 抽出事件循环，从输入设备收集事件
    }
}

static void seek_chapter(VideoState *is, int incr)
{
    int64_t pos = get_master_clock(is) * AV_TIME_BASE;
    int i;

    if (!is->ic->nb_chapters)
        return;

    /* find the current chapter */
    for (i = 0; i < is->ic->nb_chapters; i++) {
        AVChapter *ch = is->ic->chapters[i];
        if (av_compare_ts(pos, AV_TIME_BASE_Q, ch->start, ch->time_base) < 0) {
            i--;
            break;
        }
    }

    i += incr;
    i = FFMAX(i, 0);
    if (i >= is->ic->nb_chapters)
        return;

    av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
    stream_seek(is, av_rescale_q(is->ic->chapters[i]->start, is->ic->chapters[i]->time_base,
                                 AV_TIME_BASE_Q), 0, 0);
}

/* handle an event sent by the GUI */
// 事件循环负责处理视频渲染, 调用 video_refresh()
static void event_loop(VideoState *cur_stream)
{
    SDL_Event event;
    double incr, pos, frac;

    for (;;) {
        double x;
        refresh_loop_wait_event(cur_stream, &event);
        switch (event.type) {
        case SDL_KEYDOWN:
            if (exit_on_keydown || event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                do_exit(cur_stream);
                break;
            }
            // If we don't yet have a window, skip all key events, because read_thread might still be initializing...
            if (!cur_stream->width)
                continue;
            switch (event.key.keysym.sym) {
            case SDLK_f:
                toggle_full_screen(cur_stream);
                cur_stream->force_refresh = 1;
                break;
            case SDLK_p:
            case SDLK_SPACE:
                toggle_pause(cur_stream);
                break;
            case SDLK_m:
                toggle_mute(cur_stream);
                break;
            case SDLK_KP_MULTIPLY:
            case SDLK_0:
                update_volume(cur_stream, 1, SDL_VOLUME_STEP);
                break;
            case SDLK_KP_DIVIDE:
            case SDLK_9:
                update_volume(cur_stream, -1, SDL_VOLUME_STEP);
                break;
            case SDLK_s: // S: Step to next frame
                step_to_next_frame(cur_stream);
                break;
            case SDLK_a:
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
                break;
            case SDLK_v:
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
                break;
            case SDLK_c:
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_t:
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_w:
#if CONFIG_AVFILTER
                if (cur_stream->show_mode == SHOW_MODE_VIDEO && cur_stream->vfilter_idx < nb_vfilters - 1) {
                    if (++cur_stream->vfilter_idx >= nb_vfilters)
                        cur_stream->vfilter_idx = 0;
                } else {
                    cur_stream->vfilter_idx = 0;
                    toggle_audio_display(cur_stream);
                }
#else
                toggle_audio_display(cur_stream);
#endif
                break;
            case SDLK_PAGEUP:
                if (cur_stream->ic->nb_chapters <= 1) {
                    incr = 600.0;
                    goto do_seek;
                }
                seek_chapter(cur_stream, 1);
                break;
            case SDLK_PAGEDOWN:
                if (cur_stream->ic->nb_chapters <= 1) {
                    incr = -600.0;
                    goto do_seek;
                }
                seek_chapter(cur_stream, -1);
                break;
            case SDLK_LEFT:
                incr = seek_interval ? -seek_interval : -10.0;
                goto do_seek;
            case SDLK_RIGHT:
                incr = seek_interval ? seek_interval : 10.0;
                goto do_seek;
            case SDLK_UP:
                incr = 60.0;
                goto do_seek;
            case SDLK_DOWN:
                incr = -60.0;
            do_seek:
                    if (seek_by_bytes) {
                        pos = -1;
                        if (pos < 0 && cur_stream->video_stream >= 0)
                            pos = frame_queue_last_pos(&cur_stream->pictq);
                        if (pos < 0 && cur_stream->audio_stream >= 0)
                            pos = frame_queue_last_pos(&cur_stream->sampq);
                        if (pos < 0)
                            pos = avio_tell(cur_stream->ic->pb);
                        if (cur_stream->ic->bit_rate)
                            incr *= cur_stream->ic->bit_rate / 8.0;
                        else
                            incr *= 180000.0;
                        pos += incr;
                        stream_seek(cur_stream, pos, incr, 1);
                    } else {
                        pos = get_master_clock(cur_stream);
                        if (isnan(pos))
                            pos = (double)cur_stream->seek_pos / AV_TIME_BASE;
                        pos += incr;
                        if (cur_stream->ic->start_time != AV_NOPTS_VALUE && pos < cur_stream->ic->start_time / (double)AV_TIME_BASE)
                            pos = cur_stream->ic->start_time / (double)AV_TIME_BASE;
                        stream_seek(cur_stream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
                    }
                break;
            default:
                break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (exit_on_mousedown) {
                do_exit(cur_stream);
                break;
            }
            if (event.button.button == SDL_BUTTON_LEFT) {
                static int64_t last_mouse_left_click = 0;
                if (av_gettime_relative() - last_mouse_left_click <= 500000) {
                    toggle_full_screen(cur_stream);
                    cur_stream->force_refresh = 1;
                    last_mouse_left_click = 0;
                } else {
                    last_mouse_left_click = av_gettime_relative();
                }
            }
        case SDL_MOUSEMOTION:
            if (cursor_hidden) {
                SDL_ShowCursor(1);
                cursor_hidden = 0;
            }
            cursor_last_shown = av_gettime_relative();
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button != SDL_BUTTON_RIGHT)
                    break;
                x = event.button.x;
            } else {
                if (!(event.motion.state & SDL_BUTTON_RMASK))
                    break;
                x = event.motion.x;
            }
                if (seek_by_bytes || cur_stream->ic->duration <= 0) {
                    uint64_t size =  avio_size(cur_stream->ic->pb);
                    stream_seek(cur_stream, size*x/cur_stream->width, 0, 1);
                } else {
                    int64_t ts;
                    int ns, hh, mm, ss;
                    int tns, thh, tmm, tss;
                    tns  = cur_stream->ic->duration / 1000000LL;
                    thh  = tns / 3600;
                    tmm  = (tns % 3600) / 60;
                    tss  = (tns % 60);
                    frac = x / cur_stream->width;
                    ns   = frac * tns;
                    hh   = ns / 3600;
                    mm   = (ns % 3600) / 60;
                    ss   = (ns % 60);
                    av_log(NULL, AV_LOG_INFO,
                           "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac*100,
                            hh, mm, ss, thh, tmm, tss);
                    ts = frac * cur_stream->ic->duration;
                    if (cur_stream->ic->start_time != AV_NOPTS_VALUE)
                        ts += cur_stream->ic->start_time;
                    stream_seek(cur_stream, ts, 0, 0);
                }
            break;
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    screen_width  = cur_stream->width  = event.window.data1;
                    screen_height = cur_stream->height = event.window.data2;
                    if (cur_stream->vis_texture) {
                        SDL_DestroyTexture(cur_stream->vis_texture);
                        cur_stream->vis_texture = NULL;
                    }
                case SDL_WINDOWEVENT_EXPOSED:
                    cur_stream->force_refresh = 1;
            }
            break;
        case SDL_QUIT:
        case FF_QUIT_EVENT:
            do_exit(cur_stream);
            break;
        default:
            break;
        }
    }
}

static int opt_frame_size(void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "Option -s is deprecated, use -video_size.\n");
    return opt_default(NULL, "video_size", arg);
}

static int opt_width(void *optctx, const char *opt, const char *arg)
{
    screen_width = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

static int opt_height(void *optctx, const char *opt, const char *arg)
{
    screen_height = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

// 如果用户设置了 -f 参数, 制定了输入格式, 则查找格式对应的格式描述: AVInputFormat 
static int opt_format(void *optctx, const char *opt, const char *arg)
{
    file_iformat = av_find_input_format(arg);
    // 如果无法找到, 则发出警告, 表示用户指定的格式无法识别, 如果用户没指定, 本函数不执行也就不用警告了.
    if (!file_iformat) {
        av_log(NULL, AV_LOG_FATAL, "Unknown input format: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_frame_pix_fmt(void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "Option -pix_fmt is deprecated, use -pixel_format.\n");
    return opt_default(NULL, "pixel_format", arg);
}

static int opt_sync(void *optctx, const char *opt, const char *arg)
{
    if (!strcmp(arg, "audio"))
        av_sync_type = AV_SYNC_AUDIO_MASTER;
    else if (!strcmp(arg, "video"))
        av_sync_type = AV_SYNC_VIDEO_MASTER;
    else if (!strcmp(arg, "ext"))
        av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
    else {
        av_log(NULL, AV_LOG_ERROR, "Unknown value for %s: %s\n", opt, arg);
        exit(1);
    }
    return 0;
}

static int opt_seek(void *optctx, const char *opt, const char *arg)
{
    start_time = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_duration(void *optctx, const char *opt, const char *arg)
{
    duration = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_show_mode(void *optctx, const char *opt, const char *arg)
{
    show_mode = !strcmp(arg, "video") ? SHOW_MODE_VIDEO :
                !strcmp(arg, "waves") ? SHOW_MODE_WAVES :
                !strcmp(arg, "rdft" ) ? SHOW_MODE_RDFT  :
                parse_number_or_die(opt, arg, OPT_INT, 0, SHOW_MODE_NB-1);
    return 0;
}

static void opt_input_file(void *optctx, const char *filename)
{
    if (input_filename) {
        av_log(NULL, AV_LOG_FATAL,
               "Argument '%s' provided as input filename, but '%s' was already specified.\n",
                filename, input_filename);
        exit(1);
    }
    if (!strcmp(filename, "-"))
        filename = "pipe:";
    input_filename = filename;
}

static int opt_codec(void *optctx, const char *opt, const char *arg)
{
   const char *spec = strchr(opt, ':');
   if (!spec) {
       av_log(NULL, AV_LOG_ERROR,
              "No media specifier was specified in '%s' in option '%s'\n",
               arg, opt);
       return AVERROR(EINVAL);
   }
   spec++;
   switch (spec[0]) {
   case 'a' :    audio_codec_name = arg; break;
   case 's' : subtitle_codec_name = arg; break;
   case 'v' :    video_codec_name = arg; break;
   default:
       av_log(NULL, AV_LOG_ERROR,
              "Invalid media specifier '%s' in option '%s'\n", spec, opt);
       return AVERROR(EINVAL);
   }
   return 0;
}

static int dummy;

static const OptionDef options[] = {
    CMDUTILS_COMMON_OPTIONS
    { "x", HAS_ARG, { .func_arg = opt_width }, "force displayed width", "width" },
    { "y", HAS_ARG, { .func_arg = opt_height }, "force displayed height", "height" },
    { "s", HAS_ARG | OPT_VIDEO, { .func_arg = opt_frame_size }, "set frame size (WxH or abbreviation)", "size" },
    { "fs", OPT_BOOL, { &is_full_screen }, "force full screen" },
    { "an", OPT_BOOL, { &audio_disable }, "disable audio" },
    { "vn", OPT_BOOL, { &video_disable }, "disable video" },
    { "sn", OPT_BOOL, { &subtitle_disable }, "disable subtitling" },
    { "ast", OPT_STRING | HAS_ARG | OPT_EXPERT, { &wanted_stream_spec[AVMEDIA_TYPE_AUDIO] }, "select desired audio stream", "stream_specifier" },
    { "vst", OPT_STRING | HAS_ARG | OPT_EXPERT, { &wanted_stream_spec[AVMEDIA_TYPE_VIDEO] }, "select desired video stream", "stream_specifier" },
    { "sst", OPT_STRING | HAS_ARG | OPT_EXPERT, { &wanted_stream_spec[AVMEDIA_TYPE_SUBTITLE] }, "select desired subtitle stream", "stream_specifier" },
    { "ss", HAS_ARG, { .func_arg = opt_seek }, "seek to a given position in seconds", "pos" },
    { "t", HAS_ARG, { .func_arg = opt_duration }, "play  \"duration\" seconds of audio/video", "duration" },
    { "bytes", OPT_INT | HAS_ARG, { &seek_by_bytes }, "seek by bytes 0=off 1=on -1=auto", "val" },
    { "seek_interval", OPT_FLOAT | HAS_ARG, { &seek_interval }, "set seek interval for left/right keys, in seconds", "seconds" },
    { "nodisp", OPT_BOOL, { &display_disable }, "disable graphical display" },
    { "noborder", OPT_BOOL, { &borderless }, "borderless window" },
    { "alwaysontop", OPT_BOOL, { &alwaysontop }, "window always on top" },
    { "volume", OPT_INT | HAS_ARG, { &startup_volume}, "set startup volume 0=min 100=max", "volume" },
    { "f", HAS_ARG, { .func_arg = opt_format }, "force format", "fmt" },
    { "pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO, { .func_arg = opt_frame_pix_fmt }, "set pixel format", "format" },
    { "stats", OPT_BOOL | OPT_EXPERT, { &show_status }, "show status", "" },
    { "fast", OPT_BOOL | OPT_EXPERT, { &fast }, "non spec compliant optimizations", "" },
    { "genpts", OPT_BOOL | OPT_EXPERT, { &genpts }, "generate pts", "" },
    { "drp", OPT_INT | HAS_ARG | OPT_EXPERT, { &decoder_reorder_pts }, "let decoder reorder pts 0=off 1=on -1=auto", ""},
    { "lowres", OPT_INT | HAS_ARG | OPT_EXPERT, { &lowres }, "", "" },
    { "sync", HAS_ARG | OPT_EXPERT, { .func_arg = opt_sync }, "set audio-video sync. type (type=audio/video/ext)", "type" },
    { "autoexit", OPT_BOOL | OPT_EXPERT, { &autoexit }, "exit at the end", "" },
    { "exitonkeydown", OPT_BOOL | OPT_EXPERT, { &exit_on_keydown }, "exit on key down", "" },
    { "exitonmousedown", OPT_BOOL | OPT_EXPERT, { &exit_on_mousedown }, "exit on mouse down", "" },
    { "loop", OPT_INT | HAS_ARG | OPT_EXPERT, { &loop }, "set number of times the playback shall be looped", "loop count" },
    { "framedrop", OPT_BOOL | OPT_EXPERT, { &framedrop }, "drop frames when cpu is too slow", "" },
    { "infbuf", OPT_BOOL | OPT_EXPERT, { &infinite_buffer }, "don't limit the input buffer size (useful with realtime streams)", "" },
    { "window_title", OPT_STRING | HAS_ARG, { &window_title }, "set window title", "window title" },
    { "left", OPT_INT | HAS_ARG | OPT_EXPERT, { &screen_left }, "set the x position for the left of the window", "x pos" },
    { "top", OPT_INT | HAS_ARG | OPT_EXPERT, { &screen_top }, "set the y position for the top of the window", "y pos" },
#if CONFIG_AVFILTER
    { "vf", OPT_EXPERT | HAS_ARG, { .func_arg = opt_add_vfilter }, "set video filters", "filter_graph" },
    { "af", OPT_STRING | HAS_ARG, { &afilters }, "set audio filters", "filter_graph" },
#endif
    { "rdftspeed", OPT_INT | HAS_ARG| OPT_AUDIO | OPT_EXPERT, { &rdftspeed }, "rdft speed", "msecs" },
    { "showmode", HAS_ARG, { .func_arg = opt_show_mode}, "select show mode (0 = video, 1 = waves, 2 = RDFT)", "mode" },
    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, { .func_arg = opt_default }, "generic catch all option", "" },
    { "i", OPT_BOOL, { &dummy}, "read specified file", "input_file"},
    { "codec", HAS_ARG, { .func_arg = opt_codec}, "force decoder", "decoder_name" },
    { "acodec", HAS_ARG | OPT_STRING | OPT_EXPERT, {    &audio_codec_name }, "force audio decoder",    "decoder_name" },
    { "scodec", HAS_ARG | OPT_STRING | OPT_EXPERT, { &subtitle_codec_name }, "force subtitle decoder", "decoder_name" },
    { "vcodec", HAS_ARG | OPT_STRING | OPT_EXPERT, {    &video_codec_name }, "force video decoder",    "decoder_name" },
    { "autorotate", OPT_BOOL, { &autorotate }, "automatically rotate video", "" },
    { "find_stream_info", OPT_BOOL | OPT_INPUT | OPT_EXPERT, { &find_stream_info },
        "read and decode the streams to fill missing information with heuristics" },
    { "filter_threads", HAS_ARG | OPT_INT | OPT_EXPERT, { &filter_nbthreads }, "number of filter threads per graph" },
    { NULL, },
};

static void show_usage(void)
{
    av_log(NULL, AV_LOG_INFO, "Simple media player\n");
    av_log(NULL, AV_LOG_INFO, "usage: %s [options] input_file\n", program_name);
    av_log(NULL, AV_LOG_INFO, "\n");
}

void show_help_default(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:", 0, OPT_EXPERT, 0);
    show_help_options(options, "Advanced options:", OPT_EXPERT, 0, 0);
    printf("\n");
    show_help_children(avcodec_get_class(), AV_OPT_FLAG_DECODING_PARAM);
    show_help_children(avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);
#if !CONFIG_AVFILTER
    show_help_children(sws_get_class(), AV_OPT_FLAG_ENCODING_PARAM);
#else
    show_help_children(avfilter_get_class(), AV_OPT_FLAG_FILTERING_PARAM);
#endif
    printf("\nWhile playing:\n"
           "q, ESC              quit\n"
           "f                   toggle full screen\n"
           "p, SPC              pause\n"
           "m                   toggle mute\n"
           "9, 0                decrease and increase volume respectively\n"
           "/, *                decrease and increase volume respectively\n"
           "a                   cycle audio channel in the current program\n"
           "v                   cycle video channel\n"
           "t                   cycle subtitle channel in the current program\n"
           "c                   cycle program\n"
           "w                   cycle video filters or show modes\n"
           "s                   activate frame-step mode\n"
           "left/right          seek backward/forward 10 seconds or to custom interval if -seek_interval is set\n"
           "down/up             seek backward/forward 1 minute\n"
           "page down/page up   seek backward/forward 10 minutes\n"
           "right mouse click   seek to percentage in file corresponding to fraction of width\n"
           "left double-click   toggle full screen\n"
           );
}

/* Called from the main */
int main(int argc, char **argv)
{
    // 用于设置旗语的临时变量
    int flags;
    // 播放状态, ffplay 整体的状态存储
    VideoState *is;

    // 仅Windows用
    init_dynload();

    // 设置LOG的旗语, 跳过重复的log输出
    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    // 解析log的级别, 参数: -v
    parse_loglevel(argc, argv, options);

    /* register all codecs, demux and protocols */
    // 注册所有的设备, 以前的 av_register_all()包含了此步骤.
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    // 注册所有的网络模块
    avformat_network_init();

    // 设置swscale的采样方式为: 双三次插值
    // https://en.wikipedia.org/wiki/Bicubic_interpolation
    init_opts();

    // 捕捉Interrupt/Termination两个信号, 但不处理, 仅返回123
    signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).    */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */

    // 打印banner
    show_banner(argc, argv, options);

    // 解析参数
    parse_options(NULL, argc, argv, options, opt_input_file);

    // 检查收入文件的名字, 如果是空, 那么直接打印帮助信息
    if (!input_filename) {
        show_usage();
        av_log(NULL, AV_LOG_FATAL, "An input file must be specified\n");
        av_log(NULL, AV_LOG_FATAL,
               "Use -h to get full help or, even better, run 'man %s'\n", program_name);
        exit(1);
    }

    // 如果是不允许显示, 则设置 video_disable 为 1
    if (display_disable) {
        video_disable = 1;
    }

    // 设置SDL的旗语, 对于回放, 一般需要的是 VIDEO, AUDIO 和一个额外的 TIMER
    flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (audio_disable)
        flags &= ~SDL_INIT_AUDIO;
    else {
        // 获取 AUDIO 的缓冲区大小配置(如果有)
        /* Try to work around an occasional ALSA buffer underflow issue when the
         * period size is NPOT due to ALSA resampling by forcing the buffer size. */
        if (!SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
            SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE","1", 1);
    }
    // 如果显示是禁止的, 没有必要设置SDL支持VIDEO, 因此从旗语当中去掉对应的标志
    if (display_disable)
        flags &= ~SDL_INIT_VIDEO;

    // 初始化SDL, 这是必要的调用
    if (SDL_Init (flags)) {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        exit(1);
    }

    // 初设置 SDL 事件的过滤配置
    // 参考: http://wiki.libsdl.org/SDL_EventState
    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

    // 如果显示部分没有被禁用, 则初始化一个SDL窗口
    if (!display_disable) {
        // 首先设置窗体模式为隐藏
        int flags = SDL_WINDOW_HIDDEN;
        if (alwaysontop)
#if SDL_VERSION_ATLEAST(2,0,5)
            flags |= SDL_WINDOW_ALWAYS_ON_TOP;
#else
            av_log(NULL, AV_LOG_WARNING, "Your SDL version doesn't support SDL_WINDOW_ALWAYS_ON_TOP. Feature will be inactive.\n");
#endif
        // 默认该变量为0, 除非主动通过 -noborder 参数设置为1
        if (borderless)
            flags |= SDL_WINDOW_BORDERLESS;
        else
            // 如果不设置无边框, 则允许调整大小
            flags |= SDL_WINDOW_RESIZABLE;
        // 创建SDL窗口
        window = SDL_CreateWindow(program_name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, default_width, default_height, flags);
        
        // 设置渲染方式为线性渲染
        // "0" or "nearest"
        // "1" or "linear"
        // "2" or "best"
        // https://wiki.libsdl.org/SDL_SetHint
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        if (window) {
            // 创建一个2D渲染器
            // 例程: https://wiki.libsdl.org/SDL_CreateRenderer
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!renderer) {
                av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
                renderer = SDL_CreateRenderer(window, -1, 0);
            }
            // 如果2D渲染器创建成功, 则获取渲染其信息
            if (renderer) {
                if (!SDL_GetRendererInfo(renderer, &renderer_info))
                    av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", renderer_info.name);
            }
        }
        if (!window || !renderer || !renderer_info.num_texture_formats) {
            av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
            do_exit(NULL);
        }
    }

    // 打开输入流
    is = stream_open(input_filename, file_iformat);
    if (!is) {
        av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
        do_exit(NULL);
    }

    // 进入SDL事件循环
    event_loop(is);

    /* never returns */

    return 0;
}
