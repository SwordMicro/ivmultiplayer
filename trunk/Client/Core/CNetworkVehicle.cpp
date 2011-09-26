//============== IV: Multiplayer - http://code.iv-multiplayer.com ==============
//
// File: CNetworkVehicle.cpp
// Project: Client.Core
// Author(s): jenksta
// License: See LICENSE in root directory
//
//==============================================================================
// TODO: Save driver/passengers in StreamOut and put them back in vehicle on StreamIn (if
// they are streamed in too (possibly force stream in if not?))

#include "CNetworkVehicle.h"
#include "CChatWindow.h"
#include "CPlayerManager.h"
#include "CModelManager.h"
#include "CLocalPlayer.h"
#include "Scripting.h"
#include <SharedUtility.h>
#include "CPools.h"

extern CChatWindow * g_pChatWindow;
extern CPlayerManager * g_pPlayerManager;
extern CModelManager * g_pModelManager;
extern CLocalPlayer * g_pLocalPlayer;

CNetworkVehicle::CNetworkVehicle(DWORD dwModelHash)
	: CStreamableEntity(STREAM_ENTITY_VEHICLE, 200.0f)
{
	m_pVehicle = NULL;
	m_vehicleId = INVALID_ENTITY_ID;
	m_pDriver = NULL;
	memset(m_pPassengers, 0, sizeof(m_pPassengers));
	m_pModelInfo = CGame::GetModelInfo(MODEL_ADMIRAL_INDEX);
	SetModel(dwModelHash);
	memset(&m_vecPosition, 0, sizeof(CVector3));
	memset(&m_vecRotation, 0, sizeof(CVector3));
	memset(&m_vecMoveSpeed, 0, sizeof(CVector3));
	memset(&m_vecTurnSpeed, 0, sizeof(CVector3));
	memset(m_byteColors, 0, sizeof(m_byteColors));
	m_bSirenState = false;
	memset(m_bIndicatorState, 0, sizeof(m_bIndicatorState));
	memset(m_bComponents, 0, sizeof(m_bComponents));
	m_interp.pos.ulFinishTime = 0;
	m_interp.rot.ulFinishTime = 0;
	m_dwDoorLockState = 0;
	m_ulHornDurationEnd = 0;
	m_uiHealth = 1000;
	m_fDirtLevel = 1000.0f;
	m_fPetrolTankHealth = 1000.0f;
	m_ucVariation = 0;
}

CNetworkVehicle::~CNetworkVehicle()
{
	// Remove the driver if we have one
	if(m_pDriver)
		m_pDriver->RemoveFromVehicle();

	// Remove all passengers if we have any
	for(BYTE i = 0; i < MAX_VEHICLE_PASSENGERS; i++)
	{
		if(m_pPassengers[i])
			m_pPassengers[i]->RemoveFromVehicle();
	}

	// Tell the streamer to delete us
	OnDelete();
}

void CNetworkVehicle::SetPassenger(BYTE bytePassengerId, CNetworkPlayer * pPassenger)
{
	if(bytePassengerId < MAX_VEHICLE_PASSENGERS)
		return;

	m_pPassengers[bytePassengerId] = pPassenger;
}

CNetworkPlayer * CNetworkVehicle::GetPassenger(BYTE bytePassengerId)
{
	if(bytePassengerId >= MAX_VEHICLE_PASSENGERS)
		return NULL;

	return m_pPassengers[bytePassengerId];
}

void CNetworkVehicle::SetOccupant(BYTE byteSeatId, CNetworkPlayer * pOccupant)
{
	if(byteSeatId == 0)
		SetDriver(pOccupant);
	else
		SetPassenger((byteSeatId - 1), pOccupant);
}

CNetworkPlayer * CNetworkVehicle::GetOccupant(BYTE byteSeatId)
{
	if(byteSeatId == 0)
		return GetDriver();

	return GetPassenger(byteSeatId - 1);
}

