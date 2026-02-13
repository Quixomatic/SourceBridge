#include "Actors/SourceEntityActor.h"
#include "Utilities/ToolTextureClassifier.h"
#include "Utilities/SourceCoord.h"
#include "Import/MaterialImporter.h"
#include "UI/SourceIOVisualizer.h"
#include "Components/BillboardComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"

// ---- Base ----

ASourceEntityActor::ASourceEntityActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

#if WITH_EDITORONLY_DATA
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (SpriteComponent)
	{
		SpriteComponent->SetupAttachment(RootComponent);
		SpriteComponent->bIsScreenSizeScaled = true;
	}

	// I/O wire visualization (automatically draws connection lines in editor)
	USourceIOVisualizer* IOVis = CreateEditorOnlyDefaultSubobject<USourceIOVisualizer>(TEXT("IOVisualizer"));
	if (IOVis)
	{
		IOVis->SetIsVisualizationComponent(true);
	}
#endif
}

#if WITH_EDITORONLY_DATA
void ASourceEntityActor::UpdateEditorSprite()
{
	if (!SpriteComponent || IsRunningCommandlet())
	{
		return;
	}

	const FString& CN = SourceClassname;
	FString SpritePath;

	// Sound entities
	if (CN.StartsWith(TEXT("ambient_")) || CN == TEXT("env_soundscape"))
	{
		SpritePath = TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent.S_AudioComponent");
	}
	// Trigger entities
	else if (CN.StartsWith(TEXT("trigger_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Trigger.S_Trigger");
	}
	// Spot lights
	else if (CN == TEXT("light_spot"))
	{
		SpritePath = TEXT("/Engine/EditorResources/LightIcons/S_LightSpot.S_LightSpot");
	}
	// All other lights
	else if (CN.StartsWith(TEXT("light")))
	{
		SpritePath = TEXT("/Engine/EditorResources/LightIcons/S_LightPoint.S_LightPoint");
	}
	// Props
	else if (CN.StartsWith(TEXT("prop_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Actor.S_Actor");
	}
	// Player spawns
	else if (CN.StartsWith(TEXT("info_player")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Player.S_Player");
	}
	// Cameras
	else if (CN == TEXT("point_viewcontrol") || CN.StartsWith(TEXT("point_camera")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Camera.S_Camera");
	}
	// Environment effects (but not soundscapes, handled above)
	else if (CN.StartsWith(TEXT("env_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Emitter.S_Emitter");
	}
	// Logic entities
	else if (CN.StartsWith(TEXT("logic_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint");
	}
	// Brush entities (func_detail, func_door, func_wall, etc.)
	else if (CN.StartsWith(TEXT("func_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Trigger.S_Trigger");
	}
	// Info entities (info_target, info_landmark, etc.)
	else if (CN.StartsWith(TEXT("info_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint");
	}
	// Game text entity
	else if (CN == TEXT("game_text"))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TextRenderActorIcon.S_TextRenderActorIcon");
	}
	// Other game entities (game_end, etc.)
	else if (CN.StartsWith(TEXT("game_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint");
	}
	// Point entities (point_template, point_spotlight, etc.)
	else if (CN.StartsWith(TEXT("point_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint");
	}
	else
	{
		return; // Keep default UE sprite
	}

	UTexture2D* Sprite = LoadObject<UTexture2D>(nullptr, *SpritePath);
	if (Sprite)
	{
		SpriteComponent->SetSprite(Sprite);
	}
}
#endif

// ---- BeginPlay (PIE Runtime) ----

void ASourceEntityActor::BeginPlay()
{
	Super::BeginPlay();

#if WITH_EDITORONLY_DATA
	// Hide editor-only visualization in PIE
	if (SpriteComponent)
	{
		SpriteComponent->SetVisibility(false);
	}

	// Disable I/O visualizer wire drawing
	if (USourceIOVisualizer* IOVis = FindComponentByClass<USourceIOVisualizer>())
	{
		IOVis->bDrawWires = false;
	}

	// Hide arrow components (spawn direction indicators, etc.)
	TArray<UArrowComponent*> Arrows;
	GetComponents<UArrowComponent>(Arrows);
	for (UArrowComponent* Arrow : Arrows)
	{
		Arrow->SetVisibility(false);
	}

	// Hide capsule visualization (spawn point bounds)
	TArray<UCapsuleComponent*> Capsules;
	GetComponents<UCapsuleComponent>(Capsules);
	for (UCapsuleComponent* Capsule : Capsules)
	{
		Capsule->SetVisibility(false);
	}
#endif
}

// ---- T Spawn ----

ASourceTSpawn::ASourceTSpawn()
{
	SourceClassname = TEXT("info_player_terrorist");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	// Red capsule showing player bounds (72 units tall, 16 radius in Source = ~137cm tall, ~30cm radius in UE)
	UCapsuleComponent* Capsule = CreateEditorOnlyDefaultSubobject<UCapsuleComponent>(TEXT("PlayerBounds"));
	if (Capsule)
	{
		Capsule->SetupAttachment(RootComponent);
		Capsule->SetCapsuleHalfHeight(68.5f); // ~137cm / 2
		Capsule->SetCapsuleRadius(30.0f);
		Capsule->SetRelativeLocation(FVector(0, 0, 68.5f));
		Capsule->ShapeColor = FColor::Red;
		Capsule->SetHiddenInGame(true);
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Red arrow showing facing direction
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Red;
		Arrow->ArrowSize = 1.5f;
		Arrow->ArrowLength = 80.0f;
		Arrow->SetRelativeLocation(FVector(0, 0, 68.5f));
		Arrow->SetHiddenInGame(true);
	}
#endif
}

// ---- CT Spawn ----

ASourceCTSpawn::ASourceCTSpawn()
{
	SourceClassname = TEXT("info_player_counterterrorist");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	// Blue capsule showing player bounds
	UCapsuleComponent* Capsule = CreateEditorOnlyDefaultSubobject<UCapsuleComponent>(TEXT("PlayerBounds"));
	if (Capsule)
	{
		Capsule->SetupAttachment(RootComponent);
		Capsule->SetCapsuleHalfHeight(68.5f);
		Capsule->SetCapsuleRadius(30.0f);
		Capsule->SetRelativeLocation(FVector(0, 0, 68.5f));
		Capsule->ShapeColor = FColor::Blue;
		Capsule->SetHiddenInGame(true);
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Blue arrow showing facing direction
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Blue;
		Arrow->ArrowSize = 1.5f;
		Arrow->ArrowLength = 80.0f;
		Arrow->SetRelativeLocation(FVector(0, 0, 68.5f));
		Arrow->SetHiddenInGame(true);
	}
#endif
}

// ---- Trigger ----

ASourceTrigger::ASourceTrigger()
{
	SourceClassname = TEXT("trigger_multiple");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Light ----

ASourceLight::ASourceLight()
{
	SourceClassname = TEXT("light");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Prop ----

ASourceProp::ASourceProp()
{
	SourceClassname = TEXT("prop_static");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ModelMesh"));
	if (MeshComponent)
	{
		MeshComponent->SetupAttachment(RootComponent);
		MeshComponent->SetMobility(EComponentMobility::Movable);
	}
}

void ASourceProp::SetStaticMesh(UStaticMesh* Mesh)
{
	if (MeshComponent && Mesh)
	{
		MeshComponent->SetStaticMesh(Mesh);
	}
}

// ---- Brush Entity ----

ASourceBrushEntity::ASourceBrushEntity()
{
	SourceClassname = TEXT("func_detail");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

void ASourceBrushEntity::PostLoad()
{
	Super::PostLoad();

	// If we have stored brush data but no proc mesh components, reconstruct
	if (StoredBrushData.Num() > 0)
	{
		bool bNeedsReconstruct = (BrushMeshes.Num() == 0);
		if (!bNeedsReconstruct)
		{
			// Check if all mesh references are valid
			for (const auto& Mesh : BrushMeshes)
			{
				if (!Mesh || Mesh->GetNumSections() == 0)
				{
					bNeedsReconstruct = true;
					break;
				}
			}
		}

		if (bNeedsReconstruct)
		{
			ReconstructFromStoredData();
		}
	}
}

// ---- UV Axis Parsing Helper ----
static bool ParseStoredUVAxis(const FString& AxisStr, FVector& Axis, float& Offset, float& Scale)
{
	int32 BracketStart = AxisStr.Find(TEXT("["));
	int32 BracketEnd = AxisStr.Find(TEXT("]"));
	if (BracketStart == INDEX_NONE || BracketEnd == INDEX_NONE) return false;

	FString Inside = AxisStr.Mid(BracketStart + 1, BracketEnd - BracketStart - 1).TrimStartAndEnd();
	FString After = AxisStr.Mid(BracketEnd + 1).TrimStartAndEnd();

	TArray<FString> Parts;
	Inside.ParseIntoArrayWS(Parts);
	if (Parts.Num() < 4) return false;

	Axis.X = FCString::Atod(*Parts[0]);
	Axis.Y = FCString::Atod(*Parts[1]);
	Axis.Z = FCString::Atod(*Parts[2]);
	Offset = FCString::Atof(*Parts[3]);
	Scale = FCString::Atof(*After);
	if (FMath::IsNearlyZero(Scale)) Scale = 0.25f;

	return true;
}

// ---- Polygon Clipping Helpers (duplicated from VMFImporter for self-contained reconstruction) ----
static TArray<FVector> ClipPolygon(const TArray<FVector>& Polygon, const FPlane& Plane)
{
	if (Polygon.Num() < 3) return TArray<FVector>();

	TArray<FVector> Result;
	constexpr float Epsilon = 0.01f;

	for (int32 i = 0; i < Polygon.Num(); i++)
	{
		const FVector& Current = Polygon[i];
		const FVector& Next = Polygon[(i + 1) % Polygon.Num()];

		float DistCurrent = Plane.PlaneDot(Current);
		float DistNext = Plane.PlaneDot(Next);

		if (DistCurrent >= -Epsilon) Result.Add(Current);

		if ((DistCurrent > Epsilon && DistNext < -Epsilon) ||
			(DistCurrent < -Epsilon && DistNext > Epsilon))
		{
			float T = DistCurrent / (DistCurrent - DistNext);
			T = FMath::Clamp(T, 0.0f, 1.0f);
			Result.Add(FMath::Lerp(Current, Next, T));
		}
	}

	return Result;
}

static TArray<FVector> CreateLargePoly(const FPlane& Plane, const FVector& PointOnPlane)
{
	constexpr float HalfSize = 65536.0f;
	FVector Normal(Plane.X, Plane.Y, Plane.Z);

	FVector Up = FMath::Abs(Normal.Z) > 0.9f ? FVector(1, 0, 0) : FVector(0, 0, 1);
	FVector U = FVector::CrossProduct(Normal, Up).GetSafeNormal() * HalfSize;
	FVector V = FVector::CrossProduct(Normal, U).GetSafeNormal() * HalfSize;

	return {
		PointOnPlane - U - V,
		PointOnPlane + U - V,
		PointOnPlane + U + V,
		PointOnPlane - U + V
	};
}

void ASourceBrushEntity::ReconstructFromStoredData()
{
	// Clear existing proc meshes
	for (auto& Mesh : BrushMeshes)
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	BrushMeshes.Empty();

	const float Scale = 1.0f / 0.525f; // Source→UE scale
	FVector ActorCenter = GetActorLocation();

	for (int32 SolidIdx = 0; SolidIdx < StoredBrushData.Num(); SolidIdx++)
	{
		const FImportedBrushData& BrushData = StoredBrushData[SolidIdx];
		if (BrushData.Sides.Num() < 4) continue;

		// Build planes from stored plane points
		TArray<FPlane> Planes;
		TArray<FVector> PlaneFirstPoints;
		TArray<FVector> UAxes, VAxes;
		TArray<float> UOffsets, VOffsets, UScales, VScales;
		TArray<FString> Materials;

		for (const FImportedSideData& Side : BrushData.Sides)
		{
			FVector Edge1 = Side.PlaneP2 - Side.PlaneP1;
			FVector Edge2 = Side.PlaneP3 - Side.PlaneP1;
			FVector Normal = FVector::CrossProduct(Edge1, Edge2);
			if (Normal.IsNearlyZero()) continue;
			Normal.Normalize();

			Planes.Add(FPlane(Side.PlaneP1, Normal));
			PlaneFirstPoints.Add(Side.PlaneP1);

			FVector UAxis(1, 0, 0), VAxis(0, -1, 0);
			float UOff = 0, VOff = 0, USc = 0.25f, VSc = 0.25f;
			ParseStoredUVAxis(Side.UAxisStr, UAxis, UOff, USc);
			ParseStoredUVAxis(Side.VAxisStr, VAxis, VOff, VSc);

			UAxes.Add(UAxis);
			VAxes.Add(VAxis);
			UOffsets.Add(UOff);
			VOffsets.Add(VOff);
			UScales.Add(USc);
			VScales.Add(VSc);
			Materials.Add(Side.Material);
		}

		if (Planes.Num() < 4) continue;

		// Reconstruct face polygons by CSG clipping
		TArray<TArray<FVector>> Faces;
		TArray<int32> FaceToPlaneIdx;

		for (int32 i = 0; i < Planes.Num(); i++)
		{
			TArray<FVector> Poly = CreateLargePoly(Planes[i], PlaneFirstPoints[i]);

			for (int32 j = 0; j < Planes.Num(); j++)
			{
				if (i == j) continue;
				// Clip by the inward-pointing plane: keep what's behind other planes
				FPlane ClipPlane(-Planes[j].X, -Planes[j].Y, -Planes[j].Z, -Planes[j].W);
				Poly = ClipPolygon(Poly, ClipPlane);
				if (Poly.Num() < 3) break;
			}

			if (Poly.Num() >= 3)
			{
				Faces.Add(Poly);
				FaceToPlaneIdx.Add(i);
			}
		}

		if (Faces.Num() < 4) continue;

		// Build proc mesh component
		FString MeshName = FString::Printf(TEXT("BrushMesh_%d"), SolidIdx);
		UProceduralMeshComponent* ProcMesh = NewObject<UProceduralMeshComponent>(this, *MeshName);
		ProcMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		ProcMesh->SetRelativeTransform(FTransform::Identity);
		ProcMesh->CreationMethod = EComponentCreationMethod::Instance;

		// Group faces by material into sections
		TMap<FString, int32> MatToSection;
		struct FSectionData
		{
			TArray<FVector> Vertices;
			TArray<int32> Triangles;
			TArray<FVector> Normals;
			TArray<FVector2D> UVs;
			TArray<FProcMeshTangent> Tangents;
			UMaterialInterface* Material = nullptr;
		};
		TArray<FSectionData> Sections;

		for (int32 FaceIdx = 0; FaceIdx < Faces.Num(); FaceIdx++)
		{
			const TArray<FVector>& FaceVerts = Faces[FaceIdx];
			if (FaceVerts.Num() < 3) continue;

			int32 PlaneIdx = FaceToPlaneIdx[FaceIdx];
			const FString& MatPath = Materials[PlaneIdx];

			// Resolve material from manifest
			UMaterialInterface* Mat = nullptr;
			if (!MatPath.IsEmpty())
			{
				Mat = FMaterialImporter::ResolveSourceMaterial(MatPath);
			}

			FString MatKey = MatPath.IsEmpty() ? TEXT("__none__") : MatPath;
			int32* SecIdx = MatToSection.Find(MatKey);
			if (!SecIdx)
			{
				int32 NewIdx = Sections.Num();
				MatToSection.Add(MatKey, NewIdx);
				Sections.AddDefaulted();
				Sections[NewIdx].Material = Mat;
				SecIdx = &MatToSection[MatKey];
			}

			FSectionData& Sec = Sections[*SecIdx];
			int32 BaseVert = Sec.Vertices.Num();

			// Get texture size for UV normalization
			FIntPoint TexSize(512, 512);
			if (!MatPath.IsEmpty())
			{
				TexSize = FMaterialImporter::GetTextureSize(MatPath);
			}

			// Get outward normal from stored plane definition (known correct)
			// Planes[] contain inward-pointing normals (VMF convention: (P2-P1)×(P3-P1) points INWARD)
			// Convert to UE space (negate Y) and negate for outward
			FVector InwardSource(Planes[PlaneIdx].X, Planes[PlaneIdx].Y, Planes[PlaneIdx].Z);
			FVector InwardUE(InwardSource.X, -InwardSource.Y, InwardSource.Z);
			FVector OutwardNormal = -InwardUE;
			if (!OutwardNormal.IsNearlyZero()) OutwardNormal.Normalize();

			// Determine winding flip by comparing vertex winding to outward normal
			FVector V0(FaceVerts[0].X * Scale, -FaceVerts[0].Y * Scale, FaceVerts[0].Z * Scale);
			FVector V1(FaceVerts[1].X * Scale, -FaceVerts[1].Y * Scale, FaceVerts[1].Z * Scale);
			FVector V2(FaceVerts[2].X * Scale, -FaceVerts[2].Y * Scale, FaceVerts[2].Z * Scale);
			V0 -= ActorCenter; V1 -= ActorCenter; V2 -= ActorCenter;
			FVector WindingNormal = FVector::CrossProduct(V1 - V0, V2 - V0);
			bool bFlipWinding = FVector::DotProduct(WindingNormal, OutwardNormal) > 0.0f;

			// Compute tangent from texture U axis (Source→UE: negate Y)
			FVector UETangentDir(UAxes[PlaneIdx].X, -UAxes[PlaneIdx].Y, UAxes[PlaneIdx].Z);
			if (!UETangentDir.IsNearlyZero()) UETangentDir.Normalize();
			FProcMeshTangent FaceTangent(UETangentDir, false);

			for (const FVector& V : FaceVerts)
			{
				// Source→UE conversion
				FVector LocalPos(V.X * Scale, -V.Y * Scale, V.Z * Scale);
				LocalPos -= ActorCenter;
				Sec.Vertices.Add(LocalPos);
				Sec.Normals.Add(OutwardNormal);
				Sec.Tangents.Add(FaceTangent);

				// Compute UVs in Source space
				float UTexel = FVector::DotProduct(V, UAxes[PlaneIdx]) / UScales[PlaneIdx] + UOffsets[PlaneIdx];
				float VTexel = FVector::DotProduct(V, VAxes[PlaneIdx]) / VScales[PlaneIdx] + VOffsets[PlaneIdx];
				Sec.UVs.Add(FVector2D(UTexel / (float)TexSize.X, VTexel / (float)TexSize.Y));
			}

			// Fan triangulation
			for (int32 i = 1; i < FaceVerts.Num() - 1; i++)
			{
				Sec.Triangles.Add(BaseVert);
				if (bFlipWinding)
				{
					Sec.Triangles.Add(BaseVert + i + 1);
					Sec.Triangles.Add(BaseVert + i);
				}
				else
				{
					Sec.Triangles.Add(BaseVert + i);
					Sec.Triangles.Add(BaseVert + i + 1);
				}
			}
		}

		// Create mesh sections
		for (int32 i = 0; i < Sections.Num(); i++)
		{
			ProcMesh->CreateMeshSection_LinearColor(i,
				Sections[i].Vertices, Sections[i].Triangles,
				Sections[i].Normals, Sections[i].UVs,
				TArray<FLinearColor>(), Sections[i].Tangents, true);

			if (Sections[i].Material)
			{
				ProcMesh->SetMaterial(i, Sections[i].Material);
			}
		}

		ProcMesh->RegisterComponent();
		BrushMeshes.Add(ProcMesh);
	}

	UE_LOG(LogTemp, Log, TEXT("SourceBrushEntity: Reconstructed %d proc meshes from stored data for '%s'"),
		BrushMeshes.Num(), *SourceClassname);
}

UProceduralMeshComponent* ASourceBrushEntity::AddBrushMesh(const FString& MeshName)
{
	UProceduralMeshComponent* ProcMesh = NewObject<UProceduralMeshComponent>(this, *MeshName);
	if (ProcMesh)
	{
		ProcMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		ProcMesh->SetRelativeTransform(FTransform::Identity);
		// Mark as instance component so it serializes with the level (survives save/reload)
		ProcMesh->CreationMethod = EComponentCreationMethod::Instance;
		ProcMesh->RegisterComponent();
		BrushMeshes.Add(ProcMesh);
	}
	return ProcMesh;
}

void ASourceBrushEntity::BeginPlay()
{
	Super::BeginPlay();

	const FString& CN = SourceClassname;

	// Determine collision behavior from entity classname
	bool bIsTrigger = CN.StartsWith(TEXT("trigger_"));
	bool bIsClip = CN.Equals(TEXT("func_clip_vphysics"), ESearchCase::IgnoreCase) ||
				   CN.Equals(TEXT("func_clip"), ESearchCase::IgnoreCase);
	bool bIsIllusionary = CN.Equals(TEXT("func_illusionary"), ESearchCase::IgnoreCase);

	for (UProceduralMeshComponent* Mesh : BrushMeshes)
	{
		if (!Mesh) continue;

		// --- Visibility: hide individual sections with tool textures (like vbsp strips faces) ---
		int32 NumSections = Mesh->GetNumSections();
		for (int32 i = 0; i < NumSections; i++)
		{
			UMaterialInterface* Mat = Mesh->GetMaterial(i);
			if (Mat && Mat->GetName().Contains(TEXT("TOOLS"), ESearchCase::IgnoreCase))
			{
				Mesh->SetMeshSectionVisible(i, false);
			}
		}

		// --- Collision: driven by entity classname (entity behavior) ---
		if (bIsTrigger)
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Mesh->SetCollisionResponseToAllChannels(ECR_Overlap);
		}
		else if (bIsClip)
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Mesh->SetCollisionResponseToAllChannels(ECR_Block);
		}
		else if (bIsIllusionary)
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	// func_wall with rendermode 10 (Don't render) — hide regardless of materials
	if (CN.Equals(TEXT("func_wall"), ESearchCase::IgnoreCase) ||
		CN.Equals(TEXT("func_wall_toggle"), ESearchCase::IgnoreCase))
	{
		const FString* RenderMode = KeyValues.Find(TEXT("rendermode"));
		if (RenderMode && FCString::Atoi(**RenderMode) == 10)
		{
			for (UProceduralMeshComponent* Mesh : BrushMeshes)
			{
				if (Mesh) Mesh->SetVisibility(false);
			}
		}
	}
}

FString ASourceBrushEntity::GetDefaultMaterialForClassname() const
{
	const FString& CN = SourceClassname;
	if (CN.StartsWith(TEXT("trigger_")))
		return TEXT("TOOLS/TOOLSTRIGGER");
	if (CN.Equals(TEXT("func_clip_vphysics"), ESearchCase::IgnoreCase) ||
		CN.Equals(TEXT("func_clip"), ESearchCase::IgnoreCase))
		return TEXT("TOOLS/TOOLSPLAYERCLIP");
	if (CN.Equals(TEXT("func_areaportal"), ESearchCase::IgnoreCase))
		return TEXT("TOOLS/TOOLSAREAPORTAL");
	if (CN.Equals(TEXT("func_viscluster"), ESearchCase::IgnoreCase))
		return TEXT("TOOLS/TOOLSSKIP");
	return TEXT("TOOLS/TOOLSNODRAW");
}

void ASourceBrushEntity::GenerateDefaultGeometry()
{
	// Don't regenerate if we already have imported geometry
	if (StoredBrushData.Num() > 0)
		return;

	bIsGeneratedGeometry = true;

	// Override default dimensions for specific entity types
	const FString& CN = SourceClassname;
	if (CN.Equals(TEXT("func_door"), ESearchCase::IgnoreCase) ||
		CN.Equals(TEXT("func_door_rotating"), ESearchCase::IgnoreCase))
	{
		BrushDimensions = FVector(32.0, 64.0, 128.0);
	}
	else if (CN.Equals(TEXT("func_areaportal"), ESearchCase::IgnoreCase))
	{
		BrushDimensions = FVector(64.0, 64.0, 2.0);
	}
	else if (CN.Equals(TEXT("func_conveyor"), ESearchCase::IgnoreCase))
	{
		BrushDimensions = FVector(128.0, 32.0, 4.0);
	}

	// Half-extents in Source units
	const double HX = BrushDimensions.X * 0.5;
	const double HY = BrushDimensions.Y * 0.5;
	const double HZ = BrushDimensions.Z * 0.5;

	const FString DefaultMat = GetDefaultMaterialForClassname();

	// Build 6 faces as FImportedSideData with plane points in Source coordinates.
	// VMF convention: (P2-P1)x(P3-P1) points INWARD into the solid.
	// Each face's 3 plane points are CW when viewed from outside.
	FImportedBrushData BrushData;
	BrushData.SolidId = 0;

	auto MakeSide = [&](const FVector& P1, const FVector& P2, const FVector& P3) -> FImportedSideData
	{
		FImportedSideData Side;
		Side.PlaneP1 = P1;
		Side.PlaneP2 = P2;
		Side.PlaneP3 = P3;
		Side.Material = DefaultMat;
		Side.UAxisStr = TEXT("[1 0 0 0] 0.25");
		Side.VAxisStr = TEXT("[0 -1 0 0] 0.25");
		Side.LightmapScale = 16;
		return Side;
	};

	// Top face (Z+): normal points down (inward). CW from outside (looking down at top):
	BrushData.Sides.Add(MakeSide(
		FVector(-HX,  HY, HZ), FVector( HX,  HY, HZ), FVector( HX, -HY, HZ)));

	// Bottom face (Z-): normal points up (inward). CW from outside (looking up at bottom):
	BrushData.Sides.Add(MakeSide(
		FVector(-HX, -HY, -HZ), FVector( HX, -HY, -HZ), FVector( HX,  HY, -HZ)));

	// Front face (X+): normal points -X (inward).
	BrushData.Sides.Add(MakeSide(
		FVector( HX, -HY, -HZ), FVector( HX, -HY,  HZ), FVector( HX,  HY,  HZ)));

	// Back face (X-): normal points +X (inward).
	BrushData.Sides.Add(MakeSide(
		FVector(-HX,  HY, -HZ), FVector(-HX,  HY,  HZ), FVector(-HX, -HY,  HZ)));

	// Right face (Y+): normal points -Y (inward).
	BrushData.Sides.Add(MakeSide(
		FVector( HX,  HY, -HZ), FVector( HX,  HY,  HZ), FVector(-HX,  HY,  HZ)));

	// Left face (Y-): normal points +Y (inward).
	BrushData.Sides.Add(MakeSide(
		FVector(-HX, -HY, -HZ), FVector(-HX, -HY,  HZ), FVector( HX, -HY,  HZ)));

	StoredBrushData.Empty();
	StoredBrushData.Add(MoveTemp(BrushData));

	// Now build the visual ProceduralMeshComponent from StoredBrushData.
	// We generate a simple box mesh in UE local space (centered at origin).
	RebuildGeometryFromDimensions();
}

void ASourceBrushEntity::RebuildGeometryFromDimensions()
{
	// Clear existing meshes
	for (UProceduralMeshComponent* Mesh : BrushMeshes)
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	BrushMeshes.Empty();

	if (StoredBrushData.Num() == 0)
		return;

	// Regenerate StoredBrushData planes from dimensions if this is generated geometry
	if (bIsGeneratedGeometry && StoredBrushData.Num() == 1 && StoredBrushData[0].Sides.Num() == 6)
	{
		const double HX = BrushDimensions.X * 0.5;
		const double HY = BrushDimensions.Y * 0.5;
		const double HZ = BrushDimensions.Z * 0.5;

		auto UpdatePlane = [](FImportedSideData& Side, const FVector& P1, const FVector& P2, const FVector& P3)
		{
			Side.PlaneP1 = P1;
			Side.PlaneP2 = P2;
			Side.PlaneP3 = P3;
		};

		TArray<FImportedSideData>& Sides = StoredBrushData[0].Sides;
		UpdatePlane(Sides[0], FVector(-HX,  HY, HZ), FVector( HX,  HY, HZ), FVector( HX, -HY, HZ));
		UpdatePlane(Sides[1], FVector(-HX, -HY, -HZ), FVector( HX, -HY, -HZ), FVector( HX,  HY, -HZ));
		UpdatePlane(Sides[2], FVector( HX, -HY, -HZ), FVector( HX, -HY,  HZ), FVector( HX,  HY,  HZ));
		UpdatePlane(Sides[3], FVector(-HX,  HY, -HZ), FVector(-HX,  HY,  HZ), FVector(-HX, -HY,  HZ));
		UpdatePlane(Sides[4], FVector( HX,  HY, -HZ), FVector( HX,  HY,  HZ), FVector(-HX,  HY,  HZ));
		UpdatePlane(Sides[5], FVector(-HX, -HY, -HZ), FVector(-HX, -HY,  HZ), FVector( HX, -HY,  HZ));
	}

	// Build a simple box ProceduralMeshComponent from the dimensions.
	// Convert Source units to UE units for the local-space mesh.
	const double Scale = 1.0 / FSourceCoord::ScaleFactor; // Source units → UE units
	const double HX = BrushDimensions.X * 0.5 * Scale;
	const double HY = BrushDimensions.Y * 0.5 * Scale;
	const double HZ = BrushDimensions.Z * 0.5 * Scale;

	// UE local space: negate Y for Source→UE handedness conversion
	// 8 vertices of the box
	const FVector V000(-HX, -HY, -HZ);
	const FVector V001(-HX, -HY,  HZ);
	const FVector V010(-HX,  HY, -HZ);
	const FVector V011(-HX,  HY,  HZ);
	const FVector V100( HX, -HY, -HZ);
	const FVector V101( HX, -HY,  HZ);
	const FVector V110( HX,  HY, -HZ);
	const FVector V111( HX,  HY,  HZ);

	// 6 faces, each a quad (4 verts, 2 triangles)
	struct FBoxFace
	{
		FVector Verts[4]; // CW winding for outward-facing normal
		FVector Normal;
	};

	TArray<FBoxFace> Faces;
	// Top (+Z)
	Faces.Add({{V011, V111, V101, V001}, FVector(0, 0, 1)});
	// Bottom (-Z)
	Faces.Add({{V000, V100, V110, V010}, FVector(0, 0, -1)});
	// Front (+X)
	Faces.Add({{V100, V101, V111, V110}, FVector(1, 0, 0)});
	// Back (-X)
	Faces.Add({{V010, V011, V001, V000}, FVector(-1, 0, 0)});
	// Right (+Y)
	Faces.Add({{V110, V111, V011, V010}, FVector(0, 1, 0)});
	// Left (-Y)
	Faces.Add({{V000, V001, V101, V100}, FVector(0, -1, 0)});

	UProceduralMeshComponent* ProcMesh = NewObject<UProceduralMeshComponent>(this, TEXT("BrushMesh_0"));
	ProcMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	ProcMesh->SetRelativeTransform(FTransform::Identity);

	// One mesh section per face for per-face material support
	const FString DefaultMat = GetDefaultMaterialForClassname();
	const TArray<FImportedSideData>& Sides = StoredBrushData[0].Sides;

	for (int32 FaceIdx = 0; FaceIdx < Faces.Num(); FaceIdx++)
	{
		const FBoxFace& Face = Faces[FaceIdx];

		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> FaceNormals;
		TArray<FVector2D> UVs;

		for (int32 i = 0; i < 4; i++)
		{
			Vertices.Add(Face.Verts[i]);
			FaceNormals.Add(Face.Normal);
		}

		UVs.Add(FVector2D(0, 0));
		UVs.Add(FVector2D(1, 0));
		UVs.Add(FVector2D(1, 1));
		UVs.Add(FVector2D(0, 1));

		Triangles.Add(0); Triangles.Add(1); Triangles.Add(2);
		Triangles.Add(0); Triangles.Add(2); Triangles.Add(3);

		ProcMesh->CreateMeshSection_LinearColor(FaceIdx, Vertices, Triangles, FaceNormals, UVs,
			TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);

		// Per-face material from StoredBrushData
		FString MatPath = Sides.IsValidIndex(FaceIdx) ? Sides[FaceIdx].Material : DefaultMat;
		if (MatPath.IsEmpty()) MatPath = DefaultMat;
		UMaterialInterface* Material = FMaterialImporter::ResolveSourceMaterial(MatPath);
		if (Material)
		{
			ProcMesh->SetMaterial(FaceIdx, Material);
		}
	}

	ProcMesh->RegisterComponent();
	BrushMeshes.Add(ProcMesh);
}

void ASourceBrushEntity::SetFaceMaterial(int32 BrushIndex, int32 FaceIndex, const FString& NewMaterial)
{
	if (!StoredBrushData.IsValidIndex(BrushIndex)) return;
	FImportedBrushData& Brush = StoredBrushData[BrushIndex];
	if (!Brush.Sides.IsValidIndex(FaceIndex)) return;

	Modify();
	Brush.Sides[FaceIndex].Material = NewMaterial;

	// Update the visual material on the proc mesh
	if (BrushMeshes.IsValidIndex(BrushIndex) && BrushMeshes[BrushIndex])
	{
		UMaterialInterface* Mat = FMaterialImporter::ResolveSourceMaterial(NewMaterial);
		if (Mat)
		{
			BrushMeshes[BrushIndex]->SetMaterial(FaceIndex, Mat);
		}
	}
}

void ASourceBrushEntity::SetAllFacesMaterial(int32 BrushIndex, const FString& NewMaterial)
{
	if (!StoredBrushData.IsValidIndex(BrushIndex)) return;

	Modify();
	for (int32 i = 0; i < StoredBrushData[BrushIndex].Sides.Num(); i++)
	{
		StoredBrushData[BrushIndex].Sides[i].Material = NewMaterial;
	}

	// Update all visual materials
	if (BrushMeshes.IsValidIndex(BrushIndex) && BrushMeshes[BrushIndex])
	{
		UMaterialInterface* Mat = FMaterialImporter::ResolveSourceMaterial(NewMaterial);
		if (Mat)
		{
			for (int32 i = 0; i < StoredBrushData[BrushIndex].Sides.Num(); i++)
			{
				BrushMeshes[BrushIndex]->SetMaterial(i, Mat);
			}
		}
	}
}

FString ASourceBrushEntity::GetFaceLabel(int32 FaceIndex, int32 TotalFaces)
{
	// Standard 6-face box labels
	if (TotalFaces == 6)
	{
		static const TCHAR* Labels[] = {
			TEXT("Top"), TEXT("Bottom"), TEXT("Front"), TEXT("Back"), TEXT("Right"), TEXT("Left")
		};
		if (FaceIndex >= 0 && FaceIndex < 6)
		{
			return Labels[FaceIndex];
		}
	}
	return FString::Printf(TEXT("Face %d"), FaceIndex);
}

#if WITH_EDITOR
void ASourceBrushEntity::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.Property)
		return;

	const FName PropName = PropertyChangedEvent.Property->GetFName();

	if (PropName == GET_MEMBER_NAME_CHECKED(ASourceBrushEntity, BrushDimensions))
	{
		// Clamp dimensions to reasonable values (minimum 1 Source unit per axis)
		BrushDimensions.X = FMath::Max(1.0, BrushDimensions.X);
		BrushDimensions.Y = FMath::Max(1.0, BrushDimensions.Y);
		BrushDimensions.Z = FMath::Max(1.0, BrushDimensions.Z);

		if (bIsGeneratedGeometry)
		{
			RebuildGeometryFromDimensions();
		}
	}
}
#endif

// ---- Env Sprite ----

ASourceEnvSprite::ASourceEnvSprite()
{
	SourceClassname = TEXT("env_sprite");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Soundscape ----

ASourceSoundscape::ASourceSoundscape()
{
	SourceClassname = TEXT("env_soundscape");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Spectator Spawn ----

ASourceSpectatorSpawn::ASourceSpectatorSpawn()
{
	SourceClassname = TEXT("info_player_spectator");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Yellow;
		Arrow->ArrowSize = 1.5f;
		Arrow->ArrowLength = 80.0f;
		Arrow->SetHiddenInGame(true);
	}
#endif
}

// ---- Goal Trigger (Soccer) ----

ASourceGoalTrigger::ASourceGoalTrigger()
{
	SourceClassname = TEXT("trigger_multiple");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Ball Spawn (Soccer) ----

ASourceBallSpawn::ASourceBallSpawn()
{
	SourceClassname = TEXT("info_target");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("SpawnArrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Green;
		Arrow->ArrowSize = 1.0f;
		Arrow->ArrowLength = 60.0f;
		Arrow->SetRelativeRotation(FRotator(-90, 0, 0)); // Point up
		Arrow->SetHiddenInGame(true);
	}
#endif
}

// ---- Spectator Camera ----

ASourceSpectatorCamera::ASourceSpectatorCamera()
{
	SourceClassname = TEXT("point_viewcontrol");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("CameraDir"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Cyan;
		Arrow->ArrowSize = 2.0f;
		Arrow->ArrowLength = 100.0f;
		Arrow->SetHiddenInGame(true);
	}
#endif
}
