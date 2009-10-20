/*
 *  Copyright (C) 2006 - 2009 Stephen F. Booth <me@sbooth.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "AudioPlayer.h"
#include "CoreAudioDecoder.h"

#include "CARingBuffer.h"


// ========================================
// Macros
// ========================================
#define RING_BUFFER_SIZE_FRAMES					16384
//#define RING_BUFFER_WRITE_CHUNK_SIZE_FRAMES		2048


// ========================================
// Constants
// ========================================
CFStringRef const AudioPlayerErrorDomain = CFSTR("org.sbooth.Play.ErrorDomain.AudioPlayer");


// ========================================
// Utility functions
// ========================================
static bool
channelLayoutsAreEqual(AudioChannelLayout *layoutA,
					   AudioChannelLayout *layoutB)
{
	assert(NULL != layoutA);
	assert(NULL != layoutB);
	
	// First check if the tags are equal
	if(layoutA->mChannelLayoutTag != layoutB->mChannelLayoutTag)
		return false;
	
	// If the tags are equal, check for special values
	if(kAudioChannelLayoutTag_UseChannelBitmap == layoutA->mChannelLayoutTag)
		return (layoutA->mChannelBitmap == layoutB->mChannelBitmap);
	
	if(kAudioChannelLayoutTag_UseChannelDescriptions == layoutA->mChannelLayoutTag) {
		if(layoutA->mNumberChannelDescriptions != layoutB->mNumberChannelDescriptions)
			return false;
		
		size_t bytesToCompare = layoutA->mNumberChannelDescriptions * sizeof(AudioChannelDescription);
		return (0 == memcmp(&layoutA->mChannelDescriptions, &layoutB->mChannelDescriptions, bytesToCompare));
	}
	
	return true;
}

static OSStatus
myAURenderCallback(void *							inRefCon,
				   AudioUnitRenderActionFlags *		ioActionFlags,
				   const AudioTimeStamp *			inTimeStamp,
				   UInt32							inBusNumber,
				   UInt32							inNumberFrames,
				   AudioBufferList *				ioData)
{
	assert(NULL != inRefCon);
	
	AudioPlayer *player = (AudioPlayer *)inRefCon;
	return player->Render(ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, ioData);
}

static inline void 
CFLog(CFStringRef format, ...)
{
	va_list args;
	va_start(args, format);
	
	CFStringRef message = CFStringCreateWithFormatAndArguments(kCFAllocatorDefault,
															   NULL,
															   format,
															   args);
	
	CFShow(message);
	CFRelease(message);
}


#pragma mark Creation/Destruction

AudioPlayer::AudioPlayer()
	: mRingBuffer(NULL), mFramesDecoded(0), mFramesRendered(0)
{
	mRingBuffer = new CARingBuffer();		
	
	CreateAUGraph();
}

AudioPlayer::~AudioPlayer()
{
	DisposeAUGraph();
	
	if(mRingBuffer)
		delete mRingBuffer, mRingBuffer = NULL;
}

#pragma mark Playback Control

void AudioPlayer::Play()
{
	if(IsPlaying())
		return;
	
	OSStatus result = AUGraphStart(mAUGraph);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphStart failed: %i"), result);
#endif
	}
}

void AudioPlayer::Pause()
{
	if(!IsPlaying())
		return;
	
	OSStatus result = AUGraphStop(mAUGraph);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphStop failed: %i"), result);
#endif
	}
}

void AudioPlayer::PlayPause()
{
	IsPlaying() ? Pause() : Play();
}

void AudioPlayer::Stop()
{
	if(!IsPlaying())
		return;

	Pause();
}

bool AudioPlayer::IsPlaying()
{
	Boolean isRunning = FALSE;
	OSStatus result = AUGraphIsRunning(mAUGraph, &isRunning);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphIsRunning failed: %i"), result);
#endif
	}
		
	return isRunning;
}

#pragma mark Seeking

#pragma mark Player Parameters

Float32 AudioPlayer::GetVolume()
{
	AudioUnit au = NULL;
	OSStatus auResult = AUGraphNodeInfo(mAUGraph, 
										mOutputNode, 
										NULL, 
										&au);

	if(noErr != auResult) {
#if DEBUG
		CFLog(CFSTR("AUGraphNodeInfo failed: %i"), auResult);
#endif
		return -1;
	}
	
	Float32 volume = -1;
	ComponentResult result = AudioUnitGetParameter(au,
												   kHALOutputParam_Volume,
												   kAudioUnitScope_Global,
												   0,
												   &volume);
	
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AudioUnitGetParameter (kHALOutputParam_Volume) failed: %i"), result);
#endif
	}
		
	return volume;
}

bool AudioPlayer::SetVolume(Float32 volume)
{
	assert(0 <= volume);
	assert(volume <= 1);
	
	AudioUnit au = NULL;
	OSStatus auResult = AUGraphNodeInfo(mAUGraph, 
										mOutputNode, 
										NULL, 
										&au);
	
	if(noErr != auResult) {
#if DEBUG
		CFLog(CFSTR("AUGraphNodeInfo failed: %i"), auResult);
#endif
		return -1;
	}
	
	ComponentResult result = AudioUnitSetParameter(au,
												   kHALOutputParam_Volume,
												   kAudioUnitScope_Global,
												   0,
												   volume,
												   0);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AudioUnitSetParameter (kHALOutputParam_Volume) failed: %i"), result);
#endif
		return false;
	}
	
	return true;
}

Float32 AudioPlayer::GetPreGain()
{
	if(false == PreGainIsEnabled())
		return 0.f;

	AudioUnit au = NULL;
	OSStatus auResult = AUGraphNodeInfo(mAUGraph, 
										mLimiterNode, 
										NULL, 
										&au);
	
	if(noErr != auResult) {
#if DEBUG
		CFLog(CFSTR("AUGraphNodeInfo failed: %i"), auResult);
#endif
		return -1;
	}

	Float32 preGain = -1;
	ComponentResult result = AudioUnitGetParameter(au, 
												   kLimiterParam_PreGain, 
												   kAudioUnitScope_Global, 
												   0,
												   &preGain);
	
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AudioUnitGetParameter (kLimiterParam_PreGain) failed: %i"), result);
#endif
	}
	
	return preGain;
}

bool AudioPlayer::SetPreGain(Float32 preGain)
{
	if(0.f == preGain)
		return EnablePreGain(false);
	
	AudioUnit au = NULL;
	OSStatus result = AUGraphNodeInfo(mAUGraph, 
									  mLimiterNode, 
									  NULL, 
									  &au);
	
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphNodeInfo failed: %i"), result);
#endif
		return false;
	}
	
	AudioUnitParameter auParameter;
	
	auParameter.mAudioUnit		= au;
	auParameter.mParameterID	= kLimiterParam_PreGain;
	auParameter.mScope			= kAudioUnitScope_Global;
	auParameter.mElement		= 0;
	
	result	= AUParameterSet(NULL, 
							 NULL, 
							 &auParameter, 
							 preGain,
							 0);
	
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUParameterSet (kLimiterParam_PreGain) failed: %i"), result);
#endif
		return false;
	}
	
	return true;
}

#pragma mark Playlist Management

bool AudioPlayer::Play(AudioDecoder *decoder)
{
	assert(NULL != decoder);

	d = decoder;
	
	OSStatus result = SetAUGraphFormat(decoder->GetFormat());

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("SetAUGraphFormat failed: %i"), result);
#endif
	}

	// Allocate enough space in the ring buffer for the new format
	mRingBuffer->Allocate(decoder->GetFormat().mChannelsPerFrame,
						  decoder->GetFormat().mBytesPerFrame,
						  RING_BUFFER_SIZE_FRAMES);

	return true;
}

bool AudioPlayer::Enqueue(AudioDecoder *decoder)
{
	assert(NULL != decoder);
	
	/*
	 AudioStreamBasicDescription		format				= [self format];
	 AudioStreamBasicDescription		nextFormat			= [decoder format];
	 
	 AudioChannelLayout				channelLayout		= [self channelLayout];
	 AudioChannelLayout				nextChannelLayout	= [decoder channelLayout];
	 
	 BOOL	formatsMatch			= (nextFormat.mSampleRate == format.mSampleRate && nextFormat.mChannelsPerFrame == format.mChannelsPerFrame);
	 BOOL	channelLayoutsMatch		= channelLayoutsAreEqual(&nextChannelLayout, &channelLayout);
	 
	 // The two files can be joined only if they have the same formats and channel layouts
	 if(NO == formatsMatch || NO == channelLayoutsMatch)
	 return NO;
	 */
	
	return false;
}

