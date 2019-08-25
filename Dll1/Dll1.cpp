// Dll1.cpp: 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "Dll1.h"
#include <thread>
#include <mutex>

#pragma warning(disable: 4996)

using namespace std;

struct BufferContent
{
	uint8_t* m_pBuffer = NULL;
	WCHAR* m_pSubtitle = NULL;
	int m_nLength = 0;
	int m_nWidth = 0;
	int m_nHeight = 0;

	int m_nChannels = 0;
	int m_nSampleRate = 0;

	double m_dTime = 0;

	double m_dStartTime = 0;
	double m_dEndTime = 0;
};

int g_nVideoStream = -1;
int g_nAudioStream = -1;
int g_nSubtitleStream = -1;

AVRational g_VideoTimeBase;
AVRational g_AudioTimeBase;
AVRational g_SubtitleTimeBase;

mutex g_hPacketQueueMutex;
queue<AVPacket*> g_PacketQueue;

mutex g_hVideoQueueMutex;
queue<BufferContent> g_VideoDataQueue;

mutex g_hAudioQueueMutex;
queue<BufferContent> g_AudioDataQueue;

mutex g_hSubitleQueueMutex;
queue<BufferContent> g_SubtitleDataQueue;

bool g_bClose;
bool g_bOpenThreadClose;
bool g_bDecodeThreadClose;

int g_nPacketCount;

double g_dVideoLength;

AVCodecContext* g_pVideoCodecCtx;
AVCodecContext* g_pAudioCodecCtx;
AVCodecContext* g_pSubtitleCodecCtx;

extern"C" __declspec(dllexport) double GetVideoLength(int& hour, int& min, int& sec)
{
	int hours, mins, secs, us;
	double duration = g_dVideoLength + 5000;
	secs = duration / AV_TIME_BASE;
	mins = secs / 60;
	secs %= 60;
	hours = mins / 60;
	mins %= 60;

	hour = hours;
	min = mins;
	sec = secs;

	return g_dVideoLength;
}

extern"C" __declspec(dllexport) bool Init(int packet_count)
{
	av_register_all();
	avfilter_register_all();
	avformat_network_init();
	av_log_set_level(AV_LOG_WARNING);

	g_nPacketCount = packet_count;

	g_bClose = false;
	g_bOpenThreadClose = false;
	g_bDecodeThreadClose = false;

	g_pVideoCodecCtx = NULL;
	g_pAudioCodecCtx = NULL;
	g_pSubtitleCodecCtx = NULL;

	g_nVideoStream = -1;
	g_nAudioStream = -1;
	g_nSubtitleStream = -1;

	g_dVideoLength = 0;

	return true;
}


