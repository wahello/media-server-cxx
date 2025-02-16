/* 
 * File:   VideoEncoderWorker.cpp
 * Author: Sergio
 * 
 * Created on 12 de agosto de 2014, 10:32
 */

#include "VideoEncoderWorker.h"
#include "log.h"
#include "tools.h"
#include "acumulator.h"
#include "VideoCodecFactory.h"

VideoEncoderWorker::VideoEncoderWorker() 
{
	//Create objects
	pthread_mutex_init(&mutex,NULL);
	pthread_cond_init(&cond,NULL);
}

VideoEncoderWorker::~VideoEncoderWorker()
{
	End();
	//Clean object
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}

int VideoEncoderWorker::Init(VideoInput *input)
{
	//Store it
	this->input = input;
	//Done
	return true;
}

int VideoEncoderWorker::SetCodec(VideoCodec::Type codec,int mode,int fps,int bitrate,int intraPeriod, const Properties& properties)
{
	return SetVideoCodec(codec,GetWidth(mode),GetHeight(mode),fps,bitrate,intraPeriod,properties);
}

int VideoEncoderWorker::SetVideoCodec(VideoCodec::Type codec, int width, int height, int fps,int bitrate,int intraPeriod, const Properties& properties)
{
	Log("-VideoEncoderWorker::SetCodec() [%s,width:%d,height:%d,fps:%d,bitrate:%d,intraPeriod:%d]\n",VideoCodec::GetNameFor(codec),width,height,fps,bitrate,intraPeriod);

	//Check size
	if (!width || !height)
		//Error
		return Error("Wrong size\n");
	
	//Store parameters
	this->codec	  = codec;
	this->width	  = width;
	this->height	  = height;
	this->bitrate	  = bitrate;
	this->fps	  = fps;
	this->intraPeriod = intraPeriod;
	//Init limits
	this->bitrateLimit	= bitrate;
	this->bitrateLimitCount	= fps;
        //Store properties
        this->properties  = properties;

	//Good
	return 1;
}

int VideoEncoderWorker::Start()
{
	Log("-VideoEncoderWorker::Start()\n");
	
	//Check
	if (!input)
		//Exit
		return Error("-VideoEncoderWorker::Start() Error: null video input");
	
	
	//Check if need to restart
	if (encoding)
		//Stop first
		Stop();

	//Start decoding
	encoding = 1;

	//launc thread
	createPriorityThread(&thread,startEncoding,this,0);

	return 1;
}

void * VideoEncoderWorker::startEncoding(void *par)
{
	//Get worker
	VideoEncoderWorker *worker = (VideoEncoderWorker *)par;
	//Block all signals
	blocksignals();
	//Run
	worker->Encode();
	//Exit
	return NULL;
}

int VideoEncoderWorker::Stop()
{
	Log(">VideoEncoderWorker::Stop()\n");

	//If we were started
	if (encoding)
	{
		//Stop
		encoding=0;

		//Cancel and frame grabbing
		input->CancelGrabFrame();

		//Cancel sending
		pthread_cond_signal(&cond);

		//Esperamos
		pthread_join(thread,NULL);
	}

	Log("<VideoEncoderWorker::Stop()\n");

	return 1;
}

int VideoEncoderWorker::End()
{
	//Check if already decoding
	if (encoding)
		//Stop
		Stop();

	//Set null
	input = NULL;
	
	//Done
	return 1;
}