#pragma mark Callbacks

OSStatus  AudioPlayer::Render(AudioUnitRenderActionFlags		*ioActionFlags,
							  const AudioTimeStamp				*inTimeStamp,
							  UInt32							inBusNumber,
							  UInt32							inNumberFrames,
							  AudioBufferList					*ioData)
{

#pragma unused(ioActionFlags)
#pragma unused(inTimeStamp)
#pragma unused(inBusNumber)
	
	assert(NULL != ioData);
	
//	UInt32 framesRead = d->ReadAudio(ioData, inNumberFrames);
//#if DEBUG
//	CFLog(CFSTR("AudioPlayer::Render rendered %i/%i"), framesRead, inNumberFrames);
//#endif
//	
//	if(0 == framesRead)
//		*ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
	
	CARingBufferError rbResult = mRingBuffer->Fetch(ioData, inNumberFrames, mFramesRendered, false);
	if(kCARingBufferError_OK != rbResult) {
#if DEBUG
		CFLog(CFSTR("AudioPlayer::Render error: CARingBuffer::Fetch() failed (%i)"), rbResult);
#endif
		return ioErr;
	}

	mFramesRendered += inNumberFrames;
	
	return noErr;
}

#pragma mark AUGraph Utilities

OSStatus AudioPlayer::CreateAUGraph()
{
	OSStatus result = NewAUGraph(&mAUGraph);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("NewAUGraph failed: %i"), result);
