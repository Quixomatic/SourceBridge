#include "UI/SSourceIOGraphNode.h"
#include "UI/SourceIOGraph.h"
#include "UI/SourceEntityDetailCustomization.h"
#include "Actors/SourceEntityActor.h"
#include "Entities/EntityIOConnection.h"
#include "SGraphPin.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SSourceIOGraphNode"

// ============================================================================
// Font helpers
// ============================================================================

FSlateFontInfo SSourceIOGraphNode::GetNodeBodyFont()
{
	return FCoreStyle::GetDefaultFontStyle("Regular", 8);
}

FSlateFontInfo SSourceIOGraphNode::GetNodeBodyBoldFont()
{
	return FCoreStyle::GetDefaultFontStyle("Bold", 8);
}

// ============================================================================
// Construction
// ============================================================================

void SSourceIOGraphNode::Construct(const FArguments& InArgs, USourceIOGraphNode* InNode)
{
	IONode = InNode;
	GraphNode = InNode;
	UpdateGraphNode();
	SetCursor(EMouseCursor::CardinalCross);
}

// ============================================================================
// UpdateGraphNode - delegate to base class for proper rendering
// ============================================================================

void SSourceIOGraphNode::UpdateGraphNode()
{
	// Base class handles: title bar, pin layout, wire geometry,
	// selection visuals, content scaling, error reporting, etc.
	SGraphNode::UpdateGraphNode();
}

// ============================================================================
// Pin creation - delegate to base class
// ============================================================================

void SSourceIOGraphNode::CreatePinWidgets()
{
	SGraphNode::CreatePinWidgets();
}

// ============================================================================
// CreateBelowPinControls - inject FGD properties and I/O connections
// ============================================================================

void SSourceIOGraphNode::CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox)
{
	if (!IONode || !MainBox.IsValid()) return;

	PropertiesBox = SNew(SVerticalBox);
	ConnectionsBox = SNew(SVerticalBox);

	// === Properties section (collapsible) ===
	if (IONode->bHasFGDData && IONode->ResolvedFGDClass.Properties.Num() > 0)
	{
		MainBox->AddSlot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("Properties", "Properties"))
				.AreaTitleFont(GetNodeBodyBoldFont())
				.InitiallyCollapsed(!IONode->bShowProperties)
				.OnAreaExpansionChanged_Lambda([this](bool bExpanded)
				{
					if (IONode) IONode->bShowProperties = bExpanded;
				})
				.BodyContent()
				[
					PropertiesBox.ToSharedRef()
				]
				.Padding(FMargin(4, 0))
			];
	}

	// === Connections section (collapsible) ===
	bool bHasIOTags = false;
	if (IONode->SourceActor.IsValid())
	{
		for (const FName& ActorTag : IONode->SourceActor->Tags)
		{
			if (ActorTag.ToString().StartsWith(TEXT("io:"), ESearchCase::IgnoreCase))
			{
				bHasIOTags = true;
				break;
			}
		}
	}

	if (bHasIOTags)
	{
		MainBox->AddSlot()
			.AutoHeight()
			.Padding(4, 0, 4, 4)
			[
				SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("Connections", "Connections"))
				.AreaTitleFont(GetNodeBodyBoldFont())
				.InitiallyCollapsed(!IONode->bShowConnections)
				.OnAreaExpansionChanged_Lambda([this](bool bExpanded)
				{
					if (IONode) IONode->bShowConnections = bExpanded;
				})
				.BodyContent()
				[
					ConnectionsBox.ToSharedRef()
				]
				.Padding(FMargin(4, 0, 4, 4))
			];
	}

	// Populate the sections
	BuildPropertyWidgets();
	BuildConnectionWidgets();
}

// ============================================================================
// Property widgets
// ============================================================================

