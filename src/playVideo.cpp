#include <iostream>
#define SDL_MAIN_HANDLED 

//Refresh Event
#define SFM_REFRESH_EVENT     (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT       (SDL_USEREVENT + 2)

extern "C" {
#include <libavformat/avformat.h>   
#include <libavcodec/avcodec.h>   
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <SDL.h>
}

using std::cout;
using std::endl;

namespace video {
    int thread_exit = 0;
    int thread_pause = 0;

    int sfp_refresh_thread(void* opaque) {
        while (!thread_exit) {
            if (!thread_pause) {
                SDL_Event event;
                event.type = SFM_REFRESH_EVENT;
                SDL_PushEvent(&event);
            }
            SDL_Delay(40);
        }
        thread_exit = 0;
        thread_pause = 0;
        //Break
        SDL_Event event;
        event.type = SFM_BREAK_EVENT;
        SDL_PushEvent(&event);
        return 0;
    }

    int playVideo(const char* filePath) {
        AVFormatContext* pFmtCtx = NULL;
        AVCodecContext* pCodecCtx = NULL;
        AVFrame* pFrame = NULL;
        AVFrame* pFrameYUV = NULL;
        uint8_t* outBuffer = NULL;
        AVPacket* pPacket = NULL;
        SwsContext* pSwsCtx = NULL;

        //SDL
        int screen_w, screen_h;
        SDL_Window* screen = NULL;
        SDL_Renderer* sdlRenderer = NULL;
        SDL_Texture* sdlTexture = NULL;
        SDL_Rect sdlRect;
        SDL_Thread* video_tid = NULL;
        SDL_Event event;

        av_register_all();
        avformat_network_init();

        pFmtCtx = avformat_alloc_context();

        if (avformat_open_input(&pFmtCtx, filePath, NULL, NULL) != 0) {
            cout << "Couldn't open input stream." << endl;
            return -1;
        }

        if (avformat_find_stream_info(pFmtCtx, NULL) < 0) {
            cout << "Couldn't find stream information." << endl;
            return -1;
        }

        int i = 0, videoIndex = -1;
        videoIndex = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if (videoIndex == -1) {
            cout << "no video stream" << endl;
            return -1;
        }

        pCodecCtx = avcodec_alloc_context3(NULL);
        if (avcodec_parameters_to_context(pCodecCtx, pFmtCtx->streams[videoIndex]->codecpar) < 0) {
            cout << "no parameters to contex." << endl;
            return -1;
        }
        AVCodec* pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
        if (pCodec == NULL) {
            cout << "no codec" << endl;
            return -1;
        }
        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {//打开解码器
            cout << "Could not open codec." << endl;
            return -1;
        }

        pFrame = av_frame_alloc();
        pFrameYUV = av_frame_alloc();

        outBuffer = (uint8_t*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1) * sizeof(uint8_t));
        av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, outBuffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

        pPacket = av_packet_alloc();

        av_dump_format(pFmtCtx, 0, filePath, 0);

        //获取SwsContext
        pSwsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
            pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, NULL, NULL, NULL, NULL);

        //  SDL初始化
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
            cout << "could not initialize sdl" << endl;
            return -1;
        }
        screen_w = pCodecCtx->width;
        screen_h = pCodecCtx->height;
        screen = SDL_CreateWindow("WS ffmpeg player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            pCodecCtx->width / 2, pCodecCtx->height / 2, SDL_WINDOW_OPENGL);
        if (!screen) {
            cout << "could not create window" << endl;
            return -1;
        }

        sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
        sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);

        sdlRect.x = 0;
        sdlRect.y = 0;
        sdlRect.w = screen_w;
        sdlRect.h = screen_h;

        video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);


        for (;;) {
            //Wait
            SDL_WaitEvent(&event);
            if (event.type == SFM_REFRESH_EVENT) {
                if (av_read_frame(pFmtCtx, pPacket) == 0) {
                    if (pPacket->stream_index == videoIndex) {
                        if (avcodec_send_packet(pCodecCtx, pPacket) != 0) {
                            cout << "decode fail" << endl;
                            break;
                        }
                        else {
                            avcodec_receive_frame(pCodecCtx, pFrame);
                            if (sws_scale(pSwsCtx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                                pFrameYUV->data, pFrameYUV->linesize) == 0) {
                                continue;
                            }

                            SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                            SDL_RenderClear(sdlRenderer);
                            SDL_RenderCopy(sdlRenderer, sdlTexture, &sdlRect, NULL);
                            SDL_RenderPresent(sdlRenderer);
                        }
                    }
                }
                else {
                    //退出线程
                    thread_exit = 1;
                    av_packet_unref(pPacket);
                }
            }
            else if (event.type == SDL_KEYDOWN) {
                cout << "Pause" << endl;
                if (event.key.keysym.sym == SDLK_SPACE)
                    thread_pause = !thread_pause;
            }
            else if (event.type == SDL_QUIT) {
                cout << "quit" << endl;
                thread_exit = 1;
                break;
            }
            else if (event.type == SFM_BREAK_EVENT) {
                cout << "break" << endl;
                break;
            }
        }

        SDL_Quit();

        sws_freeContext(pSwsCtx);
        av_free(outBuffer);
        av_frame_free(&pFrameYUV);
        av_frame_free(&pFrame);
        avcodec_close(pCodecCtx);
        avformat_close_input(&pFmtCtx);

    }

  
}
//int main() {
//    video::playVideo("D:\\videos\\超能陆战队1.ts");
//    return 1;
//}







