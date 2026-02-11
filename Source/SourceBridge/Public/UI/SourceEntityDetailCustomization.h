#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

/**
 * Custom detail panel for ASourceEntityActor and subclasses.
 * Shows Source entity properties in a clean layout with FGD validation.
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
};