void SSourceIOGraphNode::BuildPropertyWidgets()
{
	if (!PropertiesBox.IsValid() || !IONode || !IONode->bHasFGDData) return;

	PropertiesBox->ClearChildren();

	TWeakObjectPtr<ASourceEntityActor> WeakActor = IONode->SourceActor;
	if (!WeakActor.IsValid()) return;

	// Skip these keyvalues - handled by native UPROPERTYs
	static const TSet<FString> SkipKeys = {
		TEXT("origin"), TEXT("angles"), TEXT("targetname"), TEXT("classname")
	};

	for (const FFGDProperty& Prop : IONode->ResolvedFGDClass.Properties)
	{
		if (SkipKeys.Contains(Prop.Name.ToLower())) continue;

		if (Prop.Type == EFGDPropertyType::Flags)
		{
			// Spawnflags header
			PropertiesBox->AddSlot()
				.AutoHeight()
				.Padding(0, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Spawnflags", "Spawnflags"))
					.Font(GetNodeBodyBoldFont())
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f)))
				];

			for (const FFGDFlag& Flag : Prop.Flags)
			{
				PropertiesBox->AddSlot()
					.AutoHeight()
					.Padding(4, 0, 0, 0)
					[
						CreateFlagWidget(Flag, WeakActor)
					];
			}
		}
		else
		{
			PropertiesBox->AddSlot()
				.AutoHeight()
				.Padding(0, 1)
				[
					CreatePropertyWidget(Prop, WeakActor)
				];
		}
	}
}

TSharedRef<SWidget> SSourceIOGraphNode::CreatePropertyWidget(
	const FFGDProperty& Prop, TWeakObjectPtr<ASourceEntityActor> WeakActor)
{
	FString DisplayName = Prop.DisplayName.IsEmpty() ? Prop.Name : Prop.DisplayName;
	FString KeyName = Prop.Name;

	// Choices dropdown
	if (Prop.Type == EFGDPropertyType::Choices && Prop.Choices.Num() > 0)
	{
		TSharedPtr<TArray<TSharedPtr<FString>>> ChoiceItems = MakeShared<TArray<TSharedPtr<FString>>>();
		for (const FFGDChoice& Choice : Prop.Choices)
		{
			ChoiceItems->Add(MakeShared<FString>(Choice.DisplayName));
		}
		TArray<FFGDChoice> Choices = Prop.Choices;

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(DisplayName))
				.Font(GetNodeBodyFont())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(ChoiceItems.Get())
				.OnGenerateWidget_Lambda([ChoiceItems](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock)
						.Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty())
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				.OnSelectionChanged_Lambda([WeakActor, KeyName, Choices, ChoiceItems](
					TSharedPtr<FString> Selected, ESelectInfo::Type)
				{
					if (!Selected.IsValid()) return;
					if (ASourceEntityActor* A = WeakActor.Get())
					{
						for (const FFGDChoice& C : Choices)
						{
							if (C.DisplayName == *Selected)
							{
								FSourceEntityDetailCustomization::SetKeyValue(A, KeyName, C.Value);
								break;
							}
						}
					}
				})
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.Text_Lambda([WeakActor, KeyName, Choices, ChoiceItems]() -> FText
					{
						if (ASourceEntityActor* A = WeakActor.Get())
						{
							const FString* Val = A->KeyValues.Find(KeyName);
							if (Val)
							{
								for (const FFGDChoice& C : Choices)
								{
									if (C.Value == *Val) return FText::FromString(C.DisplayName);
								}
								return FText::FromString(*Val);
							}
						}
						if (Choices.Num() > 0) return FText::FromString(Choices[0].DisplayName);
						return FText::GetEmpty();
					})
				]
			];
	}

	// All other types: text box
	FFGDProperty PropCopy = Prop;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(DisplayName))
			.Font(GetNodeBodyFont())
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SEditableTextBox)
			.Font(GetNodeBodyFont())
			.MinDesiredWidth(60.0f)
			.IsReadOnly(PropCopy.bReadOnly)
			.Text_Lambda([WeakActor, KeyName, PropCopy]() -> FText
			{
				if (ASourceEntityActor* A = WeakActor.Get())
				{
					const FString* Val = A->KeyValues.Find(KeyName);
					if (Val) return FText::FromString(*Val);
				}
				return FText::FromString(PropCopy.DefaultValue);
			})
			.OnTextCommitted_Lambda([WeakActor, KeyName, PropCopy](const FText& NewText, ETextCommit::Type)
			{
				if (ASourceEntityActor* A = WeakActor.Get())
				{
					FString Str = NewText.ToString().TrimStartAndEnd();

					// Basic validation
					if (PropCopy.Type == EFGDPropertyType::Integer)
					{
						if (!Str.IsNumeric() && !(Str.StartsWith(TEXT("-")) && Str.Mid(1).IsNumeric()))
							return;
					}
					else if (PropCopy.Type == EFGDPropertyType::Float)
					{
						if (FCString::Atof(*Str) == 0.0f && Str != TEXT("0") && Str != TEXT("0.0"))
							return;
					}

					FSourceEntityDetailCustomization::SetKeyValue(A, KeyName, Str);
				}
			})
		];
}

