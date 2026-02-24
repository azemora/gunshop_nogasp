// NPCRequestWidget.cpp
// NEW

#include "GUNSHOP_nogasp/NPC/NPCRequestWidget.h"
#include "Components/TextBlock.h"

void UNPCRequestWidget::SetRequestText(const FText& InText)
{
	if (RequestText)
	{
		RequestText->SetText(InText);
	}
}