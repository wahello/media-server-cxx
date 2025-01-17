/*
 * File:   h265depacketizer.cpp
 * Author: Sergio
 *
 * Created on 26 de enero de 2012, 9:46
 */

#include "H265Depacketizer.h"
#include "media.h"
#include "codecs.h"
#include "rtp.h"
#include "log.h"


H265Depacketizer::H265Depacketizer() : RTPDepacketizer(MediaFrame::Video, VideoCodec::H265), frame(VideoCodec::H265, 0)
{
	//Set clock rate
	frame.SetClockRate(90000);
}

H265Depacketizer::~H265Depacketizer()
{
}

void H265Depacketizer::ResetFrame()
{
	//Clear packetization info
	frame.Reset();
	//Clear config
	//No fragments
	iniFragNALU = 0;
	startedFrag = false;
}

MediaFrame* H265Depacketizer::AddPacket(const RTPPacket::shared& packet)
{
	//Get timestamp in ms
	auto ts = packet->GetExtTimestamp();
	//Check it is from same packet
	if (frame.GetTimeStamp() != ts)
		//Reset frame
		ResetFrame();
	//If not timestamp
	if (frame.GetTimeStamp() == (DWORD)-1)
	{
		//Set timestamp
		frame.SetTimestamp(ts);
		//Set clock rate
		frame.SetClockRate(packet->GetClockRate());
		//Set time
		frame.SetTime(packet->GetTime());
		//Set sender time
		frame.SetSenderTime(packet->GetSenderTime());
	}
	//Set SSRC
	frame.SetSSRC(packet->GetSSRC());
	//Add payload
	AddPayload(packet->GetMediaData(), packet->GetMediaLength());
	//If it is last return frame
	if (!packet->GetMark())
		return NULL;
	//Return frame
	return &frame;
}

