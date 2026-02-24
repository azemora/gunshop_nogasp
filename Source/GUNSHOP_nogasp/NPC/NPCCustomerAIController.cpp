// NPCCustomerAIController.cpp
// ALTERED - StopMovement before new MoveTo, ignore aborted (Code=3)

#include "GUNSHOP_nogasp/NPC/NPCCustomerAIController.h"
#include "GUNSHOP_nogasp/NPC/NPCCustomer.h"
#include "Navigation/PathFollowingComponent.h"

ANPCCustomerAIController::ANPCCustomerAIController()
{
}

void ANPCCustomerAIController::MoveToDestination(FVector Destination)
{
	// Stop any current move to prevent abort callbacks
	StopMovement();

	FAIMoveRequest MoveReq;
	MoveReq.SetGoalLocation(Destination);
	MoveReq.SetAcceptanceRadius(AcceptanceRadius);
	MoveReq.SetUsePathfinding(true);

	MoveTo(MoveReq);
}

void ANPCCustomerAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	ANPCCustomer* Customer = Cast<ANPCCustomer>(GetPawn());
	if (!Customer) return;

	if (Result.IsSuccess())
	{
		Customer->OnReachedDestination();
	}
	else if (Result.Code == EPathFollowingResult::Aborted)
	{
		// Aborted by a new MoveTo call - not an error, ignore
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AI %s: Move FAILED (Code=%d)"),
			*Customer->GetName(), static_cast<int32>(Result.Code));
		Customer->OnMoveToFailed();
	}
}