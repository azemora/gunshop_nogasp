// NPCCustomerAIController.h
// ALTERED - Calls OnMoveToFailed on NPC when pathing fails

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "NPCCustomerAIController.generated.h"

UCLASS()
class ANPCCustomerAIController : public AAIController
{
	GENERATED_BODY()

public:
	ANPCCustomerAIController();

	UFUNCTION(BlueprintCallable, Category = "Shop|AI")
	void MoveToDestination(FVector Destination);

protected:
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

	UPROPERTY(EditDefaultsOnly, Category = "Shop|AI")
	float AcceptanceRadius = 80.f;
};