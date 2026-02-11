#pragma once

#include "CoreMinimal.h"

class FToolBarBuilder;
class FExtender;

/**
 * Registers SourceBridge toolbar button and menu in the UE editor.
 *
 * Adds a "SourceBridge" dropdown menu to the editor toolbar with:
 * - Export Scene to VMF
 * - Export & Compile (Full Pipeline)
 * - Validate Scene
 * - Export Test Box Room
 * - Export Settings...
 */
class FSourceBridgeToolbar
{
public:
	/** Register the toolbar extension. Call in StartupModule. */
	static void Register();

	/** Unregister the toolbar extension. Call in ShutdownModule. */
	static void Unregister();

private:
	/** Build the dropdown menu contents. */
	static void BuildMenu(class UToolMenu* Menu);

	/** Action handlers */
	static void OnExportScene();
	static void OnFullExport();
	static void OnValidate();
	static void OnExportTestBoxRoom();
	static void OnImportVMF();
	static void OnImportBSP();
	static void OnOpenSettings();
};