void Decode()
{
	// Allocate video frame.
	AVFrame* pFrame = av_frame_alloc();

	// Allocate an AVFrame structure.
	AVFrame* pFrameRGB = av_frame_alloc();

	AVFrame* pAudioFrame = av_frame_alloc();

	AVSubtitle* pSubtitle = (AVSubtitle*)av_malloc(sizeof(AVSubtitle));

	int nNumBytes;
	uint8_t *pBuffer = NULL;

	// Determine required buffer size and allocate buffer.
	nNumBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, g_pVideoCodecCtx->width, g_pVideoCodecCtx->height, 1);
	pBuffer = (uint8_t *)av_malloc(nNumBytes * sizeof(uint8_t));

	// Assign appropriate parts of buffer to image planes in pFrameRGB Note that pFrameRGB is an AVFrame, but AVFrame is a superset of AVPicture
	av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, pBuffer, AV_PIX_FMT_RGB24, g_pVideoCodecCtx->width, g_pVideoCodecCtx->height, 1);

	//AVPacket packet;
	struct SwsContext *sws_ctx = NULL;

	// Initialize SWS context for software scaling.
	sws_ctx = sws_getContext(g_pVideoCodecCtx->width, g_pVideoCodecCtx->height, g_pVideoCodecCtx->pix_fmt, g_pVideoCodecCtx->width, g_pVideoCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

	if (sws_ctx == NULL)
	{
		cout << "Could not initialize the conversion context" << endl;
	}

	while (!g_bClose)
	{
		g_hPacketQueueMutex.lock();
		if (g_PacketQueue.empty())
		{
			g_hPacketQueueMutex.unlock();
			Sleep(20);
			continue;
		}


		int nFrameFinished = 0;
		int nAudioFinish = 0;
		int nSubtitleFinish = 0;

		AVPacket* pPacket = g_PacketQueue.front();
		g_PacketQueue.pop();

		g_hPacketQueueMutex.unlock();

		if (pPacket->stream_index == g_nVideoStream)
		{
#pragma region DecodeVideo
			// Decode video frame
			avcodec_decode_video2(g_pVideoCodecCtx, pFrame, &nFrameFinished, pPacket);

			// Did we get a video frame?
			if (nFrameFinished)
			{
				double pts = av_frame_get_best_effort_timestamp(pFrame);

				pts *= av_q2d(g_VideoTimeBase);

				// Convert the image from its native format to RGB.
				sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, g_pVideoCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

				BufferContent pBuffer2;

				pBuffer2.m_dTime = pts;
				pBuffer2.m_nWidth = pFrame->width;
				pBuffer2.m_nHeight = pFrame->height;
				pBuffer2.m_nLength = nNumBytes * sizeof(uint8_t);

				pBuffer2.m_pBuffer = new uint8_t[nNumBytes];

				memcpy(pBuffer2.m_pBuffer, pFrameRGB->data[0], pBuffer2.m_nLength);

				g_hVideoQueueMutex.lock();
				g_VideoDataQueue.push(pBuffer2);
				g_hVideoQueueMutex.unlock();
			}
#pragma endregion
		}
		else if (pPacket->stream_index == g_nAudioStream)
		{
#pragma region DecodeAudio		
			avcodec_decode_audio4(g_pAudioCodecCtx, pAudioFrame, &nAudioFinish, pPacket);

			if (nAudioFinish)
			{
				double pts = av_frame_get_best_effort_timestamp(pAudioFrame);

				pts *= av_q2d(g_AudioTimeBase);

				//int iDataSize = av_samples_get_buffer_size(NULL, g_pAudioCodecCtx->channels, pAudioFrame->nb_samples, g_pAudioCodecCtx->sample_fmt, 1);
				int iDataSize2 = av_samples_get_buffer_size(NULL, g_pAudioCodecCtx->channels, pAudioFrame->nb_samples, AV_SAMPLE_FMT_FLT, 1);

				if (g_pAudioCodecCtx->sample_fmt != AV_SAMPLE_FMT_FLT)
				{
					uint8_t* outData = new uint8_t[iDataSize2];

					SwrContext* pAudioCvtContext = NULL;
					pAudioCvtContext = swr_alloc_set_opts(NULL, (long)g_pAudioCodecCtx->channel_layout, AV_SAMPLE_FMT_FLT, g_pAudioCodecCtx->sample_rate, (long)g_pAudioCodecCtx->channel_layout, g_pAudioCodecCtx->sample_fmt, g_pAudioCodecCtx->sample_rate, 0, (void*)0);

					int error = 0;
					if (error = swr_init(pAudioCvtContext) < 0)
					{
						cout << "ERROR" << error << endl;
					}

					swr_convert(pAudioCvtContext, &outData, iDataSize2, (const uint8_t**)pAudioFrame->extended_data, pAudioFrame->nb_samples);

					swr_free(&pAudioCvtContext);

					BufferContent pBuffer;

					pBuffer.m_nLength = iDataSize2;
					pBuffer.m_nChannels = g_pAudioCodecCtx->channels;
					pBuffer.m_nSampleRate = g_pAudioCodecCtx->sample_rate;
					pBuffer.m_pBuffer = outData;
					pBuffer.m_dTime = pts;

					g_hAudioQueueMutex.lock();
					g_AudioDataQueue.push(pBuffer);
					g_hAudioQueueMutex.unlock();
				}
				else
				{
					BufferContent pBuffer;

					pBuffer.m_pBuffer = new uint8_t[iDataSize2];

					pBuffer.m_nChannels = g_pAudioCodecCtx->channels;
					pBuffer.m_nSampleRate = g_pAudioCodecCtx->sample_rate;
					memcpy(pBuffer.m_pBuffer, pAudioFrame->extended_data[0], pBuffer.m_nLength);
					pBuffer.m_nLength = iDataSize2;
					pBuffer.m_dTime = pts;

					g_hAudioQueueMutex.lock();
					g_AudioDataQueue.push(pBuffer);
					g_hAudioQueueMutex.unlock();
				}
			}
#pragma endregion
		}
		else if (pPacket->stream_index == g_nSubtitleStream)
		{
#pragma region DecodeSubtitle
			avcodec_decode_subtitle2(g_pSubtitleCodecCtx, pSubtitle, &nSubtitleFinish, pPacket);

			if (nSubtitleFinish)
			{
				int i = MultiByteToWideChar(CP_UTF8, 0, pSubtitle->rects[0]->ass, -1, NULL, 0);
				WCHAR   *strUnicode = new   WCHAR[i];
				MultiByteToWideChar(CP_UTF8, 0, pSubtitle->rects[0]->ass, -1, strUnicode, i);

				//cout << pSubtitle->format << endl;

				BufferContent pBuffer;

				pBuffer.m_pSubtitle = strUnicode;
				pBuffer.m_nLength = i;
				pBuffer.m_dStartTime = ((pSubtitle->start_display_time + pSubtitle->pts)* av_q2d(g_SubtitleTimeBase)) / 1000;
				pBuffer.m_dEndTime = pBuffer.m_dStartTime + pSubtitle->end_display_time / 1000;

				g_hSubitleQueueMutex.lock();
				g_SubtitleDataQueue.push(pBuffer);
				g_hSubitleQueueMutex.unlock();
			}
#pragma endregion 
		}

		av_free_packet(pPacket);
		pPacket = NULL;
	}

	// Free the RGB image.
	av_free(pBuffer);
	av_frame_free(&pFrameRGB);

	// Free the YUV frame.
	av_frame_free(&pFrame);

	av_frame_free(&pAudioFrame);

	av_free(pSubtitle);
	pSubtitle = NULL;

	// Close the codecs.
	avcodec_close(g_pVideoCodecCtx);
	avcodec_close(g_pAudioCodecCtx);
	if (g_nSubtitleStream != -1)
	{
		avcodec_close(g_pSubtitleCodecCtx);
	}

	g_bDecodeThreadClose = true;
}

