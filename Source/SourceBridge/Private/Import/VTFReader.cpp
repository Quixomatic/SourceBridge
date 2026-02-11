#include "Import/VTFReader.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture2D.h"

// VTF header is packed (no alignment padding between fields)
#pragma pack(push, 1)
struct FVTFHeaderRaw
{
	char Signature[4];          // "VTF\0"
	uint32 VersionMajor;
	uint32 VersionMinor;
	uint32 HeaderSize;
	uint16 Width;
	uint16 Height;
	uint32 Flags;
	uint16 Frames;
	uint16 FirstFrame;
	uint8 Padding0[4];
	float Reflectivity[3];
	uint8 Padding1[4];
	float BumpmapScale;
	uint32 HighResImageFormat;
	uint8 MipmapCount;
	uint32 LowResImageFormat;
	uint8 LowResImageWidth;
	uint8 LowResImageHeight;
	// v7.2+
	uint16 Depth;
};
#pragma pack(pop)

int32 FVTFReader::CalcImageSize(uint32 Format, int32 Width, int32 Height)
{
	// Minimum 1x1
	Width = FMath::Max(Width, 1);
	Height = FMath::Max(Height, 1);

	switch (Format)
	{
	case VTF_DXT1:
		return FMath::Max(Width / 4, 1) * FMath::Max(Height / 4, 1) * 8;
	case VTF_DXT3:
	case VTF_DXT5:
		return FMath::Max(Width / 4, 1) * FMath::Max(Height / 4, 1) * 16;
	case VTF_RGBA8888:
	case VTF_ABGR8888:
	case VTF_BGRA8888:
		return Width * Height * 4;
	case VTF_RGB888:
	case VTF_BGR888:
		return Width * Height * 3;
	case VTF_I8:
		return Width * Height;
	default:
		return 0;
	}
}

bool FVTFReader::ConvertToBGRA8(const uint8* Src, int32 SrcSize, uint32 SrcFormat,
	int32 Width, int32 Height, TArray<uint8>& OutBGRA)
{
	int32 PixelCount = Width * Height;
	OutBGRA.SetNumUninitialized(PixelCount * 4);

	switch (SrcFormat)
	{
	case VTF_BGRA8888:
		if (SrcSize < PixelCount * 4) return false;
		FMemory::Memcpy(OutBGRA.GetData(), Src, PixelCount * 4);
		return true;

	case VTF_RGBA8888:
		if (SrcSize < PixelCount * 4) return false;
		for (int32 i = 0; i < PixelCount; i++)
		{
			OutBGRA[i * 4 + 0] = Src[i * 4 + 2]; // B <- R
			OutBGRA[i * 4 + 1] = Src[i * 4 + 1]; // G
			OutBGRA[i * 4 + 2] = Src[i * 4 + 0]; // R <- B
			OutBGRA[i * 4 + 3] = Src[i * 4 + 3]; // A
		}
		return true;

	case VTF_ABGR8888:
		if (SrcSize < PixelCount * 4) return false;
		for (int32 i = 0; i < PixelCount; i++)
		{
			OutBGRA[i * 4 + 0] = Src[i * 4 + 3]; // B
			OutBGRA[i * 4 + 1] = Src[i * 4 + 2]; // G
			OutBGRA[i * 4 + 2] = Src[i * 4 + 1]; // R
			OutBGRA[i * 4 + 3] = Src[i * 4 + 0]; // A
		}
		return true;

	case VTF_RGB888:
		if (SrcSize < PixelCount * 3) return false;
		for (int32 i = 0; i < PixelCount; i++)
		{
			OutBGRA[i * 4 + 0] = Src[i * 3 + 2]; // B
			OutBGRA[i * 4 + 1] = Src[i * 3 + 1]; // G
			OutBGRA[i * 4 + 2] = Src[i * 3 + 0]; // R
			OutBGRA[i * 4 + 3] = 255;             // A
		}
		return true;

	case VTF_BGR888:
		if (SrcSize < PixelCount * 3) return false;
		for (int32 i = 0; i < PixelCount; i++)
		{
			OutBGRA[i * 4 + 0] = Src[i * 3 + 0]; // B
			OutBGRA[i * 4 + 1] = Src[i * 3 + 1]; // G
			OutBGRA[i * 4 + 2] = Src[i * 3 + 2]; // R
			OutBGRA[i * 4 + 3] = 255;             // A
		}
		return true;

	case VTF_I8:
		if (SrcSize < PixelCount) return false;
		for (int32 i = 0; i < PixelCount; i++)
		{
			OutBGRA[i * 4 + 0] = Src[i];
			OutBGRA[i * 4 + 1] = Src[i];
			OutBGRA[i * 4 + 2] = Src[i];
			OutBGRA[i * 4 + 3] = 255;
		}
		return true;

	default:
		return false;
	}
}

