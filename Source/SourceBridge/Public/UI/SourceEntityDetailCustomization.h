#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Entities/FGDParser.h"

class IDetailLayoutBuilder;
class ASourceEntityActor;

/**
 * Custom detail panel for ASourceEntityActor and subclasses.
 * Shows Source entity properties in a clean layout with FGD validation.
 * When an FGD is loaded, dynamically generates property widgets
 * (dropdowns, checkboxes, text fields) from the FGD schema.
 */
class FSourceEntityDetailCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/** Register this customization with the PropertyEditor module. */
	static void Register();

	/** Unregister this customization. */
	static void Unregister();

	/** Get a keyvalue from the actor, falling back to FGD default. */
	static FString GetKeyValue(ASourceEntityActor* Actor, const FFGDProperty& Prop);

	/** Set a keyvalue on the actor and mark it dirty. */
	static void SetKeyValue(ASourceEntityActor* Actor, const FString& Key, const FString& Value);

private:
	/** Build property widgets for a resolved FGD entity class. */
	void BuildFGDPropertyWidgets(
		IDetailLayoutBuilder& DetailBuilder,
		ASourceEntityActor* Actor,
		const FFGDEntityClass& Resolved);

	/** Cached weak pointer to the actor being customized. */
	TWeakObjectPtr<ASourceEntityActor> CachedActor;
};
