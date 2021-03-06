/*
 * Copyright (c) 1999-2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Portions Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 2.0 (the "License").  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON- INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 *
 * Copyright 1993 NeXT, Inc. All rights reserved.
 *
 * Refactorizing done by Master Chief in 2009 and DHP in 2010/2011. 
 */

#include "boot.h"
#include "vbe.h"
#include "bootstruct.h"

//==============================================================================

bool inGraphicsMode(void)
{
    return (bootArgs->Video.v_display == GRAPHICS_MODE);
}


//==============================================================================
// Return the VESA mode matching the specified properties.
// If a matching mode cannot be found, then return the "best" available mode.

static unsigned short getVESAModeWithProperties(unsigned short width, unsigned short height, unsigned char bitsPerPixel,
												unsigned short attributesSet, unsigned short attributesClear,
												VBEModeInfoBlock * outModeInfo, unsigned short * vesaVersion)
{
	VBEInfoBlock     vbeInfo;
	unsigned short * modePtr;
	VBEModeInfoBlock modeInfo;
	unsigned char    modeBitsPerPixel;
	unsigned short   matchedMode = modeEndOfList;
	int              status;

	// Clear output mode info.

	bzero(outModeInfo, sizeof(*outModeInfo));

	// Get VBE controller info containing the list of supported modes.

	bzero(&vbeInfo, sizeof(vbeInfo));
	strcpy((char*)&vbeInfo, "VBE2");
	status = getVBEInfo(&vbeInfo);

	if (status != EFI_SUCCESS)
	{
		return modeEndOfList;
	}

	// Report the VESA major/minor version number.

	if (vesaVersion)
	{
		*vesaVersion = vbeInfo.VESAVersion;
	}

	// Loop through the mode list, and find the matching mode.

	for (modePtr = VBEDecodeFP(unsigned short *, vbeInfo.VideoModePtr); *modePtr != modeEndOfList; modePtr++)
	{
		// Get mode information.

		bzero(&modeInfo, sizeof(modeInfo));
		status = getVBEModeInfo(*modePtr, &modeInfo);

		if (status != EFI_SUCCESS)
		{
			continue;
		}
	
#if DEBUG
		printf("Mode %x: %dx%dx%d mm:%d attr:%x\n", * modePtr, modeInfo.XResolution, modeInfo.YResolution, modeInfo.BitsPerPixel, 
													modeInfo.MemoryModel, modeInfo.ModeAttributes);
		sleep(1);
#endif

		// Filter out unwanted modes based on mode attributes.

		if (((modeInfo.ModeAttributes & attributesSet) != attributesSet) || ((modeInfo.ModeAttributes & attributesClear) != 0))
		{
			continue;
		}

		// Pixel depth in bits.
	
		modeBitsPerPixel = modeInfo.BitsPerPixel;

		if ((modeBitsPerPixel == 4) && (modeInfo.MemoryModel == 0))
		{
			// Text mode, 16 colors.
		}
		else if ((modeBitsPerPixel == 8) && (modeInfo.MemoryModel == 4))
		{
			// Packed pixel, 256 colors.
		}
		else if (((modeBitsPerPixel == 16) || (modeBitsPerPixel == 15)) && (modeInfo.MemoryModel == 6) && (modeInfo.RedMaskSize == 5) && 
				 (modeInfo.GreenMaskSize == 5) && (modeInfo.BlueMaskSize  == 5))
		{
			// Direct color, 16 bpp (1:5:5:5).
			modeInfo.BitsPerPixel = modeBitsPerPixel = 16;
		}
		else if ((modeBitsPerPixel == 32) && (modeInfo.MemoryModel == 6) && (modeInfo.RedMaskSize == 8) && 
				 (modeInfo.GreenMaskSize == 8) && (modeInfo.BlueMaskSize  == 8))
		{
			// Direct color, 32 bpp (8:8:8:8).
		}
		else
		{
			continue;	// Not a supported mode.
		}

		// Modes larger than the specified dimensions are skipped.

		if ((modeInfo.XResolution > width) || (modeInfo.YResolution > height))
		{
			continue;
		}

        // Perfect match, we're done looking.

		if (modeInfo.XResolution == width && modeInfo.YResolution == height && modeBitsPerPixel == bitsPerPixel)
		{
			matchedMode = *modePtr;
			bcopy(&modeInfo, outModeInfo, sizeof(modeInfo));
			break;
		}

		// Save the next "best" mode in case a perfect match is not found.

		if (modeInfo.XResolution == outModeInfo->XResolution && modeInfo.YResolution == outModeInfo->YResolution &&
			modeBitsPerPixel <= outModeInfo->BitsPerPixel)
		{
			continue;	// Saved mode has more depth.
		}

		if (modeInfo.XResolution < outModeInfo->XResolution || modeInfo.YResolution < outModeInfo->YResolution ||
			modeBitsPerPixel < outModeInfo->BitsPerPixel)
		{
			continue;	// Saved mode has more resolution.
		}

		matchedMode = *modePtr;
		bcopy(&modeInfo, outModeInfo, sizeof(modeInfo));
	}

	return matchedMode;
}


//==============================================================================