#endif
		return result;
	}
	
	// The graph will look like:
	// Peak Limiter -> Effects -> Output
	ComponentDescription desc;

	// Set up the peak limiter node
	desc.componentType			= kAudioUnitType_Effect;
	desc.componentSubType		= kAudioUnitSubType_PeakLimiter;
	desc.componentManufacturer	= kAudioUnitManufacturer_Apple;
	desc.componentFlags			= 0;
	desc.componentFlagsMask		= 0;
	
	result = AUGraphAddNode(mAUGraph, &desc, &mLimiterNode);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphAddNode failed: %i"), result);
#endif
		return result;
	}
	
	// Set up the output node
	desc.componentType			= kAudioUnitType_Output;
	desc.componentSubType		= kAudioUnitSubType_DefaultOutput;
	desc.componentManufacturer	= kAudioUnitManufacturer_Apple;
	desc.componentFlags			= 0;
	desc.componentFlagsMask		= 0;
	
	result = AUGraphAddNode(mAUGraph, &desc, &mOutputNode);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphAddNode failed: %i"), result);
#endif
		return result;
	}
	
	result = AUGraphConnectNodeInput(mAUGraph, mLimiterNode, 0, mOutputNode, 0);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphConnectNodeInput failed: %i"), result);
#endif
		return result;
	}
	
	// Install the input callback
	AURenderCallbackStruct cbs = { myAURenderCallback, this };
	result = AUGraphSetNodeInputCallback(mAUGraph, mLimiterNode, 0, &cbs);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphSetNodeInputCallback failed: %i"), result);
#endif
		return result;
	}
	
	// Open the graph
	result = AUGraphOpen(mAUGraph);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphOpen failed: %i"), result);
#endif
		return result;
	}
	
	// Initialize the graph
	result = AUGraphInitialize(mAUGraph);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphInitialize failed: %i"), result);