bool CNetworkVehicle::Create()
{
	// Are we not already created?
	if(!IsSpawned())
	{
		// Add our model info reference
		m_pModelInfo->AddReference(true);

#if 0
		// Get our model index
		int iModelIndex = m_pModelInfo->GetIndex();

		CLogFile::Printf("CNetworkVehicle::Create 3 - 3");

		CLogFile::Printf("pModelInfo + 0x70 = %d", *(DWORD *)(m_pModelInfo->GetModelInfo() + 0x70));
		//memset((void *)(CGame::GetBase() + 0x841808), 0x90, 5);
		//memset((void *)(CGame::GetBase() + 0x8419B8), 0x90, 5);

		// Create the vehicle
		DWORD dwFunc = (CGame::GetBase() + 0x8415D0);
		CVector3 * pVecPosition = &m_vecPosition;
		IVVehicle * pVehicle = NULL;
		_asm
		{
			push 0 ; last param of CREATE_CAR (try 1 if vehicle physics seem strange)
			push 1 // was 1 and above was 0
			push pVecPosition
			push iModelIndex
			call dwFunc
			add esp, 10h
			mov pVehicle, eax
		}

		CLogFile::Printf("CNetworkVehicle::Create 4");

		// Invalid vehicle?
		if(!pVehicle)
			return false;

		CLogFile::Printf("CNetworkVehicle::Create 4 - 4");

		// Create the vehicle instance
		m_pVehicle = new CIVVehicle(pVehicle);
#endif

		// Create the vehicle
		// TODO: Don't use natives for this
		unsigned int uiVehicleHandle;
		Scripting::CreateCar(m_pModelInfo->GetHash(), m_vecPosition.fX, m_vecPosition.fY, m_vecPosition.fZ, &uiVehicleHandle, true); // last param something with physics (Process Physics?)
		CLogFile::Printf("New vehicle handle is %d", uiVehicleHandle);

		// Create the vehicle instance
		m_pVehicle = new CIVVehicle(CGame::GetPools()->GetVehiclePool()->AtHandle(uiVehicleHandle));

		if(!m_pVehicle)
			return false;

		// Set initial colors
		SetColors(m_byteColors[0], m_byteColors[1], m_byteColors[2], m_byteColors[3]);

		// Add the vehicle to the world
		// Not needed as native does it for us
		//m_pVehicle->AddToWorld();

		// Set the initial health
		SetHealth(m_uiHealth);

		// Set the initial position
		SetPosition(m_vecPosition);

		// Set the initial rotation
		SetRotation(m_vecRotation);
		return true;
	}

	// Already created
	return false;
}

void CNetworkVehicle::Destroy()
{
	// Are we spawned?
	if(IsSpawned())
	{
		// Remove the driver if we have one
		if(m_pDriver)
			m_pDriver->InternalRemoveFromVehicle();

		// Remove all passengers if we have any
		for(BYTE i = 0; i < MAX_VEHICLE_PASSENGERS; i++)
		{
			if(m_pPassengers[i])
				m_pPassengers[i]->InternalRemoveFromVehicle();
		}

		// See below comment for more info
		unsigned int uiVehicleHandle = GetScriptingHandle();
		Scripting::DeleteCar(&uiVehicleHandle);
		// Can't use below as we created it with the native so it also
		// needs to be removed from mission cleanup list
#if 0
		// Get the vehicle pointer
		IVVehicle * pVehicle = m_pVehicle->GetVehicle();

		*(BYTE *)(pVehicle + 0xF6D) |= 8;

		// Remove the vehicle from the world
		m_pVehicle->RemoveFromWorld();

		// Remove references?
		DWORD dwFunc = (CGame::GetBase() + 0x819190);
		_asm
		{
			push pVehicle
			call dwFunc
			add esp, 4
		}

		*(BYTE *)(pVehicle + 0xF6B) &= 0xDF;

		// Delete the vehicle
		dwFunc = pVehicle->m_VFTable->ScalarDeletingDestructor;
		_asm
		{
			push 1
			mov ecx, pVehicle
			call dwFunc
		}
#endif

		// Remove our model info reference
		m_pModelInfo->RemoveReference();

		// Delete the vehicle instance
		SAFE_DELETE(m_pVehicle);
	}
}