extern"C" __declspec(dllexport) uint8_t* GetVideoBuffer(int& len, int& width, int& height, double& time)
{
	if (g_bClose)
	{
		return NULL;
	}

	g_hVideoQueueMutex.lock();
	if (g_VideoDataQueue.empty())
	{
		g_hVideoQueueMutex.unlock();
		return NULL;
	}

	BufferContent Content = g_VideoDataQueue.front();

	len = Content.m_nLength;
	width = Content.m_nWidth;
	height = Content.m_nHeight;
	time = Content.m_dTime;

	g_hVideoQueueMutex.unlock();

	return Content.m_pBuffer;
}

extern"C" __declspec(dllexport) uint8_t* GetAudioBuffer(int& len, int&channels, int&sample_rate, double& time)
{
	if (g_bClose)
	{
		return NULL;
	}

	g_hAudioQueueMutex.lock();
	if (g_AudioDataQueue.empty())
	{
		g_hAudioQueueMutex.unlock();
		return NULL;
	}

	BufferContent pBuffer = g_AudioDataQueue.front();

	channels = pBuffer.m_nChannels;
	sample_rate = pBuffer.m_nSampleRate;
	len = pBuffer.m_nLength;
	time = pBuffer.m_dTime;

	g_hAudioQueueMutex.unlock();

	return pBuffer.m_pBuffer;
}
extern"C" __declspec(dllexport) bool FreeAudioBuffer()
{
	g_hAudioQueueMutex.lock();
	if (g_AudioDataQueue.empty())
	{
		g_hAudioQueueMutex.unlock();
		return false;
	}

	BufferContent pBuffer = g_AudioDataQueue.front();
	g_AudioDataQueue.pop();

	g_hAudioQueueMutex.unlock();

	free(pBuffer.m_pBuffer);
	pBuffer.m_pBuffer = NULL;

	return true;
}

extern"C" __declspec(dllexport) bool FreeVideoBuffer()
{
	g_hVideoQueueMutex.lock();
	if (g_VideoDataQueue.empty())
	{
		g_hVideoQueueMutex.unlock();
		return false;
	}

	BufferContent Content = g_VideoDataQueue.front();
	g_VideoDataQueue.pop();

	g_hVideoQueueMutex.unlock();

	free(Content.m_pBuffer);
	Content.m_pBuffer = NULL;

	return true;
}

extern"C" __declspec(dllexport) WCHAR* GetSubtitle(int& len, double& start_time, double& end_time)
{
	if (g_bClose)
	{
		return NULL;
	}

	g_hSubitleQueueMutex.lock();

	if (g_SubtitleDataQueue.empty())
	{
		g_hSubitleQueueMutex.unlock();
		return NULL;
	}

	BufferContent pBuffer = g_SubtitleDataQueue.front();

	len = pBuffer.m_nLength;
	start_time = pBuffer.m_dStartTime;
	end_time = pBuffer.m_dEndTime;

	g_hSubitleQueueMutex.unlock();

	return pBuffer.m_pSubtitle;
}

