// NPCRequestWidget.h
// NEW - Base class for the NPC request bubble widget

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NPCRequestWidget.generated.h"

class UTextBlock;

UCLASS()
class UNPCRequestWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Set the text displayed in the request bubble */
	UFUNCTION(BlueprintCallable, Category = "Shop|NPC")
	void SetRequestText(const FText& InText);

protected:
	/** Bind this to a TextBlock named "RequestText" in the UMG designer */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RequestText;
};