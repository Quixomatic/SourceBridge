#include "UI/SourceEntityDetailCustomization.h"
#include "Actors/SourceEntityActor.h"
#include "SourceBridgeModule.h"
#include "Entities/FGDParser.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
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
							FText::AsNumber(Resolved.KeyValues.Num()),
							FText::AsNumber(Resolved.Inputs.Num()),
							FText::AsNumber(Resolved.Outputs.Num())))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					];
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

	// Reorder subclass-specific categories to appear after Source Entity
	DetailBuilder.EditCategory(TEXT("Source Trigger"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Light"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Prop"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Sprite"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Soundscape"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Soccer"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Source Camera"), FText::GetEmpty(), ECategoryPriority::TypeSpecific);
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
