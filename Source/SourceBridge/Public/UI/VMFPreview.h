#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * VMF preview widget.
 * Generates and displays the VMF text output for the current scene
 * without saving to disk. Useful for inspecting the export before committing.
 *
 * Shows:
 * - Full VMF text output with syntax highlighting indicators
 * - Scene statistics (brush count, entity count, file size)
 * - Compile time estimate
 */
class SVMFPreview : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVMFPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** Generate VMF and update the preview. */
	FReply OnRefreshPreview();

	/** Copy VMF text to clipboard. */
	FReply OnCopyToClipboard();

	TSharedPtr<class SMultiLineEditableTextBox> VMFTextBox;
	TSharedPtr<class STextBlock> StatsText;
	FString CachedVMFContent;
};

/**
 * Registers the VMF Preview as a nomad tab in the editor.
 */
class FVMFPreviewTab
{
public:
	static void Register();
	static void Unregister();

private:
	static TSharedRef<class SDockTab> SpawnTab(const class FSpawnTabArgs& Args);
	static const FName TabId;
};