#endif
		return result;
	}
	
	// TODO: Install a render callback on the output node for more accurate tracking?
	
	return noErr;
}

OSStatus AudioPlayer::DisposeAUGraph()
{
	Boolean graphIsRunning = FALSE;
	OSStatus result = AUGraphIsRunning(mAUGraph, &graphIsRunning);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphIsRunning failed: %i"), result);
#endif
		return result;
	}
	
	if(graphIsRunning) {
		result = AUGraphStop(mAUGraph);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphStop failed: %i"), result);
#endif
			return result;
		}
	}
	
	Boolean graphIsInitialized = FALSE;	
	result = AUGraphIsInitialized(mAUGraph, &graphIsInitialized);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphIsInitialized failed: %i"), result);
#endif
		return result;
	}
	
	if(graphIsInitialized) {
		result = AUGraphUninitialize(mAUGraph);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphUninitialize failed: %i"), result);
#endif
			return result;
		}
	}
	
	result = AUGraphClose(mAUGraph);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphClose failed: %i"), result);
#endif
		return result;
	}
	
	result = ::DisposeAUGraph(mAUGraph);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("DisposeAUGraph failed: %i"), result);
#endif
		return result;
	}
	
	mAUGraph = NULL;
	
	return noErr;
}

OSStatus AudioPlayer::ResetAUGraph()
{
	UInt32 nodeCount = 0;
	OSStatus result = AUGraphGetNodeCount(mAUGraph, &nodeCount);
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphGetNodeCount failed: %i"), result);
#endif
		return result;
	}
	
	for(UInt32 i = 0; i < nodeCount; ++i) {
		AUNode node = 0;
		result = AUGraphGetIndNode(mAUGraph, i, &node);
		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphGetIndNode failed: %i"), result);
#endif
			return result;
		}
		
		AudioUnit au = NULL;
		result = AUGraphNodeInfo(mAUGraph, node, NULL, &au);
		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphNodeInfo failed: %i"), result);
#endif
			return result;
		}
		
		result = AudioUnitReset(au, kAudioUnitScope_Global, 0);
		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AudioUnitReset failed: %i"), result);
#endif
			return result;
		}
	}
	
	return noErr;
}

Float64 AudioPlayer::GetAUGraphLatency()
{
	Float64 graphLatency = 0;
	UInt32 nodeCount = 0;
	OSStatus result = AUGraphGetNodeCount(mAUGraph, &nodeCount);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphGetNodeCount failed: %i"), result);
#endif
		return -1;
	}
	
	for(UInt32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
		AUNode node = 0;
		result = AUGraphGetIndNode(mAUGraph, nodeIndex, &node);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphGetIndNode failed: %i"), result);
#endif
			return -1;
		}
		
		AudioUnit au = NULL;
		result = AUGraphNodeInfo(mAUGraph, node, NULL, &au);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphNodeInfo failed: %i"), result);
#endif
			return -1;
		}
		
		Float64 latency = 0;
		UInt32 dataSize = sizeof(latency);
		result = AudioUnitGetProperty(au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &dataSize);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AudioUnitGetProperty (kAudioUnitProperty_Latency) failed: %i"), result);
#endif
			return -1;
		}
		
		graphLatency += latency;
	}
	
	return graphLatency;
}

Float64 AudioPlayer::GetAUGraphTailTime()
{
	Float64 graphTailTime = 0;
	UInt32 nodeCount = 0;
	OSStatus result = AUGraphGetNodeCount(mAUGraph, &nodeCount);
	
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphGetNodeCount failed: %i"), result);
#endif
		return -1;
	}
	
	for(UInt32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
		AUNode node = 0;
		result = AUGraphGetIndNode(mAUGraph, nodeIndex, &node);
		
		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphGetIndNode failed: %i"), result);
#endif
			return -1;
		}
		
		AudioUnit au = NULL;
		result = AUGraphNodeInfo(mAUGraph, node, NULL, &au);
		
		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphNodeInfo failed: %i"), result);