static int setVESATextMode(unsigned short cols, unsigned short rows, unsigned char bitsPerPixel)
{
	VBEModeInfoBlock	minfo;
	unsigned short	mode = modeEndOfList;

	if ((cols != 80) || (rows != 25))  // not 80x25 mode
	{
		mode = getVESAModeWithProperties(cols, rows, bitsPerPixel, maColorModeBit | maModeIsSupportedBit, maGraphicsModeBit, &minfo, NULL);
	}

#if DEBUG
	printf("\nsetVESATextMode: %d", mode);
	getc();
#endif

	if ((mode == modeEndOfList) || (setVBEMode(mode, NULL) != EFI_SUCCESS))
	{
		video_mode(2);  // VGA BIOS, 80x25 text mode.
		minfo.XResolution = 80;
		minfo.YResolution = 25;
	}

	// Update bootArgs with the data provided by the selected VESA mode.

	bootArgs->Video.v_display  = VGA_TEXT_MODE;
	bootArgs->Video.v_width    = minfo.XResolution;
	bootArgs->Video.v_height   = minfo.YResolution;
	bootArgs->Video.v_depth    = 8;
	bootArgs->Video.v_baseAddr = 0xb8000;
	bootArgs->Video.v_rowBytes = 0x8000;

	return EFI_SUCCESS;  // always return success
}


//==============================================================================

long setVESAGraphicsMode(unsigned short width, unsigned short height, unsigned char  bitsPerPixel, unsigned short refreshRate)
{
	VBEModeInfoBlock	minfo;

	unsigned short	mode = 280; // Default to 1024 * 768 * 32 (1920 * 1200 * 32 would be 330)
	unsigned short	vesaVersion;

	long status = (long)EFI_UNSUPPORTED;

	do
	{
		mode = getVESAModeWithProperties(width, height, bitsPerPixel, 
										 maColorModeBit | maModeIsSupportedBit | maGraphicsModeBit | maLinearFrameBufferAvailBit, 
										 0, &minfo, &vesaVersion);
#if DEBUG
		printf("\nsetVESAGraphicsMode: %d\n%d\n%d\n%d\n%d\n%x\n%x\nPress a key...", mode, minfo.XResolution, 
			   minfo.YResolution, minfo.BitsPerPixel, minfo.BytesPerScanline, minfo.PhysBasePtr_low, minfo.PhysBasePtr_high);
		getc();
#endif

		if (mode == modeEndOfList)
		{
			break;
		}

		status = setVBEMode(mode | kLinearFrameBufferBit, NULL);

		if (status != EFI_SUCCESS)
		{
			break;
		}

		// Is this required for buggy Video BIOS implementations? If so for which adapter?
		
		if (minfo.BytesPerScanline == 0)
		{
			minfo.BytesPerScanline = (minfo.XResolution * minfo.BitsPerPixel) >> 3; // ((1920 * 32 = 61440) >> 3) = 7680
		}

		// Update bootArgs with the data provided by the selected VESA mode.
		bootArgs->Video.v_display  = GRAPHICS_MODE;
		bootArgs->Video.v_width    = minfo.XResolution; // 1920 or 1600
		bootArgs->Video.v_height   = minfo.YResolution; // 1200 or 900
		bootArgs->Video.v_depth    = minfo.BitsPerPixel; // 32
		bootArgs->Video.v_rowBytes = minfo.BytesPerScanline; // 7680 or 6400

		bootArgs->Video.v_baseAddr = VBEMakeUInt32(minfo.PhysBasePtr);
	}
	while (0);

	return status;
}


//==============================================================================

static int getNumberArrayFromProperty(const char *  propKey, unsigned long numbers[], unsigned long maxArrayCount)
{
	char			* propStr;
	unsigned long	count = 0;

	#define _isdigit(c) ((c) >= '0' && (c) <= '9')

	propStr = newStringForKey((char *) propKey , &bootInfo->bootConfig);

	if (propStr)
	{
		char * delimiter = propStr;
		char * p = propStr;
		unsigned long val = 0;

		while (count < maxArrayCount && *p != '\0')
		{
			val = strtoul(p, &delimiter, 10);

			if (p != delimiter)
			{
				numbers[count++] = val;
				p = delimiter;
			}

			while ((*p != '\0') && !_isdigit(*p))
			{
				p++;
			}
		}

		free(propStr);
	}

	return count;
}


//==============================================================================

long initGraphicsMode()
{
	unsigned long params[4];

	int count = getNumberArrayFromProperty(kGraphicsModeKey, params, 4);

#if DEBUG
	printf("\nwidth: %d  height: %d  depth: %d\n", params[0], params[1], params[2]);
#endif

	//  Do we have a "Graphics Mode" property in com.apple.Boot.plist?
	if (count < 3)
	{
#if USE_STATIC_DISPLAY_RESOLUTION
		params[0] = DEFAULT_SCREEN_WIDTH;
		params[1] = DEFAULT_SCREEN_HEIGHT;
#else
		unsigned long resolutionCombo = getResolutionFromEDID();
		printf("EDID width :%l\n", resolutionCombo);
		/* params[0] = */ printf("EDID width :%l\n", (resolutionCombo << 16));
		/* params[1] = */ printf("EDID height:%l\n", (resolutionCombo & 0xFFF));
		// sleep(15);
#endif
	}

	params[2] = 32; // Don't bother. Use the de facto standard.
	params[3] = 0;

	return setVESAGraphicsMode(params[0], params[1], params[2], 0);
}


//==============================================================================

void setVideoMode(int mode)
{
	long status = EFI_SUCCESS;

	unsigned long params[4];

	if (mode == GRAPHICS_MODE)
	{
		if ((status = initGraphicsMode()) == EFI_SUCCESS)
		{
			bootArgs->Video.v_display = (gVerboseMode) ? FB_TEXT_MODE : GRAPHICS_MODE;
		}
	}

	if (mode == VGA_TEXT_MODE || status != EFI_SUCCESS)
	{
		params[0] = 80;  // Default text mode is 80x25.
		params[1] = 25;

		setVESATextMode(params[0], params[1], 4);
		bootArgs->Video.v_display = VGA_TEXT_MODE;
	}
}