int VideoEncoderWorker::Encode()
{
	timeval first;
	timeval prev;
	timeval lastFPU;
	
	DWORD num = 0;
	QWORD overslept = 0;

	Acumulator bitrateAcu(1000);
	Acumulator fpsAcu(1000);

	Log(">VideoEncoderWorker::Encode() [width:%d,size:%d,bitrate:%d,fps:%d,intra:%d]\n",width,height,bitrate,fps,intraPeriod);

	//Creamos el encoder
	VideoEncoder* videoEncoder = VideoCodecFactory::CreateEncoder(codec,properties);

	//Comprobamos que se haya creado correctamente
	if (videoEncoder == NULL)
		//error
		return Error("Can't create video encoder\n");

	//Comrpobamos que tengamos video de entrada
	if (input == NULL)
		return Error("No video input");

	//Iniciamos el tama�o del video
	if (!input->StartVideoCapture(width,height,fps))
		return Error("Couldn't set video capture\n");

	//Start at 80%
	int current = bitrate*0.8;

	//Send at higher bitrate first frame, but skip frames after that so sending bitrate is kept
	videoEncoder->SetFrameRate(fps,current*5,intraPeriod);

	//No wait for first
	QWORD frameTime = 0;

	//Iniciamos el tamama�o del encoder
 	videoEncoder->SetSize(width,height);
	
	//The time of the first one
	gettimeofday(&first,NULL);

	//The time of the previos one
	gettimeofday(&prev,NULL);

	//Fist FPU
	gettimeofday(&lastFPU,NULL);

	//Mientras tengamos que capturar
	while(encoding)
	{
		//Capture video frame buffer
		auto pic = input->GrabFrame(frameTime/1000);

		//Check picture
		if (!pic.buffer)
			//Exit
			continue;
		
		//Check size
		if (pic.width!=width || pic.height!=height)
		{
			//Check
			if (videoEncoder)
				//Borramos el encoder
				delete videoEncoder;
			//Update size
			width	= pic.width;
			height	= pic.height;
			//Create encoder again
			videoEncoder = VideoCodecFactory::CreateEncoder(codec,properties);
			//Reset bitrate
			videoEncoder->SetFrameRate(fps,current,intraPeriod);
			//Set on the encoder
			videoEncoder->SetSize(width,height);
		}

		//Check if we need to send intra
		if (sendFPU)
		{
			//Do not send anymore
			sendFPU = false;
			//Do not send if just send one (100ms)
			if (getDifTime(&lastFPU)/100>100)
			{
				//Set it
				videoEncoder->FastPictureUpdate();
				//Update last FPU
				getUpdDifTime(&lastFPU);
			}
		}
		//Calculate target bitrate
		int target = current;

		//Check temporal limits for estimations
		if (bitrateAcu.IsInWindow())
		{
			//Get real sent bitrate during last second and convert to kbits
			DWORD instant = bitrateAcu.GetInstantAvg()/1000;
			//If we are in quarentine
			if (bitrateLimitCount)
				//Limit sending bitrate
				target = bitrateLimit;
			//Check if sending below limits
			else if (instant<bitrate)
				//Increase a 8% each second or fps kbps
				target += (DWORD)(target*0.08/fps)+1;
		}

		//Check target bitrate agains max conf bitrate
		if (target>bitrate*1.2)
			//Set limit to max bitrate allowing a 20% overflow so instant bitrate can get closer to target
			target = bitrate*1.2;

		//Check limits counter
		if (bitrateLimitCount>0)
			//One frame less of limit
			bitrateLimitCount--;

		//Check if we have a new bitrate
		if (target && target!=current)
		{
			//Reset bitrate
			videoEncoder->SetFrameRate(fps,target,intraPeriod);
			//Upate current
			current = target;
		}

		

		//Procesamos el frame
		VideoFrame *videoFrame = videoEncoder->EncodeFrame(pic.buffer,pic.GetBufferSize());

		//If was failed
		if (!videoFrame)
			//Next
			continue;

		//Increase frame counter
		fpsAcu.Update(getTime()/1000,1);

		//Check
		if (frameTime)
		{
			timespec ts;
			//Lock
			pthread_mutex_lock(&mutex);
			//Calculate slept time
			QWORD sleep = frameTime;
			//Remove extra sleep from prev
			if (overslept<sleep)
				//Remove it
				sleep -= overslept;
			else
				//Do not overflow
				sleep = 1;
			//Calculate timeout
			calcAbsTimeoutNS(&ts,&prev,sleep);
			//Wait next or stopped
			int canceled  = !pthread_cond_timedwait(&cond,&mutex,&ts);
			//Unlock
			pthread_mutex_unlock(&mutex);
			//Check if we have been canceled
			if (canceled)
				//Exit
				break;
			//Get differencence
			QWORD diff = getDifTime(&prev);
			//If it is biffer
			if (diff>frameTime)
				//Get what we have slept more
				overslept = diff-frameTime;
			else
				//No oversletp (shoulddn't be possible)
				overslept = 0;
		}
		
		//If first
		if (!frameTime)
		{
			//Set frame time, slower
			frameTime = 5*1E6/fps;
			//Restore frame rate
			videoEncoder->SetFrameRate(fps,current,intraPeriod);
		} else {
			//Set frame time
			frameTime = 1E6/fps;
		}

		//Add frame size in bits to bitrate calculator
	        bitrateAcu.Update(getDifTime(&first)/1000,videoFrame->GetLength()*8);

		//Set clock rate
		videoFrame->SetClockRate(90000);
		//Get now
		auto now = getDifTime(&first)/1000;
		//Set frame timestamp
		videoFrame->SetTimestamp(now*90);
		videoFrame->SetTime(now);
		//Set dudation
		videoFrame->SetDuration(frameTime*90000/1E6);
		
		//Lock
		pthread_mutex_lock(&mutex);

		//For each listener
		for (auto &listener : listeners)
		{
			//If was not null
			if (listener)
				//Call listener
				listener->onMediaFrame(*videoFrame);
		}

		//unlock
		pthread_mutex_unlock(&mutex);

		//Set sending time of previous frame
		getUpdDifTime(&prev);
		
		//Calculate sending times based on bitrate
		DWORD sendingTime = videoFrame->GetLength()*8/current;

		//Adjust to maximum time
		if (sendingTime>frameTime/1000)
			//Cap it
			sendingTime = frameTime/1000;

//                //If it was a I frame
//                if (videoFrame->IsIntra())
//			//Clean rtp rtx buffer
//			FlushRTXPackets();
//
//		//Send it smoothly
//		SmoothFrame(videoFrame,sendingTime);

		//Dump statistics
		if (num && ((num%fps*10)==0))
		{
			//Debug("-Send bitrate current=%d avg=%llf rate=[%llf,%llf] fps=[%llf,%llf] limit=%d\n",current,bitrateAcu.GetInstantAvg()/1000,bitrateAcu.GetMinAvg()/1000,bitrateAcu.GetMaxAvg()/1000,fpsAcu.GetMinAvg(),fpsAcu.GetMaxAvg(),bitrateLimit);
			bitrateAcu.ResetMinMax();
			fpsAcu.ResetMinMax();
		}
		num++;
	}

	//Terminamos de capturar
	input->StopVideoCapture();

	//Check
	if (videoEncoder)
		//Borramos el encoder
		delete videoEncoder;

	//Salimos
	Log("<VideoEncoderWorker::Encode()  [%d]\n",encoding);
	
	//Done
	return 1;
}

int VideoEncoderWorker::SetTemporalBitrateLimit(int estimation)
{
	//Set bitrate limit
	bitrateLimit = estimation/1000;
	//Set limit of bitrate to 1 second;
	bitrateLimitCount = fps;
	//Exit
	return 1;
}

bool VideoEncoderWorker::AddListener(const MediaFrame::Listener::shared& listener)
{
	//Lock
	pthread_mutex_lock(&mutex);

	//Add to set
	listeners.insert(listener);

	//unlock
	pthread_mutex_unlock(&mutex);

	return true;
}

bool VideoEncoderWorker::RemoveListener(const MediaFrame::Listener::shared& listener)
{
	//Lock
	pthread_mutex_lock(&mutex);

	//Search
	Listeners::iterator it = listeners.find(listener);

	//If found
	if (it!=listeners.end())
		//Erase it
		listeners.erase(it);

	//Unlock
	pthread_mutex_unlock(&mutex);

	return true;
}


void VideoEncoderWorker::SendFPU()
{
	sendFPU = true;
}
