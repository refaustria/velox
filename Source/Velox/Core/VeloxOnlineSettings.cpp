// Velox - online tunables (task M6-04).

#include "Core/VeloxOnlineSettings.h"

const UVeloxOnlineSettings& UVeloxOnlineSettings::Get()
{
	return *GetDefault<UVeloxOnlineSettings>();
}