#endif
			return -1;
		}
		
		Float64 tailTime = 0;
		UInt32 dataSize = sizeof(tailTime);
		result = AudioUnitGetProperty(au, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &tailTime, &dataSize);
		
		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AudioUnitGetProperty (kAudioUnitProperty_TailTime) failed: %i"), result);
#endif
			return -1;
		}
		
		graphTailTime += tailTime;
	}
	
	return graphTailTime;
}

OSStatus AudioPlayer::SetPropertyOnAUGraphNodes(AudioUnitPropertyID propertyID, const void *propertyData, UInt32 propertyDataSize)
{
	assert(NULL != propertyData);
	assert(0 < propertyDataSize);
	
	UInt32 nodeCount = 0;
	OSStatus result = AUGraphGetNodeCount(mAUGraph, &nodeCount);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphGetNodeCount failed: %i"), result);
#endif
		return result;
	}
	
	// Iterate through the nodes and attempt to set the property
	for(UInt32 i = 0; i < nodeCount; ++i) {
		AUNode node;
		result = AUGraphGetIndNode(mAUGraph, i, &node);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphGetIndNode failed: %i"), result);
#endif
			return result;
		}
		
		AudioUnit au = NULL;
		result = AUGraphNodeInfo(mAUGraph, node, NULL, &au);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphNodeInfo failed: %i"), result);
#endif
			return result;
		}
		
		if(mOutputNode == node) {
			// For AUHAL as the output node, you can't set the device side, so just set the client side
			result = AudioUnitSetProperty(au, propertyID, kAudioUnitScope_Input, 0, propertyData, propertyDataSize);

			if(noErr != result) {
#if DEBUG
				CFLog(CFSTR("AudioUnitSetProperty (%i) failed: %i"), propertyID, result);
#endif
				return result;
			}
			
// IO must be enabled for this to work
/*			err = AudioUnitSetProperty(au, propertyID, kAudioUnitScope_Output, 1, propertyData, propertyDataSize);

			if(noErr != err)
				return err;*/
		}
		else {
			UInt32 elementCount = 0;
			UInt32 dataSize = sizeof(elementCount);
			result = AudioUnitGetProperty(au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0, &elementCount, &dataSize);

			if(noErr != result) {
#if DEBUG
				CFLog(CFSTR("AudioUnitGetProperty (kAudioUnitProperty_ElementCount) failed: %i"), result);
#endif
				return result;
			}
			
			for(UInt32 j = 0; j < elementCount; ++j) {
/*				Boolean writable;
				err = AudioUnitGetPropertyInfo(au, propertyID, kAudioUnitScope_Input, j, &dataSize, &writable);

				if(noErr != err && kAudioUnitErr_InvalidProperty != err)
					return err;
				 
				if(kAudioUnitErr_InvalidProperty == err || !writable)
					continue;*/
				
				result = AudioUnitSetProperty(au, propertyID, kAudioUnitScope_Input, j, propertyData, propertyDataSize);

				if(noErr != result) {
#if DEBUG
					CFLog(CFSTR("AudioUnitSetProperty (%i) failed: %i"), propertyID, result);
#endif
					return result;
				}
			}
			
			elementCount = 0;
			dataSize = sizeof(elementCount);
			result = AudioUnitGetProperty(au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Output, 0, &elementCount, &dataSize);

			if(noErr != result) {
#if DEBUG
				CFLog(CFSTR("AudioUnitGetProperty (kAudioUnitProperty_ElementCount) failed: %i"), result);
#endif
				return result;
			}
			
			for(UInt32 j = 0; j < elementCount; ++j) {
/*				Boolean writable;
				err = AudioUnitGetPropertyInfo(au, propertyID, kAudioUnitScope_Output, j, &dataSize, &writable);

				if(noErr != err && kAudioUnitErr_InvalidProperty != err)
					return err;
				 
				if(kAudioUnitErr_InvalidProperty == err || !writable)
					continue;*/
				
				result = AudioUnitSetProperty(au, propertyID, kAudioUnitScope_Output, j, propertyData, propertyDataSize);

				if(noErr != result) {
#if DEBUG
					CFLog(CFSTR("AudioUnitSetProperty (%i) failed: %i"), propertyID, result);
#endif
					return result;
				}
			}
		}
	}
	
	return noErr;
}

