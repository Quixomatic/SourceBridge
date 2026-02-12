#include "Import/VTFReader.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture2D.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

bool FVTFReader::bDebugDumpTextures = false;
FString FVTFReader::DebugDumpPath;

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

	// Debug dump: save every loaded VTF as PNG
	if (bDebugDumpTextures)
	{
		TArray<uint8> DumpBGRA;
		bool bDecompressed = false;

		if (Format == VTF_DXT1)
		{
			bDecompressed = DecompressDXT1(MipData, Width, Height, DumpBGRA);
		}
		else if (Format == VTF_DXT3)
		{
			bDecompressed = DecompressDXT3(MipData, Width, Height, DumpBGRA);
		}
		else if (Format == VTF_DXT5)
		{
			bDecompressed = DecompressDXT5(MipData, Width, Height, DumpBGRA);
		}
		else
		{
			bDecompressed = ConvertToBGRA8(MipData, FullMipSize, Format, Width, Height, DumpBGRA);
		}

		if (bDecompressed)
		{
			if (DebugDumpPath.IsEmpty())
			{
				DebugDumpPath = FPaths::ProjectSavedDir() / TEXT("SourceBridge/Debug/Textures");
			}

			// Build filename from the debug name (strip path junk, keep material path structure)
			FString SafeName = DebugName.Replace(TEXT("\\"), TEXT("/"));
			// Try to extract just the material-relative path (e.g., "tools/toolstrigger")
			int32 MaterialsIdx;
			if (SafeName.FindLastChar('/', MaterialsIdx))
			{
				// Use last two path components as filename
				FString Remaining = SafeName;
				Remaining.ReplaceInline(TEXT("/"), TEXT("_"));
				SafeName = Remaining;
			}
			SafeName = SafeName.Replace(TEXT("/"), TEXT("_")).Replace(TEXT(":"), TEXT(""));

			FString PNGPath = DebugDumpPath / SafeName + TEXT(".png");
			SaveBGRAAsPNG(DumpBGRA, Width, Height, PNGPath);
			UE_LOG(LogTemp, Log, TEXT("VTFReader: Debug dump → %s (%dx%d, fmt=%d)"), *PNGPath, Width, Height, Format);
		}
	}

	return Texture;
}

// ---- DXT Decompression ----

static void DecodeDXTColor(uint16 Color565, uint8& R, uint8& G, uint8& B)
{
	R = ((Color565 >> 11) & 0x1F) * 255 / 31;
	G = ((Color565 >> 5) & 0x3F) * 255 / 63;
	B = (Color565 & 0x1F) * 255 / 31;
}

bool FVTFReader::DecompressDXT1(const uint8* Src, int32 Width, int32 Height, TArray<uint8>& OutBGRA)
{
	int32 BlocksX = FMath::Max(Width / 4, 1);
	int32 BlocksY = FMath::Max(Height / 4, 1);
	OutBGRA.SetNumZeroed(Width * Height * 4);

	for (int32 BY = 0; BY < BlocksY; BY++)
	{
		for (int32 BX = 0; BX < BlocksX; BX++)
		{
			const uint8* Block = Src + (BY * BlocksX + BX) * 8;

			uint16 C0 = Block[0] | (Block[1] << 8);
			uint16 C1 = Block[2] | (Block[3] << 8);
			uint32 LookupTable = Block[4] | (Block[5] << 8) | (Block[6] << 16) | (Block[7] << 24);

			uint8 R[4], G[4], B[4], A[4];
			DecodeDXTColor(C0, R[0], G[0], B[0]); A[0] = 255;
			DecodeDXTColor(C1, R[1], G[1], B[1]); A[1] = 255;

			if (C0 > C1)
			{
				R[2] = (2 * R[0] + R[1]) / 3; G[2] = (2 * G[0] + G[1]) / 3; B[2] = (2 * B[0] + B[1]) / 3; A[2] = 255;
				R[3] = (R[0] + 2 * R[1]) / 3; G[3] = (G[0] + 2 * G[1]) / 3; B[3] = (B[0] + 2 * B[1]) / 3; A[3] = 255;
			}
			else
			{
				R[2] = (R[0] + R[1]) / 2; G[2] = (G[0] + G[1]) / 2; B[2] = (B[0] + B[1]) / 2; A[2] = 255;
				R[3] = 0; G[3] = 0; B[3] = 0; A[3] = 0; // transparent black
			}

			for (int32 PY = 0; PY < 4; PY++)
			{
				for (int32 PX = 0; PX < 4; PX++)
				{
					int32 X = BX * 4 + PX;
					int32 Y = BY * 4 + PY;
					if (X >= Width || Y >= Height) continue;

					int32 Idx = (LookupTable >> ((PY * 4 + PX) * 2)) & 0x03;
					int32 Pixel = (Y * Width + X) * 4;
					OutBGRA[Pixel + 0] = B[Idx];
					OutBGRA[Pixel + 1] = G[Idx];
					OutBGRA[Pixel + 2] = R[Idx];
					OutBGRA[Pixel + 3] = A[Idx];
				}
			}
		}
	}
	return true;
}