UTexture2D* FVTFReader::LoadVTF(const FString& FilePath)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("VTFReader: Failed to read file: %s"), *FilePath);
		return nullptr;
	}

	return LoadVTFFromMemory(FileData, FilePath);
}

UTexture2D* FVTFReader::LoadVTFFromMemory(const TArray<uint8>& FileData, const FString& DebugName)
{
	if (FileData.Num() < (int32)sizeof(FVTFHeaderRaw))
	{
		UE_LOG(LogTemp, Warning, TEXT("VTFReader: Data too small: %s"), *DebugName);
		return nullptr;
	}

	// Parse header
	const FVTFHeaderRaw* Header = reinterpret_cast<const FVTFHeaderRaw*>(FileData.GetData());

	if (FMemory::Memcmp(Header->Signature, "VTF", 3) != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("VTFReader: Invalid VTF signature: %s"), *DebugName);
		return nullptr;
	}

	int32 Width = Header->Width;
	int32 Height = Header->Height;
	uint32 Format = Header->HighResImageFormat;
	int32 MipCount = Header->MipmapCount;
	uint32 HeaderSize = Header->HeaderSize;

	if (Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096)
	{
		UE_LOG(LogTemp, Warning, TEXT("VTFReader: Invalid dimensions %dx%d: %s"), Width, Height, *DebugName);
		return nullptr;
	}

	// Calculate offset to the largest mipmap
	// Data layout after header:
	//   1. Low-res thumbnail (DXT1)
	//   2. High-res mipmaps from smallest to largest (last = full size)
	int32 LowResSize = CalcImageSize(Header->LowResImageFormat,
		Header->LowResImageWidth, Header->LowResImageHeight);

	// Sum all mipmaps except the largest to find its offset
	int32 MipDataOffset = HeaderSize + LowResSize;
	for (int32 Mip = MipCount - 1; Mip > 0; Mip--)
	{
		int32 MipW = FMath::Max(Width >> Mip, 1);
		int32 MipH = FMath::Max(Height >> Mip, 1);
		MipDataOffset += CalcImageSize(Format, MipW, MipH);
	}

	int32 FullMipSize = CalcImageSize(Format, Width, Height);
	if (FullMipSize == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("VTFReader: Unsupported format %d: %s"), Format, *DebugName);
		return nullptr;
	}

	if (MipDataOffset + FullMipSize > FileData.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("VTFReader: Data truncated (need %d, have %d): %s"),
			MipDataOffset + FullMipSize, FileData.Num(), *DebugName);
		return nullptr;
	}

	const uint8* MipData = FileData.GetData() + MipDataOffset;

	// Create UTexture2D based on format
	UTexture2D* Texture = nullptr;

	if (Format == VTF_DXT1)
	{
		Texture = UTexture2D::CreateTransient(Width, Height, PF_DXT1);
		if (!Texture) return nullptr;

		void* TexData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(TexData, MipData, FullMipSize);
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	}
	else if (Format == VTF_DXT3)
	{
		Texture = UTexture2D::CreateTransient(Width, Height, PF_DXT3);
		if (!Texture) return nullptr;

		void* TexData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(TexData, MipData, FullMipSize);
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	}
	else if (Format == VTF_DXT5)
	{
		Texture = UTexture2D::CreateTransient(Width, Height, PF_DXT5);
		if (!Texture) return nullptr;

		void* TexData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(TexData, MipData, FullMipSize);
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	}
	else
	{
		// Uncompressed format - convert to BGRA8888
		TArray<uint8> BGRA;
		if (!ConvertToBGRA8(MipData, FullMipSize, Format, Width, Height, BGRA))
		{
			UE_LOG(LogTemp, Warning, TEXT("VTFReader: Failed to convert format %d: %s"), Format, *DebugName);
			return nullptr;
		}

		Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture) return nullptr;

		void* TexData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(TexData, BGRA.GetData(), BGRA.Num());
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	}

	Texture->UpdateResource();
	Texture->Filter = TF_Bilinear;
	Texture->LODGroup = TEXTUREGROUP_World;

	return Texture;
}
