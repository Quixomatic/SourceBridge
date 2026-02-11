#include "Materials/TextureExporter.h"
#include "Materials/VMTWriter.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ImageUtils.h"

bool FTextureExporter::ExportTextureToTGA(UTexture2D* Texture, const FString& OutputPath)
{
	if (!Texture)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceBridge: Null texture provided for export."));
		return false;
	}

	// Ensure directory exists
	FString Dir = FPaths::GetPath(OutputPath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*Dir);

	// Get the texture source data
	FTextureSource& Source = Texture->Source;
	if (!Source.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("SourceBridge: Texture %s has no source data. Enable 'Never Stream' or reimport."),
			*Texture->GetName());
		return false;
	}

	int32 Width = Source.GetSizeX();
	int32 Height = Source.GetSizeY();

	// Lock and read source mip 0
	TArray64<uint8> SourceData;
	Source.GetMipData(SourceData, 0);

	if (SourceData.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceBridge: Failed to read texture source data for %s"), *Texture->GetName());
		return false;
	}

	ETextureSourceFormat SourceFormat = Source.GetFormat();

	// Convert to BGRA8 for TGA
	TArray<FColor> Pixels;
	Pixels.SetNum(Width * Height);

	if (SourceFormat == TSF_BGRA8 || SourceFormat == TSF_BGRE8)
	{
		FMemory::Memcpy(Pixels.GetData(), SourceData.GetData(), FMath::Min((int64)Pixels.Num() * 4, SourceData.Num()));
	}
	else if (SourceFormat == TSF_RGBA8_DEPRECATED)
	{
		// Swap R and B channels (RGBA8 was deprecated in UE 5.7, converted to BGRA8 on load,
		// but handle it here for safety)
		const uint8* Src = SourceData.GetData();
		for (int32 i = 0; i < Width * Height; i++)
		{
			Pixels[i].B = Src[i * 4 + 0]; // R -> B
			Pixels[i].G = Src[i * 4 + 1];
			Pixels[i].R = Src[i * 4 + 2]; // B -> R
			Pixels[i].A = Src[i * 4 + 3];
		}
	}
	else if (SourceFormat == TSF_RGBA16)
	{
		const uint16* Src = reinterpret_cast<const uint16*>(SourceData.GetData());
		for (int32 i = 0; i < Width * Height; i++)
		{
			Pixels[i].R = Src[i * 4 + 0] >> 8;
			Pixels[i].G = Src[i * 4 + 1] >> 8;
			Pixels[i].B = Src[i * 4 + 2] >> 8;
			Pixels[i].A = Src[i * 4 + 3] >> 8;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Unsupported texture format %d for %s, attempting raw copy."),
			(int32)SourceFormat, *Texture->GetName());
		FMemory::Memcpy(Pixels.GetData(), SourceData.GetData(), FMath::Min((int64)Pixels.Num() * 4, SourceData.Num()));
	}

	// Write TGA file
	TArray<uint8> TGAData;
	FImageUtils::ThumbnailCompressImageArray(Width, Height, Pixels, TGAData);

	// Manual TGA write since ThumbnailCompress may not produce standard TGA
	TGAData.Reset();
	TGAData.SetNum(18 + Width * Height * 4);

	// TGA header
	uint8* Header = TGAData.GetData();
	FMemory::Memzero(Header, 18);
	Header[2] = 2; // Uncompressed RGBA
	Header[12] = Width & 0xFF;
	Header[13] = (Width >> 8) & 0xFF;
	Header[14] = Height & 0xFF;
	Header[15] = (Height >> 8) & 0xFF;
	Header[16] = 32; // 32 bits per pixel
	Header[17] = 0x28; // Top-left origin + 8 alpha bits

	// Write pixel data (TGA stores BGRA)
	uint8* PixelData = TGAData.GetData() + 18;
	for (int32 y = 0; y < Height; y++)
	{
		for (int32 x = 0; x < Width; x++)
		{
			int32 SrcIdx = y * Width + x;
			int32 DstIdx = (y * Width + x) * 4;
			PixelData[DstIdx + 0] = Pixels[SrcIdx].B;
			PixelData[DstIdx + 1] = Pixels[SrcIdx].G;
			PixelData[DstIdx + 2] = Pixels[SrcIdx].R;
			PixelData[DstIdx + 3] = Pixels[SrcIdx].A;
		}
	}

	if (FFileHelper::SaveArrayToFile(TGAData, *OutputPath))
	{
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported texture %s to %s (%dx%d)"),
			*Texture->GetName(), *OutputPath, Width, Height);
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("SourceBridge: Failed to write TGA file: %s"), *OutputPath);
	return false;
}