void CNetworkVehicle::StreamIn()
{
	if(GetDriver() == g_pLocalPlayer)
		CLogFile::Printf("STREAM IN ON LOCAL PLAYERS VEHICLE!");

	// Attempt to create the vehicle
	if(Create())
	{
		// Set the rotation
		SetRotation(m_vecRotation);

		// Set the move speed
		SetMoveSpeed(m_vecMoveSpeed);

		// Set the turn speed
		SetTurnSpeed(m_vecTurnSpeed);
		
		// Set the colors
		SetColors(m_byteColors[0], m_byteColors[1], m_byteColors[2], m_byteColors[3]);

		// Disable some damage stuff
		Scripting::SetCarCanBeVisiblyDamaged(GetScriptingHandle(), false);
		Scripting::SetCarProofs(GetScriptingHandle(), true, true, true, true, true);

		// Restore the health
		SetHealth(m_uiHealth);

		// Restore the siren state
		SetSirenState(m_bSirenState);

		// Restore the petrol tank health
		SetPetrolTankHealth(m_fPetrolTankHealth);

		// Restore the dirt level
		SetDirtLevel(m_fDirtLevel);

		// Apply the lock state
		SetDoorLockState(m_dwDoorLockState);

		// Sound horn if needed
		if(m_ulHornDurationEnd > SharedUtility::GetTime())
			SoundHorn((m_ulHornDurationEnd - SharedUtility::GetTime()));

		// Set the extras
		for(int i = 0; i <= 8; ++ i)
			SetComponentState(i, m_bComponents[i]);

		// Restore the variation
		SetVariation(m_ucVariation);

		// Reset interpolation
		ResetInterpolation();

		// Add the driver if we have one
		if(m_pDriver)
			m_pDriver->InternalPutInVehicle(this, 0);

		// Add all passengers if we have any
		for(BYTE i = 0; i < MAX_VEHICLE_PASSENGERS; i++)
		{
			if(m_pPassengers[i])
				m_pPassengers[i]->InternalPutInVehicle(this, (i + 1));
		}
	}
}

void CNetworkVehicle::StreamOut()
{
	if(GetDriver() == g_pLocalPlayer)
		CLogFile::Printf("STREAM OUT ON LOCAL PLAYERS VEHICLE!");

	// Save the coordinates
	GetPosition(m_vecPosition);

	// Save the rotation
	GetRotation(m_vecRotation);

	// Save the move speed
	GetMoveSpeed(m_vecMoveSpeed);

	// Save the turn speed
	GetTurnSpeed(m_vecTurnSpeed);

	// Save the color
	GetColors(m_byteColors[0], m_byteColors[1], m_byteColors[2], m_byteColors[3]);

	// Save the health
	m_uiHealth = GetHealth();

	// Save the siren state
	m_bSirenState = GetSirenState();

	// Save the petrol tank health
	m_fPetrolTankHealth = GetPetrolTankHealth();

	// Save the dirt level
	m_fDirtLevel = GetDirtLevel();

	// Destroy the vehicle
	Destroy();
}

bool CNetworkVehicle::IsMoving()
{
	CVector3 vecMoveSpeed;
	GetMoveSpeed(vecMoveSpeed);

	if(!vecMoveSpeed.IsEmpty())
		return true;

	return false;
}

void CNetworkVehicle::StopMoving()
{
	SetMoveSpeed(CVector3());
}

void CNetworkVehicle::SoundHorn(int iDuration)
{
	if(IsSpawned())
		m_pVehicle->SoundHorn(iDuration);

	m_ulHornDurationEnd = (SharedUtility::GetTime() + iDuration);
}

unsigned int CNetworkVehicle::GetScriptingHandle()
{
	if(IsSpawned())
		return CGame::GetPools()->GetVehiclePool()->HandleOf(m_pVehicle->GetVehicle());

	return 0;
}