bool FVTFReader::DecompressDXT3(const uint8* Src, int32 Width, int32 Height, TArray<uint8>& OutBGRA)
{
	int32 BlocksX = FMath::Max(Width / 4, 1);
	int32 BlocksY = FMath::Max(Height / 4, 1);
	OutBGRA.SetNumZeroed(Width * Height * 4);

	for (int32 BY = 0; BY < BlocksY; BY++)
	{
		for (int32 BX = 0; BX < BlocksX; BX++)
		{
			const uint8* Block = Src + (BY * BlocksX + BX) * 16;
			const uint8* AlphaBlock = Block;       // 8 bytes of explicit alpha (4-bit per pixel)
			const uint8* ColorBlock = Block + 8;    // 8 bytes DXT1 color block

			uint16 C0 = ColorBlock[0] | (ColorBlock[1] << 8);
			uint16 C1 = ColorBlock[2] | (ColorBlock[3] << 8);
			uint32 LookupTable = ColorBlock[4] | (ColorBlock[5] << 8) | (ColorBlock[6] << 16) | (ColorBlock[7] << 24);

			uint8 R[4], G[4], B[4];
			DecodeDXTColor(C0, R[0], G[0], B[0]);
			DecodeDXTColor(C1, R[1], G[1], B[1]);
			R[2] = (2 * R[0] + R[1]) / 3; G[2] = (2 * G[0] + G[1]) / 3; B[2] = (2 * B[0] + B[1]) / 3;
			R[3] = (R[0] + 2 * R[1]) / 3; G[3] = (G[0] + 2 * G[1]) / 3; B[3] = (B[0] + 2 * B[1]) / 3;

			for (int32 PY = 0; PY < 4; PY++)
			{
				for (int32 PX = 0; PX < 4; PX++)
				{
					int32 X = BX * 4 + PX;
					int32 Y = BY * 4 + PY;
					if (X >= Width || Y >= Height) continue;

					int32 Idx = (LookupTable >> ((PY * 4 + PX) * 2)) & 0x03;
					// DXT3: 4-bit explicit alpha per pixel, 2 pixels per byte
					int32 AlphaIdx = PY * 4 + PX;
					uint8 Alpha4 = (AlphaBlock[AlphaIdx / 2] >> ((AlphaIdx % 2) * 4)) & 0x0F;
					uint8 Alpha = Alpha4 | (Alpha4 << 4); // expand 4-bit to 8-bit

					int32 Pixel = (Y * Width + X) * 4;
					OutBGRA[Pixel + 0] = B[Idx];
					OutBGRA[Pixel + 1] = G[Idx];
					OutBGRA[Pixel + 2] = R[Idx];
					OutBGRA[Pixel + 3] = Alpha;
				}
			}
		}
	}
	return true;
}

