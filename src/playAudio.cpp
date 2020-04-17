#include <iostream>
#define SDL_MAIN_HANDLED 

extern "C" {
#include "libswresample/swresample.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL.h>
}

using std::cout;
using std::endl;

static Uint8* audio_chunk;
static Uint32 audio_len;
static Uint8* audio_pos;

#define MAX_AUDIO_FRAME_SIZE 19200

//音频回调函数
void read_audio_data(void* udata, Uint8* stream, int len) {

    SDL_memset(stream, 0, len);
    if (audio_len == 0)
        return;
    len = (len > audio_len ? audio_len : len);

    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}

int playAudio(const char* file) {
    AVFormatContext* pFmtCtx = NULL;
    AVCodecContext* pCodecCtx = NULL;
    AVFrame* pFrame = NULL;
   // uint8_t* outBuffer = NULL;
    AVPacket* pPacket = NULL;
  
    uint8_t* out_buffer;
    int64_t in_channel_layout;
    struct SwrContext* au_convert_ctx;

    int i = 0, audioStream = -1;

    // 初始化
    av_register_all();
    avformat_network_init();
    // AVFormatContext获取
    pFmtCtx = avformat_alloc_context();
    // 打开文件
    if (avformat_open_input(&pFmtCtx, file, NULL, NULL) != 0) {
        cout << "Failed to open file" << endl;
        return -1;
    }
    // 获取文件信息
    if (avformat_find_stream_info(pFmtCtx, NULL) < 0) {
        cout << "Couldn't find stream information." << endl;
        return -1;
    }
    // 获取音频index
    audioStream = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    if (audioStream == -1) {
        cout << "no audio stream " << endl;
        return -1;
    }
    //获取解码器并打开
    pCodecCtx = avcodec_alloc_context3(NULL);
    if (avcodec_parameters_to_context(pCodecCtx, pFmtCtx->streams[audioStream]->codecpar) < 0) {
        return -1;
    }
    AVCodec* pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == NULL) {
        cout << "Failed to find decoder! " << endl;
        return -1;
    }
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        cout << "Failed to open decoder! " << endl;
        return -1;
    }

    pPacket = (AVPacket*)av_malloc(sizeof(AVPacket));
    av_init_packet(pPacket);
    pFrame = av_frame_alloc();

    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    int out_nb_samples = pCodecCtx->frame_size;
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    int out_sample_rate = pCodecCtx->sample_rate;;
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);

    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);
    out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);


    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        cout << "Could not initialize SDL" << endl;
        return -1;
    }

    SDL_AudioSpec spec;
    spec.freq = out_sample_rate;
    spec.format = AUDIO_S16SYS;
    spec.channels = out_channels;
    spec.silence = 0;
    spec.samples = out_nb_samples;
    spec.callback = read_audio_data;
    spec.userdata = pCodecCtx;

    if (SDL_OpenAudio(&spec, NULL) < 0) {
        cout << "can't open audio." << endl;
        return -1;
    }

    in_channel_layout = av_get_default_channel_layout(pCodecCtx->channels);
    au_convert_ctx = swr_alloc();
    au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate, in_channel_layout, pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);
    swr_init(au_convert_ctx);

    SDL_PauseAudio(0);

    while (av_read_frame(pFmtCtx, pPacket) >= 0) {
        if (pPacket->stream_index == audioStream) {
            avcodec_send_packet(pCodecCtx, pPacket);
            while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                swr_convert(au_convert_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t**)pFrame->data, pFrame->nb_samples); // 转换音频
            }

            audio_chunk = (Uint8*)out_buffer;
            audio_len = out_buffer_size;
            audio_pos = audio_chunk;

            while (audio_len > 0) {
                SDL_Delay(1);
            }
        }
        av_packet_unref(pPacket);
    }
    swr_free(&au_convert_ctx);
    SDL_Quit();
    return 0;
}

//int main() {
//    playAudio("D:\\videos\\Pets.ts");
//    return 1;
//}