void CNetworkVehicle::SetModel(DWORD dwModelHash)
{
	// Get the model index from the model hash
	int iModelIndex = CGame::GetStreaming()->GetModelIndexFromHash(dwModelHash);

	// Do we have an invalid model index?
	if(iModelIndex == -1)
	{
		CLogFile::Printf("CClientVehicle::SetModel Failed (Invalid model hash)!");
		return;
	}

	// Has the model not changed?
	if(m_pModelInfo->GetIndex() == iModelIndex)
	{
		CLogFile::Printf("CClientVehicle::SetModel Failed (Model change not needed)!");
		return;
	}

	// Get the new model info
	CIVModelInfo * pNewModelInfo = CGame::GetModelInfo(iModelIndex);

	// Is the new model info valid?
	if(!pNewModelInfo || !pNewModelInfo->IsValid() || !pNewModelInfo->IsVehicle())
	{
		CLogFile::Printf("CClientVehicle::SetModel Failed (Invalid model)!");
		return;
	}

	// Are we spawned?
	bool bSpawned = IsSpawned();

	if(bSpawned)
	{
		// Stream ourselves out
		CLogFile::Printf("CClientVehicle::SetModel Stream Out Begin");
		StreamOut();
		CLogFile::Printf("CClientVehicle::SetModel Stream Out Complete");
	}

	// Set the new model info
	m_pModelInfo = pNewModelInfo;

	// Were we spawned?
	if(bSpawned)
	{
		// Stream ourselves back in
		CLogFile::Printf("CClientVehicle::SetModel Stream In Begin");
		StreamIn();
		CLogFile::Printf("CClientVehicle::SetModel Stream In Complete");
	}
}

void CNetworkVehicle::SetColors(BYTE byteColor1, BYTE byteColor2, BYTE byteColor3, BYTE byteColor4)
{
	if(IsSpawned())
		m_pVehicle->SetColors(byteColor1, byteColor2, byteColor3, byteColor4);

	m_byteColors[0] = byteColor1;
	m_byteColors[1] = byteColor2;
	m_byteColors[2] = byteColor3;
	m_byteColors[3] = byteColor4;
}

void CNetworkVehicle::GetColors(BYTE &byteColor1, BYTE &byteColor2, BYTE &byteColor3, BYTE &byteColor4)
{
	if(IsSpawned())
		m_pVehicle->GetColors(&byteColor1, &byteColor2, &byteColor3, &byteColor4);
	else
	{
		byteColor1 = m_byteColors[0];
		byteColor2 = m_byteColors[1];
		byteColor3 = m_byteColors[2];
		byteColor4 = m_byteColors[3];
	}
}

void CNetworkVehicle::SetPosition(const CVector3& vecPosition, bool bDontCancelTasks, bool bResetInterpolation)
{
	if(IsSpawned())
	{
		if(!bDontCancelTasks)
			Scripting::SetCarCoordinatesNoOffset(GetScriptingHandle(), vecPosition.fX, vecPosition.fY, vecPosition.fZ);
		else
		{
			// Remove the vehicle from the world
			m_pVehicle->RemoveFromWorld();

			// Set the position in the matrix
			m_pVehicle->SetPosition(vecPosition);

			// Re add the vehicle to the world to apply the matrix change
			m_pVehicle->AddToWorld();
		}
	}
	
	m_vecPosition = vecPosition;

	// Reset interpolation if requested
	if(bResetInterpolation)
		RemoveTargetPosition();
}

void CNetworkVehicle::GetPosition(CVector3& vecPosition)
{
	if(IsSpawned())
		m_pVehicle->GetPosition(vecPosition);
	else
		vecPosition = m_vecPosition;
}

void CNetworkVehicle::SetRotation(const CVector3& vecRotation, bool bResetInterpolation)
{
	if(IsSpawned())
	{
		// Remove the vehicle from the world
		m_pVehicle->RemoveFromWorld();

		// Get the vehicle matrix
		Matrix matMatrix;
		m_pVehicle->GetMatrix(matMatrix);

		// Convert the rotation from degrees to radians
		CVector3 vecNewRotation = Math::ConvertDegreesToRadians(vecRotation);

		// Apply the rotation to the vehicle matrix
		CGame::ConvertEulerAnglesToRotationMatrix(vecNewRotation, matMatrix);

		// Set the new vehicle matrix
		m_pVehicle->SetMatrix(matMatrix);

		// Re add the vehicle to the world to apply the matrix change
		m_pVehicle->AddToWorld();
	}
	
	m_vecRotation = vecRotation;

	// Reset interpolation if requested
	if(bResetInterpolation)
		RemoveTargetRotation();
}