bool FVTFReader::DecompressDXT5(const uint8* Src, int32 Width, int32 Height, TArray<uint8>& OutBGRA)
{
	int32 BlocksX = FMath::Max(Width / 4, 1);
	int32 BlocksY = FMath::Max(Height / 4, 1);
	OutBGRA.SetNumZeroed(Width * Height * 4);

	for (int32 BY = 0; BY < BlocksY; BY++)
	{
		for (int32 BX = 0; BX < BlocksX; BX++)
		{
			const uint8* Block = Src + (BY * BlocksX + BX) * 16;

			// Alpha block: 2 reference alphas + 6 bytes of 3-bit indices (16 pixels)
			uint8 A0 = Block[0];
			uint8 A1 = Block[1];
			uint8 AlphaPalette[8];
			AlphaPalette[0] = A0;
			AlphaPalette[1] = A1;
			if (A0 > A1)
			{
				AlphaPalette[2] = (6 * A0 + 1 * A1) / 7;
				AlphaPalette[3] = (5 * A0 + 2 * A1) / 7;
				AlphaPalette[4] = (4 * A0 + 3 * A1) / 7;
				AlphaPalette[5] = (3 * A0 + 4 * A1) / 7;
				AlphaPalette[6] = (2 * A0 + 5 * A1) / 7;
				AlphaPalette[7] = (1 * A0 + 6 * A1) / 7;
			}
			else
			{
				AlphaPalette[2] = (4 * A0 + 1 * A1) / 5;
				AlphaPalette[3] = (3 * A0 + 2 * A1) / 5;
				AlphaPalette[4] = (2 * A0 + 3 * A1) / 5;
				AlphaPalette[5] = (1 * A0 + 4 * A1) / 5;
				AlphaPalette[6] = 0;
				AlphaPalette[7] = 255;
			}

			// Read 48 bits (6 bytes) of 3-bit alpha indices
			uint64 AlphaBits = 0;
			for (int32 i = 0; i < 6; i++)
			{
				AlphaBits |= (uint64)Block[2 + i] << (i * 8);
			}

			// Color block (same as DXT1, starts at byte 8)
			const uint8* ColorBlock = Block + 8;
			uint16 C0 = ColorBlock[0] | (ColorBlock[1] << 8);
			uint16 C1 = ColorBlock[2] | (ColorBlock[3] << 8);
			uint32 LookupTable = ColorBlock[4] | (ColorBlock[5] << 8) | (ColorBlock[6] << 16) | (ColorBlock[7] << 24);

			uint8 R[4], G[4], B[4];
			DecodeDXTColor(C0, R[0], G[0], B[0]);
			DecodeDXTColor(C1, R[1], G[1], B[1]);
			R[2] = (2 * R[0] + R[1]) / 3; G[2] = (2 * G[0] + G[1]) / 3; B[2] = (2 * B[0] + B[1]) / 3;
			R[3] = (R[0] + 2 * R[1]) / 3; G[3] = (G[0] + 2 * G[1]) / 3; B[3] = (B[0] + 2 * B[1]) / 3;

			for (int32 PY = 0; PY < 4; PY++)
			{
				for (int32 PX = 0; PX < 4; PX++)
				{
					int32 X = BX * 4 + PX;
					int32 Y = BY * 4 + PY;
					if (X >= Width || Y >= Height) continue;

					int32 ColorIdx = (LookupTable >> ((PY * 4 + PX) * 2)) & 0x03;
					int32 AlphaIdx = (AlphaBits >> ((PY * 4 + PX) * 3)) & 0x07;

					int32 Pixel = (Y * Width + X) * 4;
					OutBGRA[Pixel + 0] = B[ColorIdx];
					OutBGRA[Pixel + 1] = G[ColorIdx];
					OutBGRA[Pixel + 2] = R[ColorIdx];
					OutBGRA[Pixel + 3] = AlphaPalette[AlphaIdx];
				}
			}
		}
	}
	return true;
}

void FVTFReader::SaveBGRAAsPNG(const TArray<uint8>& BGRAData, int32 Width, int32 Height, const FString& FilePath)
{
	// Convert BGRA → RGBA for the image wrapper
	TArray<uint8> RGBA;
	RGBA.SetNumUninitialized(BGRAData.Num());
	for (int32 i = 0; i < Width * Height; i++)
	{
		RGBA[i * 4 + 0] = BGRAData[i * 4 + 2]; // R
		RGBA[i * 4 + 1] = BGRAData[i * 4 + 1]; // G
		RGBA[i * 4 + 2] = BGRAData[i * 4 + 0]; // B
		RGBA[i * 4 + 3] = BGRAData[i * 4 + 3]; // A
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> PNGWriter = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (PNGWriter.IsValid() && PNGWriter->SetRaw(RGBA.GetData(), RGBA.Num(), Width, Height, ERGBFormat::RGBA, 8))
	{
		const TArray64<uint8>& PNGData = PNGWriter->GetCompressed();

		// Ensure directory exists
		FString Dir = FPaths::GetPath(FilePath);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*Dir);

		FFileHelper::SaveArrayToFile(PNGData, *FilePath);
	}
}