extern"C" __declspec(dllexport) bool FreeSubtitle()
{
	g_hSubitleQueueMutex.lock();
	if (g_SubtitleDataQueue.empty())
	{
		g_hSubitleQueueMutex.unlock();
		return false;
	}

	BufferContent Content = g_SubtitleDataQueue.front();
	g_SubtitleDataQueue.pop();

	g_hSubitleQueueMutex.unlock();

	free(Content.m_pSubtitle);
	Content.m_pSubtitle = NULL;

	return true;
}

extern"C" __declspec(dllexport) void Close()
{
	g_bClose = true;

	while (!(g_bOpenThreadClose && g_bDecodeThreadClose))
	{
		Sleep(10);
	}

	while (!g_PacketQueue.empty())
	{
		AVPacket* Packet = g_PacketQueue.front();
		g_PacketQueue.pop();
		av_free_packet(Packet);
		Packet = NULL;
	}

	while (!g_VideoDataQueue.empty())
	{
		BufferContent Content = g_VideoDataQueue.front();
		free(Content.m_pBuffer);
		Content.m_pBuffer = NULL;
		g_VideoDataQueue.pop();
	}

	while (!g_AudioDataQueue.empty())
	{
		BufferContent Content = g_AudioDataQueue.front();
		free(Content.m_pBuffer);
		Content.m_pBuffer = NULL;
		g_AudioDataQueue.pop();
	}

	while (!g_SubtitleDataQueue.empty())
	{
		BufferContent Content = g_SubtitleDataQueue.front();
		free(Content.m_pSubtitle);
		Content.m_pSubtitle = NULL;
		g_SubtitleDataQueue.pop();
	}
}

extern"C" __declspec(dllexport) int Open(char* url, int transport_type, char* udp_buffer_size, char* time_out, uint64_t seek, bool is_stream)
{
	string pURL = url;
	AVFormatContext *pFormatCtx = NULL;
	AVDictionary* dic = NULL;

	char * pTransportType = NULL;
	switch (transport_type)
	{
	case 0:
		pTransportType = (char*)"udp";
		break;
	case 1:
		pTransportType = (char*)"tcp";
		break;
	default:
		break;
	}

	av_dict_set(&dic, "rtsp_transport", pTransportType, 0);
	av_dict_set(&dic, "bufsize", udp_buffer_size, 0);
	av_dict_set(&dic, "stimeout", time_out, 0);

#pragma region OPEN
	// Open video file.
	if (avformat_open_input(&pFormatCtx, pURL.c_str(), NULL, &dic) != 0)
	{
		fprintf(stderr, "Couldn't open file.\n");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -1; // Couldn't open file.
	}


	// Retrieve stream information.
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		fprintf(stderr, "Couldn't find stream information.\n");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -2; // Couldn't find stream information.
	}

	g_dVideoLength = pFormatCtx->duration;


	// Find the first video stream.
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		cout << pFormatCtx->streams[i]->codec->codec_type << endl;

		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			g_nVideoStream = i;
		}

		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			g_nAudioStream = i;
		}

		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE)
		{
			g_nSubtitleStream = i;
		}
	}
	if (g_nVideoStream == -1)
	{
		fprintf(stderr, "Didn't find a video stream.\n");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -3; // Didn't find a video stream.
	}

	if (g_nAudioStream == -1)
	{
		fprintf(stderr, "Didn't find a audio stream.\n");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -4; // Didn't find a video stream.
	}

	g_VideoTimeBase = pFormatCtx->streams[g_nVideoStream]->time_base;
	g_AudioTimeBase = pFormatCtx->streams[g_nAudioStream]->time_base;

	if (g_nSubtitleStream != -1)
	{
		cout << "Find Subtitle" << endl;
		g_SubtitleTimeBase = pFormatCtx->streams[g_nSubtitleStream]->time_base;
	}

	// Get a pointer to the codec context for the video stream.
	AVCodecContext* pVideoCodecCtxOrig = pFormatCtx->streams[g_nVideoStream]->codec;
	// Find the decoder for the video stream.
	AVCodec* pVideoCodec = avcodec_find_decoder(pVideoCodecCtxOrig->codec_id);
	if (pVideoCodec == NULL)
	{
		fprintf(stderr, "Unsupported codec!\n");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -5; // Codec not found.
	}

	AVCodecContext* pAudioCodecCtxOrig = pFormatCtx->streams[g_nAudioStream]->codec;
	AVCodec* pAudioCodec = avcodec_find_decoder(pAudioCodecCtxOrig->codec_id);
	if (pAudioCodec == NULL)
	{
		fprintf(stderr, "Unsupported codec!\n");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -6; // Codec not found.
	}

	AVCodecContext* pSubtitleCodecCtxOrig = NULL;
	AVCodec* pSubtitleCodec = NULL;


	if (g_nSubtitleStream != -1)
	{
		pSubtitleCodecCtxOrig = pFormatCtx->streams[g_nSubtitleStream]->codec;
		pSubtitleCodec = avcodec_find_decoder(pSubtitleCodecCtxOrig->codec_id);
		if (pSubtitleCodec == NULL)
		{
			fprintf(stderr, "Unsupported codec!\n");
			g_bDecodeThreadClose = true;
			g_bOpenThreadClose = true;
			return -7; // Codec not found.
		}
	}



	// Copy context.
	g_pVideoCodecCtx = avcodec_alloc_context3(pVideoCodec);
	if (avcodec_copy_context(g_pVideoCodecCtx, pVideoCodecCtxOrig) != 0)
	{
		fprintf(stderr, "Couldn't copy codec context");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -8; // Error copying codec context.
	}
	// Open codec.
	if (avcodec_open2(g_pVideoCodecCtx, pVideoCodec, NULL) < 0)
	{
		fprintf(stderr, "Could not open codec.");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -9; // Could not open codec.
	}

	g_pAudioCodecCtx = avcodec_alloc_context3(pAudioCodec);
	if (avcodec_copy_context(g_pAudioCodecCtx, pAudioCodecCtxOrig) != 0)
	{
		fprintf(stderr, "Couldn't copy codec context");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -10; // Error copying codec context.
	}

	if (avcodec_open2(g_pAudioCodecCtx, pAudioCodec, NULL) < 0)
	{
		fprintf(stderr, "Could not open codec.");
		g_bDecodeThreadClose = true;
		g_bOpenThreadClose = true;
		return -11; // Could not open codec.
	}

	if (g_nSubtitleStream != -1)
	{
		g_pSubtitleCodecCtx = avcodec_alloc_context3(pSubtitleCodec);
		if (avcodec_copy_context(g_pSubtitleCodecCtx, pSubtitleCodecCtxOrig) != 0)
		{
			fprintf(stderr, "Couldn't copy codec context");
			g_bDecodeThreadClose = true;
			g_bOpenThreadClose = true;
			return -12; // Error copying codec context.
		}

		if (avcodec_open2(g_pSubtitleCodecCtx, pSubtitleCodec, NULL) < 0)
		{
			fprintf(stderr, "Could not open codec.");
			g_bDecodeThreadClose = true;
			g_bOpenThreadClose = true;
			return -13; // Could not open codec.
		}
	}


#pragma region Seek
	uint64_t lSeek = seek * 1000 * 1000;

	if (!is_stream)
	{
		lSeek = av_rescale_q(lSeek, av_get_time_base_q(), g_VideoTimeBase);

		if (av_seek_frame(pFormatCtx, g_nVideoStream, lSeek, AVSEEK_FLAG_BACKWARD) < 0)
		{
			g_bDecodeThreadClose = true;
			g_bOpenThreadClose = true;
			return -14;
		}
	}
#pragma endregion

#pragma endregion

	thread T1(Decode);
	T1.detach();

	while (!g_bClose)
	{
		g_hVideoQueueMutex.lock();
		int i = g_VideoDataQueue.size();
		g_hVideoQueueMutex.unlock();

		g_hPacketQueueMutex.lock();
		int j = g_PacketQueue.size();
		g_hPacketQueueMutex.unlock();

		if (i + j > g_nPacketCount)
		{
			Sleep(20);
			continue;
		}

		AVPacket* Packet = av_packet_alloc();
		av_init_packet(Packet);

		av_read_frame(pFormatCtx, Packet);

		g_hPacketQueueMutex.lock();
		g_PacketQueue.push(Packet);
		g_hPacketQueueMutex.unlock();
	}

	avcodec_close(pVideoCodecCtxOrig);
	avcodec_close(pAudioCodecCtxOrig);

	if (g_nSubtitleStream != -1)
	{
		avcodec_close(pSubtitleCodecCtxOrig);
	}

	// Close the video file.
	if (pFormatCtx != nullptr)
	{
		avformat_close_input(&pFormatCtx);
	}

	g_bOpenThreadClose = true;

	return 0;
}

