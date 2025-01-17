#ifndef MEDIAFRAMELISTENERBRIDGE_H
#define MEDIAFRAMELISTENERBRIDGE_H

#include "acumulator.h"
#include "media.h"
#include "rtp.h"
#include "TimeService.h"

#include <queue>
#include <memory>

using namespace std::chrono_literals;

class MediaFrameListenerBridge :
	public MediaFrame::Listener,
	public RTPIncomingMediaStream,
	public RTPReceiver
{
public:
	using shared = std::shared_ptr<MediaFrameListenerBridge>;
public:
	static constexpr uint32_t NoSeqNum = std::numeric_limits<uint32_t>::max();
	static constexpr uint64_t NoTimestamp = std::numeric_limits<uint64_t>::max();
public:
	MediaFrameListenerBridge(TimeService& timeService, DWORD ssrc, bool smooth = false);
	virtual ~MediaFrameListenerBridge();
	
	void AddMediaListener(const MediaFrame::Listener::shared& listener);
	void RemoveMediaListener(const MediaFrame::Listener::shared& listener);
        
	virtual void AddListener(RTPIncomingMediaStream::Listener* listener);
	virtual void RemoveListener(RTPIncomingMediaStream::Listener* listener);
        
	virtual DWORD GetMediaSSRC() const override{ return ssrc; }
	
	virtual void onMediaFrame(const MediaFrame &frame) override;
	virtual void onMediaFrame(DWORD ssrc, const MediaFrame &frame) override { onMediaFrame(frame); }
	virtual TimeService& GetTimeService() override  { return timeService; }
	virtual void Mute(bool muting) override;
	void Reset();
	void Update();
	void Update(QWORD now);
	void Stop();

	virtual int SendPLI(DWORD ssrc) override { return 1; };
	virtual int Reset(DWORD ssrc) override { return 1; };

private:
	void Dispatch(const std::vector<RTPPacket::shared>& packet);
        
public:
	TimeService& timeService;
	Timer::shared dispatchTimer;

	std::queue<std::pair<RTPPacket::shared, std::chrono::milliseconds>> packets;

	DWORD ssrc = 0;
	DWORD extSeqNum = 0;
	bool  smooth = true;
	std::set<RTPIncomingMediaStream::Listener*> listeners;
        std::set<MediaFrame::Listener::shared> mediaFrameListeners;
	volatile bool reset	= false;
	QWORD firstTimestamp	= NoTimestamp;
	QWORD baseTimestamp	= 0;
	QWORD lastTimestamp	= 0;
	QWORD lastTime		= 0;
	DWORD numFrames		= 0;
	DWORD numFramesDelta	= 0;
	DWORD numPackets	= 0;
	DWORD numPacketsDelta	= 0;
	DWORD totalBytes	= 0;
	DWORD bitrate		= 0;
	Acumulator<uint32_t, uint64_t> acumulator;
	Acumulator<uint32_t, uint64_t> accumulatorFrames;
	Acumulator<uint32_t, uint64_t> accumulatorPackets;

	
	std::chrono::milliseconds lastSent = 0ms;
	
	DWORD minWaitedTime	= 0;
	DWORD maxWaitedTime	= 0;
	long double avgWaitedTime = 0;
	Acumulator<uint32_t, uint64_t> waited;
	volatile bool muted = false;
};

#endif /* MEDIAFRAMELISTENERBRIDGE_H */