OSStatus AudioPlayer::SetAUGraphFormat(AudioStreamBasicDescription format)
{
	AUNodeInteraction *interactions = NULL;
	
	// ========================================
	// If the graph is running, stop it
	Boolean graphIsRunning = FALSE;
	OSStatus result = AUGraphIsRunning(mAUGraph, &graphIsRunning);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphIsRunning failed: %i"), result);
#endif
		return result;
	}
	
	if(graphIsRunning) {
		result = AUGraphStop(mAUGraph);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphStop failed: %i"), result);
#endif
			return result;
		}
	}
	
	// ========================================
	// If the graph is initialized, uninitialize it
	Boolean graphIsInitialized = FALSE;
	result = AUGraphIsInitialized(mAUGraph, &graphIsInitialized);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphIsInitialized failed: %i"), result);
#endif
		return result;
	}
	
	if(graphIsInitialized) {
		result = AUGraphUninitialize(mAUGraph);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphUninitialize failed: %i"), result);
#endif
			return result;
		}
	}
	
	// ========================================
	// Save the interaction information and then clear all the connections
	UInt32 interactionCount = 0;
	result = AUGraphGetNumberOfInteractions(mAUGraph, &interactionCount);

	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphGetNumberOfInteractions failed: %i"), result);
#endif
		return result;
	}
	
	interactions = static_cast<AUNodeInteraction *>(calloc(interactionCount, sizeof(AUNodeInteraction)));
	if(NULL == interactions)
		return memFullErr;
	
	for(UInt32 i = 0; i < interactionCount; ++i) {
		result = AUGraphGetInteractionInfo(mAUGraph, i, &interactions[i]);

		if(noErr != result) {
			free(interactions);
#if DEBUG
			CFLog(CFSTR("AUGraphGetInteractionInfo failed: %i"), result);
#endif
			return result;
		}
	}
	
	result = AUGraphClearConnections(mAUGraph);

	if(noErr != result) {
		free(interactions);
#if DEBUG
		CFLog(CFSTR("AUGraphClearConnections failed: %i"), result);
#endif
		return result;
	}
	
	// ========================================
	// Attempt to set the new stream format
	result = SetPropertyOnAUGraphNodes(kAudioUnitProperty_StreamFormat, &format, sizeof(format));

	if(noErr != result) {
		
		// If the new format could not be set, restore the old format to ensure a working graph
		OSStatus newErr = SetPropertyOnAUGraphNodes(kAudioUnitProperty_StreamFormat, &mAUGraphFormat, sizeof(mAUGraphFormat));

		if(noErr != newErr) {
#if DEBUG
			CFLog(CFSTR("AudioPlayer error: Unable to restore AUGraph format: %i"), newErr);
#endif
		}

		// Do not free connections here, so graph can be rebuilt
		result = newErr;
	}
	else
		mAUGraphFormat = format;

	
	// ========================================
	// Restore the graph's connections and input callbacks
	for(UInt32 i = 0; i < interactionCount; ++i) {
		switch(interactions[i].nodeInteractionType) {
				
			// Reestablish the connection
			case kAUNodeInteraction_Connection:
			{
				result = AUGraphConnectNodeInput(mAUGraph, 
												 interactions[i].nodeInteraction.connection.sourceNode, 
												 interactions[i].nodeInteraction.connection.sourceOutputNumber,
												 interactions[i].nodeInteraction.connection.destNode, 
												 interactions[i].nodeInteraction.connection.destInputNumber);

				if(noErr != result) {
#if DEBUG
					CFLog(CFSTR("AUGraphConnectNodeInput failed: %i"), result);
#endif
					free(interactions), interactions = NULL;
					return result;
				}
				
				break;
			}
				
			// Reestablish the input callback
			case kAUNodeInteraction_InputCallback:
			{
				result = AUGraphSetNodeInputCallback(mAUGraph, 
												  interactions[i].nodeInteraction.inputCallback.destNode, 
												  interactions[i].nodeInteraction.inputCallback.destInputNumber,
												  &interactions[i].nodeInteraction.inputCallback.cback);

				if(noErr != result) {
#if DEBUG
					CFLog(CFSTR("AUGraphSetNodeInputCallback failed: %i"), result);
#endif
					free(interactions), interactions = NULL;
					return result;
				}
				
				break;
			}				
		}
	}
	
	free(interactions), interactions = NULL;
	
	// If the graph was initialized, reinitialize it
	if(graphIsInitialized) {
		result = AUGraphInitialize(mAUGraph);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphIsRunning failed: %i"), result);
#endif
			return result;
		}
	}
	
	// If the graph was running, restart it
	if(graphIsRunning) {
		result = AUGraphStart(mAUGraph);

		if(noErr != result) {
#if DEBUG
			CFLog(CFSTR("AUGraphIsRunning failed: %i"), result);
#endif
			return result;
		}
	}
	
	return noErr;
}