void CNetworkVehicle::GetRotation(CVector3& vecRotation)
{
	if(IsSpawned())
	{
		// Get the vehicle matrix
		Matrix matMatrix;
		m_pVehicle->GetMatrix(matMatrix);

		// Convert the matrix to euler angles
		CVector3 vecNewRotation;
		CGame::ConvertRotationMatrixToEulerAngles(matMatrix, vecNewRotation);

#pragma message("CGame::ConvertRotationMatrixToEulerAngles should flip the rotation itself")
		// Flip the rotation
		vecNewRotation.fX = ((2 * PI) - vecNewRotation.fX);
		vecNewRotation.fY = ((2 * PI) - vecNewRotation.fY);
		vecNewRotation.fZ = ((2 * PI) - vecNewRotation.fZ);

		// Convert the rotation from radians to degrees
		vecRotation = Math::ConvertRadiansToDegrees(vecNewRotation);
	}
	else
		vecRotation = m_vecRotation;
}

void CNetworkVehicle::SetHealth(unsigned int uiHealth)
{
	// Are we spawned?
	if(IsSpawned())
		m_pVehicle->SetEngineHealth((float)uiHealth);

	m_uiHealth = uiHealth;
}

unsigned int CNetworkVehicle::GetHealth()
{
	// Are we spawned?
	if(IsSpawned())
		return (unsigned int)m_pVehicle->GetEngineHealth();

	return m_uiHealth;
}

void CNetworkVehicle::SetMoveSpeed(const CVector3& vecMoveSpeed)
{
	// Are we spawned?
	if(IsSpawned())
		m_pVehicle->SetMoveSpeed(vecMoveSpeed);

	m_vecMoveSpeed = vecMoveSpeed;
}

void CNetworkVehicle::GetMoveSpeed(CVector3& vecMoveSpeed)
{
	// Are we spawned?
	if(IsSpawned())
		m_pVehicle->GetMoveSpeed(vecMoveSpeed);
	else
		vecMoveSpeed = m_vecMoveSpeed;
}

void CNetworkVehicle::SetTurnSpeed(const CVector3& vecTurnSpeed)
{
	// Are we spawned?
	if(IsSpawned())
		m_pVehicle->SetTurnSpeed(vecTurnSpeed);

	m_vecTurnSpeed = vecTurnSpeed;
}

void CNetworkVehicle::GetTurnSpeed(CVector3& vecTurnSpeed)
{
	// Are we spawned?
	if(IsSpawned())
		m_pVehicle->GetTurnSpeed(vecTurnSpeed);
	else
		vecTurnSpeed = m_vecTurnSpeed;
}

void CNetworkVehicle::SetSirenState(bool bSirenState)
{
	// Are we spawned?
	if(IsSpawned())
		m_pVehicle->SetSirenState(bSirenState);

	m_bSirenState = bSirenState;
}

bool CNetworkVehicle::GetSirenState()
{
	// Are we spawned?
	if(IsSpawned())
		return m_pVehicle->GetSirenState();

	return m_bSirenState;
}

void CNetworkVehicle::SetDirtLevel(float fDirtLevel)
{
	// Are we spawned?
	if(IsSpawned())
		m_pVehicle->SetDirtLevel(fDirtLevel);

	m_fDirtLevel = fDirtLevel;
}

float CNetworkVehicle::GetDirtLevel()
{
	// Are we spawned?
	if(IsSpawned())
		return m_pVehicle->GetDirtLevel();

	return m_fDirtLevel;
}

