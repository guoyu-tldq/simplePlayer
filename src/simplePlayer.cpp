#include <stdio.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <iostream>
#define SDL_MAIN_HANDLED 

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <SDL.h>
};

#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT       (SDL_USEREVENT + 2)

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

using std::cout;
using std::endl;

int thread_exit = 0;
int thread_pause = 0;

int sfp_refresh_thread(void* opaque)
{
	while (!thread_exit) {
		if (!thread_pause) {
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		SDL_Delay(40);
	//	std::this_thread::sleep_for(std::chrono::milliseconds(inerval));
	}
	thread_exit = 0;
	thread_pause = 0;
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);
	return 0;
}

static  Uint8* audio_chunk;
static  Uint32  audio_len;
static  Uint8* audio_pos;

//音频播放回调函数
void  fill_audio(void* udata, Uint8* stream, int len) {
	SDL_memset(stream, 0, len);

	if (audio_len == 0)
		return;
	len = (len > audio_len ? audio_len : len);
	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}

int play(const char* file)
{
	int ret;
	//注册全部插件
	av_register_all();

	//分配内存
	AVFormatContext* fctx = avformat_alloc_context();

	//打开输入流
	AVFrame* f = av_frame_alloc();
	avformat_network_init();

	ret = avformat_open_input(&fctx, file, NULL, NULL);
	if (ret != 0) {
		cout << "Couldn't open input stream." << endl;
		return -1;
	}

	ret = avformat_find_stream_info(fctx, NULL);
	if (ret < 0) {
		cout << "Couldn't find stream info." << endl;
		return -1;
	}
	av_dump_format(fctx, 0, file, false);

	//查找视频流和音频流的编号
	int video_stream, audio_stream;
	video_stream = av_find_best_stream(fctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (video_stream == -1) {
		cout << "no video stream." << endl;
		return -1;
	}
	audio_stream = av_find_best_stream(fctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (audio_stream == -1) {
		cout << "no audio stream." << endl;
		return -1;
	}

	//获取解码上下文和解码器
	AVCodecContext* video_codec_ctx = fctx->streams[video_stream]->codec;
	AVCodec* video_codec = avcodec_find_decoder(video_codec_ctx->codec_id);;
	AVCodecContext* audio_codec_ctx = fctx->streams[audio_stream]->codec;
	AVCodec* audio_codec = avcodec_find_decoder(audio_codec_ctx->codec_id);
	if (avcodec_open2(video_codec_ctx, video_codec, NULL) < 0)
	{
		cout << "Could not open video codec." << endl;
		return -1;
	}
	if (avcodec_open2(audio_codec_ctx, audio_codec, NULL) < 0)
	{
		cout << "Could not open audio codec." << endl;
		return -1;
	}

	//auto frameRate = video_codec_ctx->framerate;
	//double fr = frameRate.num && frameRate.den ? av_q2d(frameRate) : 0.0;

	//printf("frameRate", fr);

	//初始化frame
	AVFrame* pFrame, * pFrameYUV, * audioFrame;
	AVPacket* packet;
	pFrame = av_frame_alloc();
	audioFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	av_init_packet(packet);
	uint8_t* outBuffer = NULL;
	outBuffer = (uint8_t*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, video_codec_ctx->width, video_codec_ctx->height, 1) * sizeof(uint8_t));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, outBuffer, AV_PIX_FMT_YUV420P, video_codec_ctx->width, video_codec_ctx->height, 1);

	//SDL
	int screen_w, screen_h;
	SDL_Window* screen = NULL;
	SDL_Renderer* sdlRenderer = NULL;
	SDL_Texture* sdlTexture = NULL;
	SDL_Rect sdlRect;
	SDL_Thread* video_tid = NULL;
	SDL_Event event;

	//初始化SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		cout << "Could not initialize SDL" << endl;
		return -1;
	}

	//初始化视频显示
	screen_w = video_codec_ctx->width;
	screen_h = video_codec_ctx->height;
	screen = SDL_CreateWindow("simplePlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		video_codec_ctx->width/4 *3 , video_codec_ctx->height / 4 * 3, SDL_WINDOW_OPENGL);
	if (!screen) {
		cout << "could not create window" << endl;
		return -1;
	}

	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, video_codec_ctx->width, video_codec_ctx->height);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;
	struct SwsContext* img_convert_ctx = sws_getContext(video_codec_ctx->width, video_codec_ctx->height, video_codec_ctx->pix_fmt, video_codec_ctx->width, video_codec_ctx->height, AV_PIX_FMT_YUV420P, NULL, NULL, NULL, NULL);

	//初始化音频参数
	uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
	int out_nb_samples = audio_codec_ctx->frame_size;
	AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
	int out_sample_rate = audio_codec_ctx->sample_rate;
	int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
	int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);
	uint8_t* out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);

	SDL_AudioSpec 	wanted_spec;
	wanted_spec.freq = out_sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = out_channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = out_nb_samples;
	wanted_spec.callback = fill_audio;
	wanted_spec.userdata = audio_codec_ctx;

	if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
		cout << "can't open audio." << endl;
		return -1;
	}

	int64_t in_channel_layout = av_get_default_channel_layout(audio_codec_ctx->channels);
	
	//Swr
	struct SwrContext* au_convert_ctx = swr_alloc();
	au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout, audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate, 0, NULL);
	swr_init(au_convert_ctx);

	//SDL线程
	video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
	//std::thread refreshThread( pic_refresh_thread, (int)(1000 / frameRate));
	
	int got;
	while (true) {
		SDL_WaitEvent(&event);
		if (event.type == SFM_REFRESH_EVENT) {
			SDL_PauseAudio(0);
			while (true) {
				if (av_read_frame(fctx, packet) >= 0) {
					if (packet->stream_index == video_stream) {
						ret = avcodec_decode_video2(video_codec_ctx, pFrame, &got, packet);
						if (ret < 0) {
							cout << "Decode Error." << endl;
							return -1;
						}
						if (got) {
							if (sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, video_codec_ctx->height,
								pFrameYUV->data, pFrameYUV->linesize) == 0) {
								continue;
							}
							SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
							SDL_RenderClear(sdlRenderer);
							SDL_RenderCopy(sdlRenderer, sdlTexture, &sdlRect, NULL);
							SDL_RenderPresent(sdlRenderer);
							break;
						}

						/*if (avcodec_send_packet(pCodecCtx, pPacket) != 0) {
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
						}*/
					}
					else if (packet->stream_index == audio_stream) {
						ret = avcodec_decode_audio4(audio_codec_ctx, audioFrame, &got, packet);
						if (got > 0) {
							swr_convert(au_convert_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t**)audioFrame->data, audioFrame->nb_samples); // 转换音频
							audio_chunk = (Uint8*)out_buffer;
							audio_len = out_buffer_size;
							audio_pos = audio_chunk;
							while (audio_len > 0) {
								SDL_Delay(1);
							}
						}
						//avcodec_send_packet(audio_codec_ctx, packet);
						//while (avcodec_receive_frame(audio_codec_ctx, pFrame) == 0) {
						//	swr_convert(au_convert_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t**)pFrame->data, pFrame->nb_samples); // 转换音频
						//}
						//audio_chunk = (Uint8*)out_buffer;
						//audio_len = out_buffer_size;
						//audio_pos = audio_chunk;
						//while (audio_len > 0) {
						//	SDL_Delay(1);
						//}
					}
					av_free_packet(packet);
				}
				else {
					//Exit Thread
					thread_exit = 1;
					break;
				}
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
//	refreshThread.join();

	SDL_CloseAudio();
	SDL_Quit();
	sws_freeContext(img_convert_ctx);
	av_free(pFrameYUV);
	av_free(out_buffer);
	swr_free(&au_convert_ctx);
	avcodec_close(video_codec_ctx);
	avcodec_close(audio_codec_ctx);
	avformat_close_input(&fctx);
	return 0;
}

//int main() {
//	play("D:\\videos\\超能陆战队1.ts");
//	return 0;
//}