OSStatus AudioPlayer::SetAUGraphChannelLayout(AudioChannelLayout /*channelLayout*/)
{
	/*
	 // Attempt to set the new channel layout
	 //	OSStatus err = [self setPropertyOnAUGraphNodes:kAudioUnitProperty_AudioChannelLayout data:&channelLayout dataSize:sizeof(channelLayout)];
	 OSStatus err = AudioUnitSetProperty(_outputUnit, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Input, 0, &channelLayout, sizeof(channelLayout));

	 if(noErr != err) {
	 // If the new format could not be set, restore the old format to ensure a working graph
	 channelLayout = [self channelLayout];
	 //		OSStatus newErr = [self setPropertyOnAUGraphNodes:kAudioUnitProperty_AudioChannelLayout data:&channelLayout dataSize:sizeof(channelLayout)];
	 OSStatus newErr = AudioUnitSetProperty(_outputUnit, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Input, 0, &channelLayout, sizeof(channelLayout));

	 if(noErr != newErr)
	 NSLog(@"AudioPlayer error: Unable to restore AUGraph channel layout: %i", newErr);
	 
	 return err;
	 }
	 */
	return noErr;
}

bool AudioPlayer::EnablePreGain(UInt32 flag)
{
	if(flag && PreGainIsEnabled())
		return true;
	else if(!flag && false == PreGainIsEnabled())
		return true;
	
	AudioUnit au = NULL;
	OSStatus result = AUGraphNodeInfo(mAUGraph, 
									  mLimiterNode, 
									  NULL, 
									  &au);
	
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphNodeInfo failed: %i"), result);
#endif
		return false;
	}
	
	result = AudioUnitSetProperty(au, 
								  kAudioUnitProperty_BypassEffect,
								  kAudioUnitScope_Global, 
								  0, 
								  &flag, 
								  sizeof(flag));
	
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AudioUnitSetProperty (kAudioUnitProperty_BypassEffect) failed: %i"), result);
#endif
		return false;
	}
	
	return true;
}

bool AudioPlayer::PreGainIsEnabled()
{
	AudioUnit au = NULL;
	OSStatus result = AUGraphNodeInfo(mAUGraph, 
									  mLimiterNode, 
									  NULL, 
									  &au);
	
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphNodeInfo failed: %i"), result);
#endif
		return false;
	}
	
	UInt32 bypassed	= FALSE;
	UInt32 dataSize	= sizeof(bypassed);
	
	result = AudioUnitGetProperty(au, 
								  kAudioUnitProperty_BypassEffect, 
								  kAudioUnitScope_Global, 
								  0,
								  &bypassed,
								  &dataSize);
	
	if(noErr != result) {
#if DEBUG
		CFLog(CFSTR("AUGraphNodeInfo failed: %i"), result);
#endif
		return false;
	}
	
	return bypassed;
}
