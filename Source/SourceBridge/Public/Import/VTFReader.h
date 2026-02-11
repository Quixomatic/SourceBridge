#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Reads Valve Texture Format (VTF) files and creates UTexture2D objects.
 * Handles DXT1, DXT5, BGRA8888, BGR888, RGB888, RGBA8888 formats.
 * VTF format spec: https://developer.valvesoftware.com/wiki/Valve_Texture_Format
 */
class SOURCEBRIDGE_API FVTFReader
{
public:
	/** Load a VTF file from disk and create a transient UTexture2D. Returns null on failure. */
	static UTexture2D* LoadVTF(const FString& FilePath);

private:
	// VTF image format IDs (from VTF spec)
	enum EVTFFormat : uint32
	{
		VTF_RGBA8888 = 0,
		VTF_ABGR8888 = 1,
		VTF_RGB888 = 2,
		VTF_BGR888 = 3,
		VTF_I8 = 5,
		VTF_BGRA8888 = 12,
		VTF_DXT1 = 13,
		VTF_DXT3 = 14,
		VTF_DXT5 = 15,
	};

	/** Get the byte size of image data for a given format and dimensions. */
	static int32 CalcImageSize(uint32 Format, int32 Width, int32 Height);

	/** Convert uncompressed pixel data to BGRA8888. */
	static bool ConvertToBGRA8(const uint8* Src, int32 SrcSize, uint32 SrcFormat,
		int32 Width, int32 Height, TArray<uint8>& OutBGRA);
};