void CNetworkVehicle::SetPetrolTankHealth(float fPetrolTankHealth)
{
	// Are we spawned?
	if(IsSpawned())
		m_pVehicle->SetPetrolTankHealth(fPetrolTankHealth);

	m_fPetrolTankHealth = fPetrolTankHealth;
}

float CNetworkVehicle::GetPetrolTankHealth()
{
	// Are we spawned?
	if(IsSpawned())
		return m_pVehicle->GetPetrolTankHealth();

	return m_fPetrolTankHealth;
}

void CNetworkVehicle::StoreEmptySync(EMPTYVEHICLESYNCPACKET * emptyVehicleSync)
{
	// TODO: Store player id
	SetPosition(emptyVehicleSync->vecPos);
	//SetHeading(emptyVehicleSync->fHeading);
	//SetQuaternion(&emptyVehicleSync->quatQuaternion);
	SetTurnSpeed(emptyVehicleSync->vecTurnSpeed);
	SetMoveSpeed(emptyVehicleSync->vecMoveSpeed);
}

BYTE CNetworkVehicle::GetMaxPassengers()
{
	// Are we spawned?
	if(IsSpawned())
		return m_pVehicle->GetMaxPasssengers();

	return 0;
}

void CNetworkVehicle::AddToWorld()
{
	// Are we spawned?
	if(IsSpawned())
	{
		// Add the vehicle to the world
		m_pVehicle->AddToWorld();
	}
}

void CNetworkVehicle::RemoveFromWorld(bool bStopMoving)
{
	// Are we spawned?
	if(IsSpawned())
	{
		// Stop the vehicle from moving to avoid some weird bugs
		if(bStopMoving)
			StopMoving();

		// Remove the vehicle from the world
		m_pVehicle->RemoveFromWorld();
	}
}

void CNetworkVehicle::SetDoorLockState(DWORD dwDoorLockState)
{
	// Get the actual lock state
	DWORD dwState = 0;

	switch(dwDoorLockState)
	{
		case 0:
			dwState = 0;
			break;
		case 1:
			dwState = 3;
			break;
		case 2:
			dwState = 7;
			break;
		default:
			return;
	}

	// Are we spawned?
	if(IsSpawned())
		m_pVehicle->SetDoorLockState(dwState);

	m_dwDoorLockState = dwState;
}

DWORD CNetworkVehicle::GetDoorLockState()
{
	DWORD dwState = m_dwDoorLockState;

	// Are we spawned?
	if(IsSpawned())
		dwState = m_pVehicle->GetDoorLockState();
		
	switch(dwState)
	{
		case 0:
			return 0;
		case 3:
			return 1;
		case 7:
			return 2;
		default:
			CLogFile::Printf("Unknown vehicle %d door state %d", m_vehicleId, m_pVehicle->GetDoorLockState()); 
	}

	return 0;
}

void CNetworkVehicle::Pulse()
{
	Interpolate();
}

void CNetworkVehicle::UpdateTargetPosition()
{
	// Do we have a target position?
	if(HasTargetPosition())
	{
		// Get our position
		CVector3 vecCurrentPosition;
		GetPosition(vecCurrentPosition);

		// Get the factor of time spent from the interpolation start
		// to the current time.
		unsigned long ulCurrentTime = SharedUtility::GetTime();
		float fAlpha = Math::Unlerp(m_interp.pos.ulStartTime, ulCurrentTime, m_interp.pos.ulFinishTime);

		// Don't let it overcompensate the error too much
		fAlpha = Math::Clamp(0.0f, fAlpha, 1.5f);

		// Get the current error portion to compensate
		float fCurrentAlpha = (fAlpha - m_interp.pos.fLastAlpha);
		m_interp.pos.fLastAlpha = fAlpha;

		// Apply the error compensation
		CVector3 vecCompensation = Math::Lerp(CVector3(), fCurrentAlpha, m_interp.pos.vecError);

		// If we finished compensating the error, finish it for the next pulse
		if(fAlpha == 1.5f)
			m_interp.pos.ulFinishTime = 0;

		// Calculate the new position
		CVector3 vecNewPosition = (vecCurrentPosition + vecCompensation);

		// Check if the distance to interpolate is too far
		if((vecCurrentPosition - m_interp.pos.vecTarget).Length() > 20)
		{
			// Abort position interpolation
			m_interp.pos.ulFinishTime = 0;
			vecNewPosition = m_interp.pos.vecTarget;

			// Abort target interpolation
			if(HasTargetRotation())
			{
				SetRotation(m_interp.rot.vecTarget);
				m_interp.rot.ulFinishTime = 0;
			}
		}

		// Set our new position
		SetPosition(vecNewPosition, true, false);
	}
}