MediaFrame* H265Depacketizer::AddPayload(const BYTE* payload, DWORD payloadLen)
{
	BYTE nalHeader[4];
	BYTE S, E;
	DWORD pos;
	//Check length
	if (payloadLen<2)
		//Exit
		return NULL;

	/* 
	 *   +---------------+---------------+
         *   |0|1|2|3|4|5|6|7|0|1|2|3|4|5|6|7|
         *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *   |F|   Type    |  LayerId  | TID |
         *   +-------------+-----------------+
	 *
	 * F must be 0.
	 */
	 // BYTE nal_ref_idc = (payload[0] & 0x60) >> 5;
	BYTE nalUnitType = payload[0] & 0x1f;

	//Get nal data
	const BYTE* nalData = payload + 2;

	//Get nalu size
	DWORD nalSize = payloadLen;

	Debug("-H265 [NAL:%d,type:%d,size:%d]\n", payload[0], nalUnitType, nalSize);

	//Check type
	switch (nalUnitType)
	{
		case 35: //AUD
		case 36: //EOS
		case 37: //EOB
		case 38: //FD
			/* undefined */
			return NULL;
		case 24:
			/**
			   Figure 7 presents an example of an AP that contains two aggregation
			   units, labeled as 1 and 2 in the figure, without the DONL and DOND
			   fields being present.

			    0                   1                   2                   3
			    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
			   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			   |                          RTP Header                           |
			   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			   |   PayloadHdr (Type=48)        |         NALU 1 Size           |
			   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			   |          NALU 1 HDR           |                               |
			   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+         NALU 1 Data           |
			   |                   . . .                                       |
			   |                                                               |
			   +               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			   |  . . .        | NALU 2 Size                   | NALU 2 HDR    |
			   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			   | NALU 2 HDR    |                                               |
			   +-+-+-+-+-+-+-+-+              NALU 2 Data                      |
			   |                   . . .                                       |
			   |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			   |                               :...OPTIONAL RTP padding        |
			   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

			   Figure 7: An Example of an AP Packet Containing Two Aggregation
			   Units without the DONL and DOND Fields

			*/

			/* Skip SPayloadHdr */
			payload+=2;
			payloadLen-=2;

			/* STAP-A Single-time aggregation packet 5.7.1 */
			while (payloadLen > 2)
			{
				/* Get NALU size */
				nalSize = get2(payload, 0);

				/* strip NALU size */
				payload += 2;
				payloadLen -= 2;

				//Check
				if (!nalSize || nalSize > payloadLen)
					//Error
					break;

				//Get nal type
				nalUnitType = payload[0] & 0x1f;
				//Get data
				nalData = payload + 1;

				//Check if IDR SPS or PPS
				switch (nalUnitType)
				{
					case 19: //IDR
					case 20: //IDR
					case 33: //SPS
					case 34: //PPS
						//It is intra
						frame.SetIntra(true);
						break;
				}

				//Set size
				set4(nalHeader, 0, nalSize);
				//Append data
				frame.AppendMedia(nalHeader, sizeof(nalHeader));

				//Append data and get current post
				pos = frame.AppendMedia(payload, nalSize);
				//Add RTP packet
				frame.AddRtpPacket(pos, nalSize, NULL, 0);

				payload += nalSize;
				payloadLen -= nalSize;
			}
			break;
		case 29:
			/* FU-A	Fragmentation unit	 5.8 */
			/* FU-B	Fragmentation unit	 5.8 */


			//Check length
			if (payloadLen < 3)
				return NULL;

			/* +---------------+
			 * |0|1|2|3|4|5|6|7|
			 * +-+-+-+-+-+-+-+-+
			 * |S|E|R| Type	   |
			 * +---------------+
			 *
			 * R is reserved and always 0
			 */
			S = (payload[2] & 0x80) == 0x80;
			E = (payload[2] & 0x40) == 0x40;

			/* strip off FU indicator and FU header bytes */
			nalSize = payloadLen - 3;

			//if it is the start fragment of the nal unit
			if (S)
			{
				/* NAL unit starts here */
				BYTE fragNalHeader = (payload[0] & 0xe0) | (payload[2] & 0x1f);

				//Get nal type
				nalUnitType = fragNalHeader & 0x1f;

				//Check it
				if (nalUnitType == 0x05)
					//It is intra
					frame.SetIntra(true);

				//Get init of the nal
				iniFragNALU = frame.GetLength();
				//Set empty header, will be set later
				set4(nalHeader, 0, 0);
				//Append data
				frame.AppendMedia(nalHeader, sizeof(nalHeader));
				//Append NAL header
				frame.AppendMedia(&fragNalHeader, 1);
				//We have a start frag
				startedFrag = true;
			}

			//If we didn't receive a start frag
			if (!startedFrag)
				//Ignore
				return NULL;

			//Append data and get current post
			pos = frame.AppendMedia(payload + 3, nalSize);
			//Add rtp payload
			frame.AddRtpPacket(pos, nalSize, payload, 3);

			//If it is the end fragment of the nal unit
			if (E)
			{
				//Ensure it is valid
				if (iniFragNALU + 4 > frame.GetLength())
					//Error
					return NULL;
				//Get NAL size
				DWORD nalSize = frame.GetLength() - iniFragNALU - 4;
				//Set it
				set4(frame.GetData(), iniFragNALU, nalSize);
				//Done with fragment
				iniFragNALU = 0;
				startedFrag = false;
			}
			//Done
			break;
		default:
			/* the entire payload is the output buffer */
			nalSize = payloadLen;
			//Check if IDR SPS or PPS
			switch (nalUnitType)
			{
				case 19: //IDR
				case 20: //IDR
				case 33: //SPS
				case 34: //PPS
					//It is intra
					frame.SetIntra(true);
					break;
			}
			//Set size
			set4(nalHeader, 0, nalSize);
			//Append data
			frame.AppendMedia(nalHeader, sizeof(nalHeader));
			//Append data and get current post
			pos = frame.AppendMedia(payload, nalSize);
			//Add RTP packet
			frame.AddRtpPacket(pos, nalSize, NULL, 0);
			//Done
			break;
	}

	return &frame;
}

