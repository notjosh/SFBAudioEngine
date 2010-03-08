/*
 *  Copyright (C) 2006, 2007, 2008, 2009, 2010 Stephen F. Booth <me@sbooth.org>
 *  All Rights Reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    - Neither the name of Stephen F. Booth nor the names of its 
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <taglib/mpcfile.h>
#include <taglib/tag.h>

#include "AudioEngineDefines.h"
#include "MusepackMetadata.h"
#include "CreateDisplayNameForURL.h"
#include "TagLibStringFromCFString.h"


#pragma mark Static Methods


CFArrayRef MusepackMetadata::CreateSupportedFileExtensions()
{
	CFStringRef supportedExtensions [] = { CFSTR("mpc") };
	return CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void **>(supportedExtensions), 1, &kCFTypeArrayCallBacks);
}

CFArrayRef MusepackMetadata::CreateSupportedMIMETypes()
{
	CFStringRef supportedMIMETypes [] = { CFSTR("audio/musepack") };
	return CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void **>(supportedMIMETypes), 1, &kCFTypeArrayCallBacks);
}

bool MusepackMetadata::HandlesFilesWithExtension(CFStringRef extension)
{
	assert(NULL != extension);
	
	if(kCFCompareEqualTo == CFStringCompare(extension, CFSTR("mpc"), kCFCompareCaseInsensitive))
		return true;

	return false;
}

bool MusepackMetadata::HandlesMIMEType(CFStringRef mimeType)
{
	assert(NULL != mimeType);	
	
	if(kCFCompareEqualTo == CFStringCompare(mimeType, CFSTR("audio/mpeg"), kCFCompareCaseInsensitive))
		return true;
	
	return false;
}


#pragma mark Creation and Destruction


MusepackMetadata::MusepackMetadata(CFURLRef url)
	: AudioMetadata(url)
{}

MusepackMetadata::~MusepackMetadata()
{}


#pragma mark Functionality


bool MusepackMetadata::ReadMetadata(CFErrorRef *error)
{
	// Start from scratch
	CFDictionaryRemoveAllValues(mMetadata);
	
	UInt8 buf [PATH_MAX];
	if(false == CFURLGetFileSystemRepresentation(mURL, false, buf, PATH_MAX))
		return false;
	
	TagLib::MPC::File file(reinterpret_cast<const char *>(buf), false);
	
	if(!file.isValid()) {
		if(NULL != error) {
			CFMutableDictionaryRef errorDictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 
																			   32,
																			   &kCFTypeDictionaryKeyCallBacks,
																			   &kCFTypeDictionaryValueCallBacks);
			
			CFStringRef displayName = CreateDisplayNameForURL(mURL);
			CFStringRef errorString = CFStringCreateWithFormat(kCFAllocatorDefault, 
															   NULL, 
															   CFCopyLocalizedString(CFSTR("The file “%@” is not a valid Musepack file."), ""), 
															   displayName);
			
			CFDictionarySetValue(errorDictionary, 
								 kCFErrorLocalizedDescriptionKey, 
								 errorString);
			
			CFDictionarySetValue(errorDictionary, 
								 kCFErrorLocalizedFailureReasonKey, 
								 CFCopyLocalizedString(CFSTR("Not a Musepack file"), ""));
			
			CFDictionarySetValue(errorDictionary, 
								 kCFErrorLocalizedRecoverySuggestionKey, 
								 CFCopyLocalizedString(CFSTR("The file's extension may not match the file's type."), ""));
			
			CFRelease(errorString), errorString = NULL;
			CFRelease(displayName), displayName = NULL;
			
			*error = CFErrorCreate(kCFAllocatorDefault, 
								   AudioMetadataErrorDomain, 
								   AudioMetadataInputOutputError, 
								   errorDictionary);
			
			CFRelease(errorDictionary), errorDictionary = NULL;				
		}
		
		return false;
	}

	// Album title
	if(!file.tag()->album().isNull()) {
		CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, file.tag()->album().toCString(true), kCFStringEncodingUTF8);
		SetAlbumTitle(str);
		CFRelease(str), str = NULL;
	}
	
	// Artist
	if(!file.tag()->artist().isNull()) {
		CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, file.tag()->artist().toCString(true), kCFStringEncodingUTF8);
		SetArtist(str);
		CFRelease(str), str = NULL;
	}
	
	// Genre
	if(!file.tag()->genre().isNull()) {
		CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, file.tag()->genre().toCString(true), kCFStringEncodingUTF8);
		SetGenre(str);
		CFRelease(str), str = NULL;
	}
	
	// Year
	if(file.tag()->year()) {
		CFStringRef str = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("%d"), file.tag()->year());
		SetReleaseDate(str);
		CFRelease(str), str = NULL;
	}
	
	// Comment
	if(!file.tag()->comment().isNull()) {
		CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, file.tag()->comment().toCString(true), kCFStringEncodingUTF8);
		SetComment(str);
		CFRelease(str), str = NULL;
	}
	
	// Track title
	if(!file.tag()->title().isNull()) {
		CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, file.tag()->title().toCString(true), kCFStringEncodingUTF8);
		SetTitle(str);
		CFRelease(str), str = NULL;
	}
	
	// Track number
	if(file.tag()->track()) {
		int trackNum = file.tag()->track();
		CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &trackNum);
		SetTrackNumber(num);
		CFRelease(num), num = NULL;
	}

	return true;
}

bool MusepackMetadata::WriteMetadata(CFErrorRef *error)
{
	UInt8 buf [PATH_MAX];
	if(false == CFURLGetFileSystemRepresentation(mURL, false, buf, PATH_MAX))
		return false;

	TagLib::MPC::File file(reinterpret_cast<const char *>(buf), false);
	
	if(!file.isValid()) {
		if(error) {
			CFMutableDictionaryRef errorDictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 
																			   32,
																			   &kCFTypeDictionaryKeyCallBacks,
																			   &kCFTypeDictionaryValueCallBacks);
			
			CFStringRef displayName = CreateDisplayNameForURL(mURL);
			CFStringRef errorString = CFStringCreateWithFormat(kCFAllocatorDefault, 
															   NULL, 
															   CFCopyLocalizedString(CFSTR("The file “%@” is not a valid Musepack file."), ""), 
															   displayName);
			
			CFDictionarySetValue(errorDictionary, 
								 kCFErrorLocalizedDescriptionKey, 
								 errorString);
			
			CFDictionarySetValue(errorDictionary, 
								 kCFErrorLocalizedFailureReasonKey, 
								 CFCopyLocalizedString(CFSTR("Not a Musepack file"), ""));
			
			CFDictionarySetValue(errorDictionary, 
								 kCFErrorLocalizedRecoverySuggestionKey, 
								 CFCopyLocalizedString(CFSTR("The file's extension may not match the file's type."), ""));
			
			CFRelease(errorString), errorString = NULL;
			CFRelease(displayName), displayName = NULL;
			
			*error = CFErrorCreate(kCFAllocatorDefault, 
								   AudioMetadataErrorDomain, 
								   AudioMetadataInputOutputError, 
								   errorDictionary);
			
			CFRelease(errorDictionary), errorDictionary = NULL;				
		}
		
		return false;
	}

	// Album title
	if(GetAlbumTitle())
		file.tag()->setAlbum(TagLib::StringFromCFString(GetAlbumTitle()));
	else
		file.tag()->setAlbum(TagLib::String::null);
	
	// Artist
	if(GetArtist())
		file.tag()->setArtist(TagLib::StringFromCFString(GetArtist()));
	else
		file.tag()->setArtist(TagLib::String::null);
	
	// Genre
	if(GetGenre())
		file.tag()->setGenre(TagLib::StringFromCFString(GetGenre()));
	else
		file.tag()->setGenre(TagLib::String::null);
	
	// Year
	if(GetReleaseDate())
		file.tag()->setYear(CFStringGetIntValue(GetReleaseDate()));
	else
		file.tag()->setYear(0);
	
	// Comment
	if(GetComment())
		file.tag()->setComment(TagLib::StringFromCFString(GetComment()));
	else
		file.tag()->setComment(TagLib::String::null);
	
	// Track title
	if(GetTitle())
		file.tag()->setTitle(TagLib::StringFromCFString(GetTitle()));
	else
		file.tag()->setTitle(TagLib::String::null);
	
	// Track number
	int trackNum = 0;
	if(GetTrackNumber())
		CFNumberGetValue(GetTrackNumber(), kCFNumberIntType, &trackNum);

	file.tag()->setTrack(trackNum);

	if(!file.save()) {
		if(error) {
			CFMutableDictionaryRef errorDictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 
																			   32,
																			   &kCFTypeDictionaryKeyCallBacks,
																			   &kCFTypeDictionaryValueCallBacks);
			
			CFStringRef displayName = CreateDisplayNameForURL(mURL);
			CFStringRef errorString = CFStringCreateWithFormat(kCFAllocatorDefault, 
															   NULL, 
															   CFCopyLocalizedString(CFSTR("The file “%@” is not a valid Musepack file."), ""), 
															   displayName);
			
			CFDictionarySetValue(errorDictionary, 
								 kCFErrorLocalizedDescriptionKey, 
								 errorString);
			
			CFDictionarySetValue(errorDictionary, 
								 kCFErrorLocalizedFailureReasonKey, 
								 CFCopyLocalizedString(CFSTR("Unable to write metadata"), ""));
			
			CFDictionarySetValue(errorDictionary, 
								 kCFErrorLocalizedRecoverySuggestionKey, 
								 CFCopyLocalizedString(CFSTR("The file's extension may not match the file's type."), ""));
			
			CFRelease(errorString), errorString = NULL;
			CFRelease(displayName), displayName = NULL;
			
			*error = CFErrorCreate(kCFAllocatorDefault, 
								   AudioMetadataErrorDomain, 
								   AudioMetadataInputOutputError, 
								   errorDictionary);
			
			CFRelease(errorDictionary), errorDictionary = NULL;				
		}
		
		return false;
	}
	
	return true;
}