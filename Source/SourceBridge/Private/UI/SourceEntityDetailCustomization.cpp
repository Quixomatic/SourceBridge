#include "UI/SourceEntityDetailCustomization.h"
#include "Actors/SourceEntityActor.h"
#include "SourceBridgeModule.h"
#include "Entities/FGDParser.h"
#include "Models/SourceModelManifest.h"
#include "Import/SourceSoundManifest.h"
#include "Materials/SourceMaterialManifest.h"
#include "Sound/SoundWave.h"
#include "Editor.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "SourceEntityDetail"

TSharedRef<IDetailCustomization> FSourceEntityDetailCustomization::MakeInstance()
{
	return MakeShareable(new FSourceEntityDetailCustomization);
}

void FSourceEntityDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Get the selected objects
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;
	DetailBuilder.GetObjectsBeingCustomized(SelectedObjects);

	ASourceEntityActor* SourceActor = nullptr;
	if (SelectedObjects.Num() > 0 && SelectedObjects[0].IsValid())
	{
		SourceActor = Cast<ASourceEntityActor>(SelectedObjects[0].Get());
	}

	CachedActor = SourceActor;

	// Reorder categories: put Source Entity at the top
	IDetailCategoryBuilder& SourceCategory = DetailBuilder.EditCategory(
		TEXT("Source Entity"),
		LOCTEXT("SourceEntityCategory", "Source Entity"),
		ECategoryPriority::Important);

	// Add FGD validation info
	if (SourceActor && !SourceActor->SourceClassname.IsEmpty())
	{
		const FFGDDatabase& FGD = FSourceBridgeModule::GetFGDDatabase();

		if (FGD.Classes.Num() > 0)
		{
			const FFGDEntityClass* FGDClass = FGD.FindClass(SourceActor->SourceClassname);

			FString ValidationText;
			FLinearColor ValidationColor;

			if (FGDClass)
			{
				ValidationText = FString::Printf(TEXT("Valid entity class (%s)"),
					FGDClass->bIsSolid ? TEXT("Brush") : TEXT("Point"));
				ValidationColor = FLinearColor::Green;

				// Show description if available
				if (!FGDClass->Description.IsEmpty())
				{
					SourceCategory.AddCustomRow(LOCTEXT("FGDDesc", "Description"))
						.NameContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FGDDescLabel", "FGD Description"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
						.ValueContent()
						[
							SNew(STextBlock)
							.Text(FText::FromString(FGDClass->Description))
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.AutoWrapText(true)
						];
				}

				// Show keyvalue count
				FFGDEntityClass Resolved = FGD.GetResolved(SourceActor->SourceClassname);
				SourceCategory.AddCustomRow(LOCTEXT("FGDInfo", "FGD Info"))
					.NameContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("FGDInfoLabel", "FGD Schema"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					.ValueContent()
					[
						SNew(STextBlock)
						.Text(FText::Format(
							LOCTEXT("FGDInfoValue", "{0} keyvalues, {1} inputs, {2} outputs"),
							FText::AsNumber(Resolved.Properties.Num()),
							FText::AsNumber(Resolved.Inputs.Num()),
							FText::AsNumber(Resolved.Outputs.Num())))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					];

				// Build dynamic property widgets from FGD
				BuildFGDPropertyWidgets(DetailBuilder, SourceActor, Resolved);
			}
			else
			{
				ValidationText = TEXT("Unknown classname (not in FGD)");
				ValidationColor = FLinearColor(1.0f, 0.5f, 0.0f); // Orange warning
			}

			SourceCategory.AddCustomRow(LOCTEXT("FGDValid", "Validation"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FGDValidLabel", "FGD Validation"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(ValidationText))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor(ValidationColor))
				];
		}
		else
		{
			SourceCategory.AddCustomRow(LOCTEXT("NoFGD", "No FGD"))
				.WholeRowContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoFGDText", "No FGD loaded. Use SourceBridge.LoadFGD to enable entity validation."))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
				];
		}
	}

	// Build per-face material UI for brush entities
	if (ASourceBrushEntity* BrushActor = Cast<ASourceBrushEntity>(SourceActor))
	{
		BuildFaceMaterialWidgets(DetailBuilder, BrushActor);
	}

	// Reorder subclass-specific categories to appear after Source Entity
	DetailBuilder.EditCategory(TEXT("Source Trigger"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Light"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Prop"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Sprite"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Soundscape"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Soccer"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Camera"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
}

void FSourceEntityDetailCustomization::BuildFGDPropertyWidgets(
	IDetailLayoutBuilder& DetailBuilder,
	ASourceEntityActor* Actor,
	const FFGDEntityClass& Resolved)
{
	if (!Actor) return;

	// Skip these keyvalues - they're handled by native UPROPERTYs
	static const TSet<FString> SkipKeys = {
		TEXT("origin"),
		TEXT("angles"),
		TEXT("targetname"),
		TEXT("classname")
	};

	// Create a category for FGD-driven properties
	IDetailCategoryBuilder& FGDCategory = DetailBuilder.EditCategory(
		TEXT("FGD Properties"),
		LOCTEXT("FGDPropertiesCategory", "FGD Properties"),
		ECategoryPriority::Default);

	TWeakObjectPtr<ASourceEntityActor> WeakActor = Actor;

	for (const FFGDProperty& Prop : Resolved.Properties)
	{
		// Skip properties handled natively
		if (SkipKeys.Contains(Prop.Name.ToLower()))
		{
			continue;
		}

		// Spawnflags get special handling
		if (Prop.Type == EFGDPropertyType::Flags)
		{
			// Add a sub-header for spawnflags
			FGDCategory.AddCustomRow(LOCTEXT("SpawnflagsHeader", "Spawnflags"))
				.WholeRowContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SpawnflagsLabel", "Spawnflags"))
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
				];

			// Add a checkbox for each flag bit
			for (const FFGDFlag& Flag : Prop.Flags)
			{
				int32 FlagBit = Flag.Bit;
				FString FlagName = Flag.DisplayName;

				FGDCategory.AddCustomRow(FText::FromString(FlagName))
					.NameContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString(FlagName))
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.ToolTipText(FText::Format(
							LOCTEXT("FlagBitTooltip", "Spawnflag bit {0}"),
							FText::AsNumber(FlagBit)))
					]
					.ValueContent()
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([WeakActor, FlagBit]() -> ECheckBoxState
						{
							if (ASourceEntityActor* A = WeakActor.Get())
							{
								return (A->SpawnFlags & FlagBit) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							}
							return ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([WeakActor, FlagBit](ECheckBoxState NewState)
						{
							if (ASourceEntityActor* A = WeakActor.Get())
							{
								A->Modify();
								if (NewState == ECheckBoxState::Checked)
								{
									A->SpawnFlags |= FlagBit;
								}
								else
								{
									A->SpawnFlags &= ~FlagBit;
								}
							}
						})
					];
			}
			continue;
		}

		// Determine display name
		FString DisplayName = Prop.DisplayName.IsEmpty() ? Prop.Name : Prop.DisplayName;
		FString KeyName = Prop.Name;

		// Build tooltip from description + type info
		FString Tooltip = Prop.Description;
		if (Tooltip.IsEmpty())
		{
			Tooltip = FString::Printf(TEXT("Keyvalue: %s"), *KeyName);
		}

		if (Prop.Type == EFGDPropertyType::Choices && Prop.Choices.Num() > 0)
		{
			// --- Choices: SComboBox dropdown ---

			// Build shared list of choice display strings for the combo source
			// IMPORTANT: ChoiceItems must be captured by lambdas to prevent dangling pointer -
			// SComboBox::OptionsSource stores a raw pointer, so the array must outlive the widget
			TSharedPtr<TArray<TSharedPtr<FString>>> ChoiceItems = MakeShared<TArray<TSharedPtr<FString>>>();
			for (const FFGDChoice& Choice : Prop.Choices)
			{
				ChoiceItems->Add(MakeShared<FString>(Choice.DisplayName));
			}

			// Capture choices array for value lookup
			TArray<FFGDChoice> Choices = Prop.Choices;

			// Find current selection index
			FString CurrentValue = GetKeyValue(Actor, Prop);
			int32 SelectedIndex = 0;
			for (int32 i = 0; i < Choices.Num(); ++i)
			{
				if (Choices[i].Value == CurrentValue)
				{
					SelectedIndex = i;
					break;
				}
			}

			TSharedPtr<FString> InitialSelection;
			if (ChoiceItems->IsValidIndex(SelectedIndex))
			{
				InitialSelection = (*ChoiceItems)[SelectedIndex];
			}

			FGDCategory.AddCustomRow(FText::FromString(DisplayName))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(DisplayName))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ToolTipText(FText::FromString(Tooltip))
				]
				.ValueContent()
				.MinDesiredWidth(200.0f)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(ChoiceItems.Get())
					.InitiallySelectedItem(InitialSelection)
					.OnGenerateWidget_Lambda([ChoiceItems](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
					{
						return SNew(STextBlock)
							.Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty())
							.Font(IDetailLayoutBuilder::GetDetailFont());
					})
					.OnSelectionChanged_Lambda([WeakActor, KeyName, Choices, ChoiceItems](
						TSharedPtr<FString> Selected, ESelectInfo::Type)
					{
						if (!Selected.IsValid()) return;
						if (ASourceEntityActor* A = WeakActor.Get())
						{
							// Find the value for this display name
							for (const FFGDChoice& C : Choices)
							{
								if (C.DisplayName == *Selected)
								{
									SetKeyValue(A, KeyName, C.Value);
									break;
								}
							}
						}
					})
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text_Lambda([WeakActor, KeyName, Choices, ChoiceItems]() -> FText
						{
							if (ASourceEntityActor* A = WeakActor.Get())
							{
								const FString* Val = A->KeyValues.Find(KeyName);
								if (Val)
								{
									for (const FFGDChoice& C : Choices)
									{
										if (C.Value == *Val)
										{
											return FText::FromString(C.DisplayName);
										}
									}
									return FText::FromString(*Val);
								}
							}
							// Show default
							if (Choices.Num() > 0)
							{
								return FText::FromString(Choices[0].DisplayName);
							}
							return FText::GetEmpty();
						})
					]
				];
		}
		else if (Prop.Type == EFGDPropertyType::Integer)
		{
			// --- Integer: numeric text box ---
			FGDCategory.AddCustomRow(FText::FromString(DisplayName))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(DisplayName))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ToolTipText(FText::FromString(Tooltip))
				]
				.ValueContent()
				[
					SNew(SEditableTextBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([WeakActor, KeyName, Prop]() -> FText
					{
						if (ASourceEntityActor* A = WeakActor.Get())
						{
							const FString* Val = A->KeyValues.Find(KeyName);
							if (Val) return FText::FromString(*Val);
						}
						return FText::FromString(Prop.DefaultValue);
					})
					.OnTextCommitted_Lambda([WeakActor, KeyName](const FText& NewText, ETextCommit::Type)
					{
						if (ASourceEntityActor* A = WeakActor.Get())
						{
							// Validate integer
							FString Str = NewText.ToString().TrimStartAndEnd();
							if (Str.IsNumeric() || (Str.StartsWith(TEXT("-")) && Str.Mid(1).IsNumeric()))
							{
								SetKeyValue(A, KeyName, Str);
							}
						}
					})
				];
		}
		else if (Prop.Type == EFGDPropertyType::Float)
		{
			// --- Float: numeric text box ---
			FGDCategory.AddCustomRow(FText::FromString(DisplayName))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(DisplayName))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ToolTipText(FText::FromString(Tooltip))
				]
				.ValueContent()
				[
					SNew(SEditableTextBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([WeakActor, KeyName, Prop]() -> FText
					{
						if (ASourceEntityActor* A = WeakActor.Get())
						{
							const FString* Val = A->KeyValues.Find(KeyName);
							if (Val) return FText::FromString(*Val);
						}
						return FText::FromString(Prop.DefaultValue);
					})
					.OnTextCommitted_Lambda([WeakActor, KeyName](const FText& NewText, ETextCommit::Type)
					{
						if (ASourceEntityActor* A = WeakActor.Get())
						{
							FString Str = NewText.ToString().TrimStartAndEnd();
							if (FCString::Atof(*Str) != 0.0f || Str == TEXT("0") || Str == TEXT("0.0"))
							{
								SetKeyValue(A, KeyName, Str);
							}
						}
					})
				];
		}
		else if (Prop.Type == EFGDPropertyType::Color255)
		{
			// --- Color255: "R G B" text entry ---
			FGDCategory.AddCustomRow(FText::FromString(DisplayName))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(DisplayName))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ToolTipText(FText::FromString(Tooltip + TEXT(" (R G B, 0-255)")))
				]
				.ValueContent()
				[
					SNew(SEditableTextBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([WeakActor, KeyName, Prop]() -> FText
					{
						if (ASourceEntityActor* A = WeakActor.Get())
						{
							const FString* Val = A->KeyValues.Find(KeyName);
							if (Val) return FText::FromString(*Val);
						}
						return FText::FromString(Prop.DefaultValue.IsEmpty() ? TEXT("255 255 255") : Prop.DefaultValue);
					})
					.OnTextCommitted_Lambda([WeakActor, KeyName](const FText& NewText, ETextCommit::Type)
					{
						if (ASourceEntityActor* A = WeakActor.Get())
						{
							SetKeyValue(A, KeyName, NewText.ToString().TrimStartAndEnd());
						}
					})
				];
		}
		else
		{
			// Detect if this property references a Source asset (model, sound, material)
			enum class ESmartAsset : uint8 { None, Model, Sound, Material };
			ESmartAsset SmartType = ESmartAsset::None;

			// Direct FGD type detection
			if (Prop.Type == EFGDPropertyType::Studio || Prop.Type == EFGDPropertyType::Sprite)
				SmartType = ESmartAsset::Model;
			else if (Prop.Type == EFGDPropertyType::Sound)
				SmartType = ESmartAsset::Sound;
			else if (Prop.Type == EFGDPropertyType::Material || Prop.Type == EFGDPropertyType::Decal)
				SmartType = ESmartAsset::Material;

			// Name-based fallback for string-typed properties that are actually asset refs
			if (SmartType == ESmartAsset::None && Actor)
			{
				FString LowerName = Prop.Name.ToLower();
				FString ClassName = Actor->SourceClassname.ToLower();

				if (LowerName == TEXT("message") && ClassName == TEXT("ambient_generic"))
					SmartType = ESmartAsset::Sound;
				else if (LowerName == TEXT("startsound") || LowerName == TEXT("stopsound") || LowerName == TEXT("movesound"))
					SmartType = ESmartAsset::Sound;
				else if (LowerName.StartsWith(TEXT("scape")) && LowerName.Len() <= 6)
					SmartType = ESmartAsset::Sound;
			}

			// Build the text box (always present as source of truth)
			TSharedRef<SEditableTextBox> TextBox =
				SNew(SEditableTextBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.IsReadOnly(Prop.bReadOnly)
				.Text_Lambda([WeakActor, KeyName, Prop]() -> FText
				{
					if (ASourceEntityActor* A = WeakActor.Get())
					{
						const FString* Val = A->KeyValues.Find(KeyName);
						if (Val) return FText::FromString(*Val);
					}
					return FText::FromString(Prop.DefaultValue);
				})
				.OnTextCommitted_Lambda([WeakActor, KeyName](const FText& NewText, ETextCommit::Type)
				{
					if (ASourceEntityActor* A = WeakActor.Get())
					{
						SetKeyValue(A, KeyName, NewText.ToString());
					}
				});

			if (SmartType == ESmartAsset::None)
			{
				// --- Plain text box (no asset reference detected) ---
				FGDCategory.AddCustomRow(FText::FromString(DisplayName))
					.NameContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString(DisplayName))
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.ToolTipText(FText::FromString(Tooltip))
					]
					.ValueContent()
					.MinDesiredWidth(200.0f)
					[
						TextBox
					];
			}
			else
			{
				// --- Smart asset widget: text box + picker buttons ---
				TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						TextBox
					];

				// Sound: add play/stop button
				if (SmartType == ESmartAsset::Sound)
				{
					Row->AddSlot()
					.AutoWidth()
					.Padding(2.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(FMargin(2.0f))
						.ToolTipText(LOCTEXT("PlaySoundTip", "Play/Stop this sound"))
						.OnClicked_Lambda([WeakActor, KeyName]() -> FReply
						{
							if (!GEditor) return FReply::Handled();

							ASourceEntityActor* A = WeakActor.Get();
							if (!A) return FReply::Handled();

							const FString* Val = A->KeyValues.Find(KeyName);
							if (!Val || Val->IsEmpty())
							{
								GEditor->ResetPreviewAudioComponent();
								return FReply::Handled();
							}

							// Look up sound in manifest
							USourceSoundManifest* SndManifest = USourceSoundManifest::Get();
							if (!SndManifest) return FReply::Handled();

							// Try with and without "sound/" prefix
							FString SearchPath = *Val;
							FSourceSoundEntry* Entry = SndManifest->FindBySourcePath(SearchPath);
							if (!Entry && !SearchPath.StartsWith(TEXT("sound/")))
							{
								Entry = SndManifest->FindBySourcePath(TEXT("sound/") + SearchPath);
							}
							if (!Entry)
							{
								// Try stripping sound/ prefix
								FString Stripped = SearchPath;
								if (Stripped.RemoveFromStart(TEXT("sound/")))
									Entry = SndManifest->FindBySourcePath(Stripped);
							}

							if (Entry)
							{
								USoundWave* Sound = Cast<USoundWave>(Entry->SoundAsset.TryLoad());
								if (Sound)
								{
									GEditor->PlayPreviewSound(Sound);
									return FReply::Handled();
								}
							}

							GEditor->ResetPreviewAudioComponent();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.Text(FText::FromString(TEXT("\u25B6")))
						]
					];
				}

				// Browse/pick button: opens a menu listing manifest entries
				Row->AddSlot()
				.AutoWidth()
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SComboButton)
					.HasDownArrow(true)
					.ContentPadding(FMargin(2.0f, 0.0f))
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("BrowseAssetTip", "Pick from imported assets"))
					.OnGetMenuContent_Lambda([WeakActor, KeyName, SmartType]() -> TSharedRef<SWidget>
					{
						FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

						if (SmartType == ESmartAsset::Model)
						{
							USourceModelManifest* Manifest = USourceModelManifest::Get();
							if (Manifest && Manifest->Entries.Num() > 0)
							{
								for (const FSourceModelEntry& MEntry : Manifest->Entries)
								{
									FString Path = MEntry.SourcePath;
									MenuBuilder.AddMenuEntry(
										FText::FromString(Path),
										FText::GetEmpty(),
										FSlateIcon(),
										FUIAction(FExecuteAction::CreateLambda([WeakActor, KeyName, Path]()
										{
											if (ASourceEntityActor* A = WeakActor.Get())
												FSourceEntityDetailCustomization::SetKeyValue(A, KeyName, Path);
										}))
									);
								}
							}
							else
							{
								MenuBuilder.AddMenuEntry(
									LOCTEXT("NoModels", "No models in manifest"),
									FText::GetEmpty(), FSlateIcon(), FUIAction());
							}
						}
						else if (SmartType == ESmartAsset::Sound)
						{
							USourceSoundManifest* Manifest = USourceSoundManifest::Get();
							if (Manifest && Manifest->Entries.Num() > 0)
							{
								for (const FSourceSoundEntry& SEntry : Manifest->Entries)
								{
									FString Path = SEntry.SourcePath;
									MenuBuilder.AddMenuEntry(
										FText::FromString(Path),
										FText::GetEmpty(),
										FSlateIcon(),
										FUIAction(FExecuteAction::CreateLambda([WeakActor, KeyName, Path]()
										{
											if (ASourceEntityActor* A = WeakActor.Get())
												FSourceEntityDetailCustomization::SetKeyValue(A, KeyName, Path);
										}))
									);
								}
							}
							else
							{
								MenuBuilder.AddMenuEntry(
									LOCTEXT("NoSounds", "No sounds in manifest"),
									FText::GetEmpty(), FSlateIcon(), FUIAction());
							}
						}
						else if (SmartType == ESmartAsset::Material)
						{
							USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
							if (Manifest && Manifest->Entries.Num() > 0)
							{
								for (const FSourceMaterialEntry& MatEntry : Manifest->Entries)
								{
									FString Path = MatEntry.SourcePath;
									MenuBuilder.AddMenuEntry(
										FText::FromString(Path),
										FText::GetEmpty(),
										FSlateIcon(),
										FUIAction(FExecuteAction::CreateLambda([WeakActor, KeyName, Path]()
										{
											if (ASourceEntityActor* A = WeakActor.Get())
												FSourceEntityDetailCustomization::SetKeyValue(A, KeyName, Path);
										}))
									);
								}
							}
							else
							{
								MenuBuilder.AddMenuEntry(
									LOCTEXT("NoMaterials", "No materials in manifest"),
									FText::GetEmpty(), FSlateIcon(), FUIAction());
							}
						}

						return MenuBuilder.MakeWidget();
					})
					.ButtonContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text(LOCTEXT("BrowseBtn", "..."))
					]
				];

				FGDCategory.AddCustomRow(FText::FromString(DisplayName))
					.NameContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString(DisplayName))
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.ToolTipText(FText::FromString(Tooltip))
					]
					.ValueContent()
					.MinDesiredWidth(200.0f)
					[
						Row
					];
			}
		}
	}

	// Add I/O info section if inputs/outputs exist
	if (Resolved.Inputs.Num() > 0 || Resolved.Outputs.Num() > 0)
	{
		IDetailCategoryBuilder& IOCategory = DetailBuilder.EditCategory(
			TEXT("FGD I/O Reference"),
			LOCTEXT("FGDIOCategory", "FGD I/O Reference"),
			ECategoryPriority::Default);

		// List available inputs
		if (Resolved.Inputs.Num() > 0)
		{
			FString InputList;
			for (const FFGDIODef& Input : Resolved.Inputs)
			{
				if (!InputList.IsEmpty()) InputList += TEXT(", ");
				InputList += Input.Name;
			}

			IOCategory.AddCustomRow(LOCTEXT("InputsRow", "Inputs"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("InputsLabel", "Available Inputs"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(300.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(InputList))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.AutoWrapText(true)
				];
		}

		// List available outputs
		if (Resolved.Outputs.Num() > 0)
		{
			FString OutputList;
			for (const FFGDIODef& Output : Resolved.Outputs)
			{
				if (!OutputList.IsEmpty()) OutputList += TEXT(", ");
				OutputList += Output.Name;
			}

			IOCategory.AddCustomRow(LOCTEXT("OutputsRow", "Outputs"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OutputsLabel", "Available Outputs"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(300.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(OutputList))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.AutoWrapText(true)
				];
		}
	}
}

void FSourceEntityDetailCustomization::BuildFaceMaterialWidgets(
	IDetailLayoutBuilder& DetailBuilder,
	ASourceBrushEntity* BrushActor)
{
	if (!BrushActor || BrushActor->StoredBrushData.Num() == 0) return;

	IDetailCategoryBuilder& FaceCategory = DetailBuilder.EditCategory(
		TEXT("Face Materials"),
		LOCTEXT("FaceMaterialsCategory", "Face Materials"),
		ECategoryPriority::Default);

	TWeakObjectPtr<ASourceBrushEntity> WeakBrush = BrushActor;

	for (int32 BrushIdx = 0; BrushIdx < BrushActor->StoredBrushData.Num(); BrushIdx++)
	{
		const FImportedBrushData& Brush = BrushActor->StoredBrushData[BrushIdx];

		// Header for multi-solid entities
		if (BrushActor->StoredBrushData.Num() > 1)
		{
			FaceCategory.AddCustomRow(FText::FromString(FString::Printf(TEXT("Solid %d"), BrushIdx)))
				.WholeRowContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Solid %d (%d faces)"), BrushIdx, Brush.Sides.Num())))
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
				];
		}

		int32 TotalFaces = Brush.Sides.Num();
		for (int32 FaceIdx = 0; FaceIdx < TotalFaces; FaceIdx++)
		{
			FString Label = ASourceBrushEntity::GetFaceLabel(FaceIdx, TotalFaces);
			int32 CapturedBrushIdx = BrushIdx;
			int32 CapturedFaceIdx = FaceIdx;

			FaceCategory.AddCustomRow(FText::FromString(Label))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(200.0f)
				[
					SNew(SEditableTextBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([WeakBrush, CapturedBrushIdx, CapturedFaceIdx]() -> FText
					{
						if (ASourceBrushEntity* B = WeakBrush.Get())
						{
							if (B->StoredBrushData.IsValidIndex(CapturedBrushIdx) &&
								B->StoredBrushData[CapturedBrushIdx].Sides.IsValidIndex(CapturedFaceIdx))
							{
								return FText::FromString(B->StoredBrushData[CapturedBrushIdx].Sides[CapturedFaceIdx].Material);
							}
						}
						return FText::GetEmpty();
					})
					.OnTextCommitted_Lambda([WeakBrush, CapturedBrushIdx, CapturedFaceIdx](const FText& NewText, ETextCommit::Type)
					{
						if (ASourceBrushEntity* B = WeakBrush.Get())
						{
							B->SetFaceMaterial(CapturedBrushIdx, CapturedFaceIdx, NewText.ToString());
						}
					})
				];
		}

		// "Apply to All Faces" button per solid
		int32 CapturedBrushIdx = BrushIdx;
		FaceCategory.AddCustomRow(LOCTEXT("ApplyAll", "Apply to All"))
			.ValueContent()
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyAllButton", "Apply to All Faces"))
				.ToolTipText(LOCTEXT("ApplyAllTooltip", "Set all faces to the same material as the Top/first face"))
				.OnClicked_Lambda([WeakBrush, CapturedBrushIdx]() -> FReply
				{
					if (ASourceBrushEntity* B = WeakBrush.Get())
					{
						if (B->StoredBrushData.IsValidIndex(CapturedBrushIdx) &&
							B->StoredBrushData[CapturedBrushIdx].Sides.Num() > 0)
						{
							FString FirstMat = B->StoredBrushData[CapturedBrushIdx].Sides[0].Material;
							B->SetAllFacesMaterial(CapturedBrushIdx, FirstMat);
						}
					}
					return FReply::Handled();
				})
			];
	}
}

FString FSourceEntityDetailCustomization::GetKeyValue(ASourceEntityActor* Actor, const FFGDProperty& Prop)
{
	if (!Actor) return Prop.DefaultValue;

	const FString* Value = Actor->KeyValues.Find(Prop.Name);
	if (Value)
	{
		return *Value;
	}
	return Prop.DefaultValue;
}

void FSourceEntityDetailCustomization::SetKeyValue(ASourceEntityActor* Actor, const FString& Key, const FString& Value)
{
	if (!Actor) return;
	Actor->Modify();
	Actor->KeyValues.Add(Key, Value);
}

void FSourceEntityDetailCustomization::Register()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomClassLayout(
		ASourceEntityActor::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FSourceEntityDetailCustomization::MakeInstance));
}

void FSourceEntityDetailCustomization::Unregister()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(ASourceEntityActor::StaticClass()->GetFName());
	}
}

#undef LOCTEXT_NAMESPACE
