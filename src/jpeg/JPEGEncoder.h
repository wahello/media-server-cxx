#ifndef _JPEGENCODER_H_
#define _JPEGENCODER_H_
#include "codecs.h"
#include "video.h"
extern "C" {
#include <libavcodec/avcodec.h>
}

class JPEGEncoder : public VideoEncoder
{
public:
	JPEGEncoder(const Properties& properties);
	virtual ~JPEGEncoder();
	virtual VideoFrame* EncodeFrame(BYTE* in, DWORD len);
	virtual int FastPictureUpdate();
	virtual int SetSize(int width, int height);
	virtual int SetFrameRate(int fps, int kbits, int intraPeriod);

private:
	int OpenCodec();
private:
	AVCodec* codec		= nullptr;
	AVCodecContext* ctx	= nullptr;
	AVFrame* input		= nullptr;
	VideoFrame	frame;

	int width = 0;
	int height = 0;
	uint32_t numPixels = 0;
	int bitrate = 300000;
	int fps = 30;
	
};

#endif 