FString FTextureExporter::ConvertTGAToVTF(
	const FString& TGAPath,
	const FString& OutputDir,
	const FString& VTFCmdPath)
{
	FString ToolPath = VTFCmdPath;
	if (ToolPath.IsEmpty())
	{
		ToolPath = FindVTFCmd();
	}

	if (ToolPath.IsEmpty() || !FPaths::FileExists(ToolPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("SourceBridge: vtfcmd.exe not found. TGA exported but VTF conversion skipped."));
		return FString();
	}

	// vtfcmd.exe -file "input.tga" -output "outputdir" -format "DXT5"
	FString Args = FString::Printf(
		TEXT("-file \"%s\" -output \"%s\" -format \"DXT5\" -nomipmaps"),
		*TGAPath, *OutputDir);

	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;

	bool bLaunched = FPlatformProcess::ExecProcess(
		*ToolPath, *Args, &ReturnCode, &StdOut, &StdErr);

	if (!bLaunched)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceBridge: Failed to launch vtfcmd.exe"));
		return FString();
	}

	if (ReturnCode != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceBridge: vtfcmd.exe failed (code %d): %s"), ReturnCode, *StdErr);
		return FString();
	}

	// VTF output has same name as input but with .vtf extension
	FString VTFName = FPaths::GetBaseFilename(TGAPath) + TEXT(".vtf");
	FString VTFPath = OutputDir / VTFName;

	if (FPaths::FileExists(VTFPath))
	{
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: VTF created: %s"), *VTFPath);
		return VTFPath;
	}

	UE_LOG(LogTemp, Warning, TEXT("SourceBridge: vtfcmd ran but VTF not found at: %s"), *VTFPath);
	return FString();
}

FTextureExportResult FTextureExporter::ExportFullPipeline(
	UTexture2D* Texture,
	const FString& OutputDir,
	const FString& MaterialPath,
	const FString& SurfaceProp,
	const FString& VTFCmdPath)
{
	FTextureExportResult Result;

	if (!Texture)
	{
		Result.ErrorMessage = TEXT("Null texture.");
		return Result;
	}

	// Create output directory structure
	// Materials go into: OutputDir/materials/<materialpath>/
	FString MatDir = OutputDir / TEXT("materials") / FPaths::GetPath(MaterialPath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*MatDir);

	FString BaseName = FPaths::GetBaseFilename(MaterialPath);

	// Export TGA
	Result.TGAPath = MatDir / BaseName + TEXT(".tga");
	if (!ExportTextureToTGA(Texture, Result.TGAPath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to export TGA for %s"), *Texture->GetName());
		return Result;
	}

	// Convert to VTF
	Result.VTFPath = ConvertTGAToVTF(Result.TGAPath, MatDir, VTFCmdPath);
	// VTF conversion is optional - TGA is still useful without it

	// Generate VMT
	FString VMTContent = FVMTWriter::GenerateBrushVMT(MaterialPath, SurfaceProp);
	Result.VMTPath = MatDir / BaseName + TEXT(".vmt");
	if (FFileHelper::SaveStringToFile(VMTContent, *Result.VMTPath))
	{
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: VMT written: %s"), *Result.VMTPath);
	}
	else
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to write VMT: %s"), *Result.VMTPath);
		return Result;
	}

	Result.bSuccess = true;
	return Result;
}

FString FTextureExporter::FindVTFCmd()
{
	// Check common locations for vtfcmd.exe
	TArray<FString> SearchPaths = {
		FPaths::ProjectDir() / TEXT("Tools/vtfcmd.exe"),
		FPaths::ProjectPluginsDir() / TEXT("SourceBridge/Tools/vtfcmd.exe"),
		TEXT("C:/Program Files (x86)/VTFEdit/vtfcmd.exe"),
		TEXT("C:/Program Files/VTFEdit/vtfcmd.exe"),
		TEXT("C:/Tools/vtfcmd.exe"),
	};

	for (const FString& Path : SearchPaths)
	{
		if (FPaths::FileExists(Path))
		{
			UE_LOG(LogTemp, Log, TEXT("SourceBridge: Found vtfcmd.exe at: %s"), *Path);
			return Path;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("SourceBridge: vtfcmd.exe not found. VTF conversion disabled."));
	return FString();
}

bool FTextureExporter::CopyToGameMaterials(
	const FString& VTFPath,
	const FString& VMTPath,
	const FString& GameDir,
	const FString& MaterialPath)
{
	FString GameMatDir = GameDir / TEXT("materials") / FPaths::GetPath(MaterialPath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*GameMatDir);

	FString BaseName = FPaths::GetBaseFilename(MaterialPath);
	bool bSuccess = true;

	if (!VTFPath.IsEmpty() && FPaths::FileExists(VTFPath))
	{
		FString Dest = GameMatDir / BaseName + TEXT(".vtf");
		if (!PlatformFile.CopyFile(*Dest, *VTFPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Failed to copy VTF to %s"), *Dest);
			bSuccess = false;
		}
	}

	if (!VMTPath.IsEmpty() && FPaths::FileExists(VMTPath))
	{
		FString Dest = GameMatDir / BaseName + TEXT(".vmt");
		if (!PlatformFile.CopyFile(*Dest, *VMTPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Failed to copy VMT to %s"), *Dest);
			bSuccess = false;
		}
	}

	return bSuccess;
}
