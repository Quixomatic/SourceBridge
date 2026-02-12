#include "UI/SSourceIOGraphNode.h"
#include "UI/SourceIOGraph.h"
#include "SGraphPin.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

void SSourceIOGraphNode::Construct(const FArguments& InArgs, USourceIOGraphNode* InNode)
{
	IONode = InNode;
	GraphNode = InNode;
	UpdateGraphNode();
	SetCursor(EMouseCursor::CardinalCross);
}

void SSourceIOGraphNode::UpdateGraphNode()
{
	// Use the default graph node rendering which reads GetNodeTitle() and GetNodeTitleColor()
	SGraphNode::UpdateGraphNode();
}

void SSourceIOGraphNode::CreatePinWidgets()
{
	SGraphNode::CreatePinWidgets();
}