TSharedRef<SWidget> SSourceIOGraphNode::CreateFlagWidget(
	const FFGDFlag& Flag, TWeakObjectPtr<ASourceEntityActor> WeakActor)
{
	int32 FlagBit = Flag.Bit;

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
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
						A->SpawnFlags |= FlagBit;
					else
						A->SpawnFlags &= ~FlagBit;
				}
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0, 0, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Flag.DisplayName))
			.Font(GetNodeBodyFont())
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		];
}

// ============================================================================
// Connection widgets
// ============================================================================

void SSourceIOGraphNode::BuildConnectionWidgets()
{
	if (!ConnectionsBox.IsValid() || !IONode || !IONode->SourceActor.IsValid()) return;

	ConnectionsBox->ClearChildren();

	TWeakObjectPtr<ASourceEntityActor> WeakActor = IONode->SourceActor;
	ASourceEntityActor* Actor = WeakActor.Get();
	if (!Actor) return;

	for (int32 i = 0; i < Actor->Tags.Num(); ++i)
	{
		FString TagStr = Actor->Tags[i].ToString();
		if (!TagStr.StartsWith(TEXT("io:"), ESearchCase::IgnoreCase)) continue;

		ConnectionsBox->AddSlot()
			.AutoHeight()
			.Padding(0, 1)
			[
				CreateConnectionRowWidget(i, TagStr, WeakActor)
			];
	}
}