void CNetworkVehicle::UpdateTargetRotation()
{
	// Do we have a target rotation?
	if(HasTargetRotation())
	{
		// Get our current rotation
		CVector3 vecCurrentRotation;
		GetRotation(vecCurrentRotation);

		// Get the factor of time spent from the interpolation start
		// to the current time.
		unsigned long ulCurrentTime = SharedUtility::GetTime();
		float fAlpha = Math::Unlerp(m_interp.rot.ulStartTime, ulCurrentTime, m_interp.rot.ulFinishTime);

		// Don't let it to overcompensate the error
		fAlpha = Math::Clamp(0.0f, fAlpha, 1.0f);

		// Get the current error portion to compensate
		float fCurrentAlpha = (fAlpha - m_interp.rot.fLastAlpha);
		m_interp.rot.fLastAlpha = fAlpha;

		// Compensate the error
		CVector3 vecCompensation = Math::Lerp(CVector3(), fCurrentAlpha, m_interp.rot.vecError);

		// If we finished compensating the error, finish it for the next pulse
		if(fAlpha == 1.0f)
			m_interp.rot.ulFinishTime = 0;

		// Set the new rotation
		SetRotation((vecCurrentRotation + vecCompensation), false);
	}
}

void CNetworkVehicle::Interpolate()
{
	// Do we have a driver?
	if(GetDriver())
	{
		// Update our target position
		UpdateTargetPosition();

		// Update our target rotation
		UpdateTargetRotation();
	}
	else
	{
		// Remove our target position
		RemoveTargetPosition();

		// Remove our target rotation
		RemoveTargetRotation();
	}
}

void CNetworkVehicle::SetTargetPosition(const CVector3& vecPosition, unsigned long ulDelay)
{
	// Are we spawned?
	if(IsSpawned())
	{
		// Update our target position
		UpdateTargetPosition();

		// Get the current time
		unsigned long ulTime = SharedUtility::GetTime();

		// Get our local position
		CVector3 vecLocalPosition;
		GetPosition(vecLocalPosition);

		// Set the target position
		m_interp.pos.vecTarget = vecPosition;

		// Calculate the relative error
		m_interp.pos.vecError = (vecPosition - vecLocalPosition);

		// Apply the error over 400ms (i.e. 1/4 per 100ms)
		m_interp.pos.vecError *= Math::Lerp<const float>(0.25f, Math::UnlerpClamped(100, ulDelay, 400), 1.0f);

		// Get the interpolation interval
		m_interp.pos.ulStartTime = ulTime;
		m_interp.pos.ulFinishTime = (ulTime + ulDelay);

		// Initialize the interpolation
		m_interp.pos.fLastAlpha = 0.0f;
	}

	// Set our position straight
	m_vecPosition = vecPosition;
}

void CNetworkVehicle::SetTargetRotation(const CVector3& vecRotation, unsigned long ulDelay)
{
	// Are we spawned?
	if(IsSpawned())
	{
		// Update our target rotation
		UpdateTargetRotation();

		// Get the current time
		unsigned long ulTime = SharedUtility::GetTime();

		// Get our local rotation
		CVector3 vecLocalRotation;
		GetRotation(vecLocalRotation);

		// Set the target rotation
		m_interp.rot.vecTarget = vecRotation;

		// Get the error
		m_interp.rot.vecError = Math::GetOffsetDegrees(vecLocalRotation, vecRotation);

		// Get the interpolation interval
		m_interp.rot.ulStartTime = ulTime;
		m_interp.rot.ulFinishTime = (ulTime + ulDelay);

		// Initialize the interpolation
		m_interp.rot.fLastAlpha = 0.0f;
	}

	// Set our rotation straight
	m_vecRotation = vecRotation;
}

void CNetworkVehicle::RemoveTargetPosition()
{
	m_interp.pos.ulFinishTime = 0;
}

void CNetworkVehicle::RemoveTargetRotation()
{
	m_interp.rot.ulFinishTime = 0;
}

void CNetworkVehicle::ResetInterpolation()
{
	if(HasTargetPosition())
		SetPosition(m_interp.pos.vecTarget, true);

	if(HasTargetRotation())
		SetRotation(m_interp.rot.vecTarget);

	RemoveTargetPosition();
	RemoveTargetRotation();
}

void CNetworkVehicle::SetInterior(unsigned int uiInterior)
{
	/*
	// TODO: Fix this (disables physics for cars when you enter & leave an interior - you can't drive nor close the door/push them) 
	if(m_bSpawned && uiInterior != GetInterior() && g_pLocalPlayer->GetAt()->GetVehicleId() != m_vehicleId)
		Scripting::SetRoomForCarByKey(GetScriptingHandle(), (Scripting::eInteriorRoomKey)uiInterior);
	*/
}

unsigned int CNetworkVehicle::GetInterior()
{
	// Are we spawned?
	if(IsSpawned())
	{
		unsigned int uiInterior;
		Scripting::GetKeyForCarInRoom(GetScriptingHandle(), (Scripting::eInteriorRoomKey *)&uiInterior);
		return uiInterior;
	}

	return 0;
}

void CNetworkVehicle::SetIndicatorState(bool bFrontLeft, bool bFrontRight, bool bBackLeft, bool bBackRight)
{
	m_bIndicatorState[0] = bFrontLeft;
	m_bIndicatorState[1] = bFrontRight;
	m_bIndicatorState[2] = bBackLeft;
	m_bIndicatorState[3] = bBackRight;
}

bool CNetworkVehicle::GetIndicatorState(unsigned char ucSlot)
{
	if(ucSlot >= 0 && ucSlot <= 3)
	{
		return m_bIndicatorState[ucSlot];
	}

	return false;
}

void CNetworkVehicle::SetComponentState(unsigned char ucSlot, bool bOn)
{
	if(ucSlot >= 0 && ucSlot <= 8)
	{
		m_bComponents[ucSlot] = bOn;

		// Are we spawned?
		if(IsSpawned())
			m_pVehicle->SetComponentState((ucSlot + 1), bOn);
	}
}

bool CNetworkVehicle::GetComponentState(unsigned char ucSlot)
{
	if(ucSlot >= 0 && ucSlot <= 8)
		return m_bComponents[ucSlot];

	return false;
}

void CNetworkVehicle::SetVariation(unsigned char ucVariation)
{
	// Are we spawned?
	if(IsSpawned())
	{
		if(m_pVehicle->GetTextureVariationCount() != 255)
		{
			//m_pVehicle->SetTextureVariation(ucVariation);
			Scripting::SetCarLivery(GetScriptingHandle(), (int)ucVariation);
			m_ucVariation = ucVariation;
		}
		else
			m_ucVariation = 0;
	}
	else
		m_ucVariation = ucVariation;
}

unsigned char CNetworkVehicle::GetVariation()
{
	// Are we spawned?
	if(IsSpawned())
	{
		if(m_pVehicle->GetTextureVariationCount() != 255)
		{
			//return (unsigned char)m_pVehicle->GetTextureVariation();
			int iVariation = 0;
			Scripting::GetCarLivery(GetScriptingHandle(), &iVariation);
			return iVariation;
		}
		else
			return 0;
	}
	else
		return m_ucVariation;
}

bool CNetworkVehicle::IsOnScreen()
{
	// Are we spawned?
	if(IsSpawned())
		return /*Scripting::IsCarOnScreen(GetScriptingHandle())*/true;

	return false;
}