// Velox - server RPC gate helpers (ADR 0007 section 5.1, M6 task 07). Implemented in VeloxNetCheats.cpp together with the
// non-shipping cvars and probes (velox.net.AllowTestRpcs, velox.move.DebugTimeScale, velox.net.ConnectProbe,
// velox.net.RpcGateProbe).

#pragma once

#include "CoreMinimal.h"

class UObject;

namespace VeloxNet
{
	/**
	 * Server: the project's test RPCs (scripted-input teleport/knockback/haste/kill, AddBot/KickBot) are accepted. False in
	 * Shipping; otherwise the cvar velox.net.AllowTestRpcs (default 0; infra/ci/nettest.sh sets it on its servers, the public
	 * server never does).
	 */
	VELOX_API bool TestRpcsAllowed();

	/**
	 * For a test RPC's _Validate: true when TestRpcsAllowed(); otherwise logs `VeloxRpcRefused` (through the owning
	 * AVeloxPlayerController's counter when Context resolves to one: an actor, a component, or their owner chain) and
	 * returns false, so the engine disconnects the sender (RPC_ValidateFailed).
	 */
	VELOX_API bool ValidateTestRpc(const UObject* Context, const TCHAR* RpcName);
}