TSharedRef<SWidget> SSourceIOGraphNode::CreateConnectionRowWidget(
	int32 TagIndex, const FString& TagStr, TWeakObjectPtr<ASourceEntityActor> WeakActor)
{
	FEntityIOConnection Conn;
	if (!FEntityIOConnection::ParseFromTag(TagStr, Conn))
	{
		return SNew(STextBlock)
			.Text(FText::FromString(TagStr))
			.Font(GetNodeBodyFont())
			.ColorAndOpacity(FSlateColor(FLinearColor::Red));
	}

	FString Summary = Conn.Parameter.IsEmpty()
		? FString::Printf(TEXT("%s -> %s.%s"), *Conn.OutputName, *Conn.TargetEntity, *Conn.InputName)
		: FString::Printf(TEXT("%s -> %s.%s (%s)"), *Conn.OutputName, *Conn.TargetEntity, *Conn.InputName, *Conn.Parameter);

	// Capture values for editing
	FString OutputName = Conn.OutputName;
	FString TargetEntity = Conn.TargetEntity;
	FString InputName = Conn.InputName;
	FString Parameter = Conn.Parameter;
	float Delay = Conn.Delay;
	int32 Refire = Conn.RefireCount;

	return SNew(SVerticalBox)
		// Connection summary + delete button
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Summary))
				.Font(GetNodeBodyFont())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.6f)))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(1))
				.ToolTipText(LOCTEXT("DeleteConn", "Remove this connection"))
				.OnClicked_Lambda([WeakActor, TagIndex, this]() -> FReply
				{
					if (ASourceEntityActor* A = WeakActor.Get())
					{
						if (A->Tags.IsValidIndex(TagIndex))
						{
							A->Modify();
							A->Tags.RemoveAt(TagIndex);
							BuildConnectionWidgets();
						}
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("X")))
					.Font(GetNodeBodyFont())
					.ColorAndOpacity(FSlateColor(FLinearColor::Red))
				]
			]
		]

		// Detail line: param, delay, refire (editable)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 0, 0, 2)
		[
			SNew(SHorizontalBox)

			// Parameter
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ParamLabel", "param:"))
				.Font(GetNodeBodyFont())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 6, 0)
			[
				SNew(SEditableTextBox)
				.Font(GetNodeBodyFont())
				.MinDesiredWidth(60.0f)
				.Text(FText::FromString(Parameter))
				.OnTextCommitted_Lambda([WeakActor, TagIndex, OutputName, TargetEntity, InputName, Delay, Refire](
					const FText& NewText, ETextCommit::Type)
				{
					if (ASourceEntityActor* A = WeakActor.Get())
					{
						if (!A->Tags.IsValidIndex(TagIndex)) return;
						FString NewParam = NewText.ToString().TrimStartAndEnd();
						FString NewTag = FString::Printf(TEXT("io:%s:%s,%s,%s,%g,%d"),
							*OutputName, *TargetEntity, *InputName, *NewParam, Delay, Refire);
						A->Modify();
						A->Tags[TagIndex] = FName(*NewTag);
					}
				})
			]

			// Delay
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DelayLabel", "delay:"))
				.Font(GetNodeBodyFont())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 6, 0)
			[
				SNew(SEditableTextBox)
				.Font(GetNodeBodyFont())
				.MinDesiredWidth(30.0f)
				.Text(FText::FromString(FString::Printf(TEXT("%.1f"), Delay)))
				.OnTextCommitted_Lambda([WeakActor, TagIndex, OutputName, TargetEntity, InputName, Parameter, Refire](
					const FText& NewText, ETextCommit::Type)
				{
					if (ASourceEntityActor* A = WeakActor.Get())
					{
						if (!A->Tags.IsValidIndex(TagIndex)) return;
						float NewDelay = FCString::Atof(*NewText.ToString());
						FString NewTag = FString::Printf(TEXT("io:%s:%s,%s,%s,%g,%d"),
							*OutputName, *TargetEntity, *InputName, *Parameter, NewDelay, Refire);
						A->Modify();
						A->Tags[TagIndex] = FName(*NewTag);
					}
				})
			]

			// Refire
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RefireLabel", "refire:"))
				.Font(GetNodeBodyFont())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SEditableTextBox)
				.Font(GetNodeBodyFont())
				.MinDesiredWidth(25.0f)
				.Text(FText::FromString(FString::Printf(TEXT("%d"), Refire)))
				.OnTextCommitted_Lambda([WeakActor, TagIndex, OutputName, TargetEntity, InputName, Parameter, Delay](
					const FText& NewText, ETextCommit::Type)
				{
					if (ASourceEntityActor* A = WeakActor.Get())
					{
						if (!A->Tags.IsValidIndex(TagIndex)) return;
						int32 NewRefire = FCString::Atoi(*NewText.ToString());
						FString NewTag = FString::Printf(TEXT("io:%s:%s,%s,%s,%g,%d"),
							*OutputName, *TargetEntity, *InputName, *Parameter, Delay, NewRefire);
						A->Modify();
						A->Tags[TagIndex] = FName(*NewTag);
					}
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE
