#pragma once

#include <windows.h>
#include <string>
#include <memory>
#include<iostream>
#include<queue>

extern "C"
{
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include "libavutil/time.h"
#include "libavutil/fifo.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavfilter/avfiltergraph.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}

using namespace std;

extern"C" __declspec(dllexport) bool Init(int packet_count);

extern"C" __declspec(dllexport) uint8_t* GetVideoBuffer(int& len, int&width, int&height, double& time);

extern"C" __declspec(dllexport) int Open(char* url, int transport_type, char* udp_buffer_size, char* time_out, uint64_t seek,bool is_stream);

extern"C" __declspec(dllexport) void Close();

extern"C" __declspec(dllexport) bool FreeVideoBuffer();

extern"C" __declspec(dllexport) uint8_t* GetAudioBuffer(int& len, int&channels, int&sample_rate, double& time);

extern"C" __declspec(dllexport) bool FreeAudioBuffer();

extern"C" __declspec(dllexport) double GetVideoLength(int& hour, int& min, int& sec);

extern"C" __declspec(dllexport) WCHAR* GetSubtitle(int& len, double& start_time, double& end_time);

extern"C" __declspec(dllexport) bool FreeSubtitle();