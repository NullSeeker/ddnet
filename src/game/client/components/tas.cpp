/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "tas.h"

#include "controls.h"

#include <base/system.h>
#include <base/vmath.h>

#include <algorithm>
#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>

namespace
{
constexpr int TAS_INPUT_DUMMY = 0;
} // namespace

CTAS::CTAS() :
	m_Active(false),
	m_Status(EStatus::IDLE),
	m_StatusBeforePause(EStatus::IDLE),
	m_Tick(0),
	m_PlayIndex(0),
	m_LastTickTime(0),
	m_TickRemainder(0.0),
	m_SpawnPos(0.0f, 0.0f),
	m_pCharacter(nullptr)
{
}

void CTAS::OnInit()
{
	ResetWorld();
}

void CTAS::OnUpdate()
{
	if(!m_Active)
		return;

	UpdateAutoTicks();
	ApplyInputFreeze();
}

void CTAS::OnConsoleInit()
{
	Console()->Register("tas_enter", "", CFGFLAG_CLIENT, ConTasEnter, this, "Enter TAS sandbox mode");
	Console()->Register("tas_record", "", CFGFLAG_CLIENT, ConTasRecord, this, "Start TAS recording");
	Console()->Register("tas_stop", "", CFGFLAG_CLIENT, ConTasStop, this, "Stop TAS recording or playback");
	Console()->Register("tas_play", "", CFGFLAG_CLIENT, ConTasPlay, this, "Play TAS recording");
	Console()->Register("tas_pause", "", CFGFLAG_CLIENT, ConTasPause, this, "Pause or resume TAS playback");
	Console()->Register("tas_clear", "", CFGFLAG_CLIENT, ConTasClear, this, "Clear TAS recording");
	Console()->Register("tas_forward", "", CFGFLAG_CLIENT, ConTasForward, this, "Advance TAS sandbox by one tick");
	Console()->Register("tas_rewind", "", CFGFLAG_CLIENT, ConTasRewind, this, "Rewind TAS sandbox by one tick");
}

void CTAS::Enter()
{
	if(m_Active)
		return;

	m_Active = true;
	m_Status = EStatus::IDLE;
	m_StatusBeforePause = EStatus::IDLE;
	m_LastTickTime = time_get();
	m_TickRemainder = 0.0;

	ResetWorld();
}

void CTAS::Record()
{
	if(!m_Active)
		Enter();

	m_Status = EStatus::RECORDING;
	m_StatusBeforePause = m_Status;
	m_PlayIndex = 0;
	m_vRecording.clear();
	m_vHistory.clear();
	m_Tick = 0;
	ResetWorld();
}

void CTAS::Stop()
{
	if(!m_Active)
		return;

	m_Status = EStatus::IDLE;
	m_StatusBeforePause = m_Status;
	m_PlayIndex = 0;
}

void CTAS::Play()
{
	if(!m_Active)
		Enter();

	if(m_vRecording.empty())
		return;

	m_Status = EStatus::PLAYING;
	m_StatusBeforePause = m_Status;
	m_PlayIndex = 0;
	m_vHistory.clear();
	m_Tick = 0;
	ResetWorld();
}

void CTAS::Pause()
{
	if(!m_Active)
		return;

	if(m_Status == EStatus::PAUSED)
	{
		m_Status = m_StatusBeforePause;
	}
	else
	{
		m_StatusBeforePause = m_Status;
		m_Status = EStatus::PAUSED;
	}
}

void CTAS::Clear()
{
	m_vRecording.clear();
	m_vHistory.clear();
	m_PlayIndex = 0;
	m_Tick = 0;
	if(m_Active)
	{
		m_Status = EStatus::IDLE;
		ResetWorld();
	}
}

void CTAS::StepForward()
{
	if(!m_Active)
		Enter();

	if(m_Status == EStatus::PLAYING)
	{
		if(m_PlayIndex < static_cast<int>(m_vRecording.size()))
		{
			const CNetObj_PlayerInput Input = m_vRecording[m_PlayIndex].m_Input;
			TickOnce(Input, false, true);
			m_PlayIndex++;
		}
	}
	else if(m_Status == EStatus::RECORDING)
	{
		const CNetObj_PlayerInput Input = GetLiveInput();
		TickOnce(Input, true, true);
	}
	else
	{
		const CNetObj_PlayerInput Input = GetLiveInput();
		TickOnce(Input, false, true);
	}
}

void CTAS::StepRewind()
{
	if(!m_Active || m_Tick <= 0)
		return;

	const int TargetTick = m_Tick - 1;
	SimulateToTick(TargetTick);
}

const char *CTAS::StatusName() const
{
	return StatusName(m_Status);
}

const char *CTAS::ModeName() const
{
	return g_Config.m_ClTasMode == 0 ? "Default" : "Binds";
}

const char *CTAS::StatusName(EStatus Status) const
{
	switch(Status)
	{
	case EStatus::IDLE:
		return "Idle";
	case EStatus::RECORDING:
		return "Recording";
	case EStatus::PLAYING:
		return "Playing";
	case EStatus::PAUSED:
		return "Paused";
	}
	return "Idle";
}

void CTAS::EnsureWorld()
{
	if(m_pCharacter != nullptr)
		return;

	ResetWorld();
}

void CTAS::ResetWorld()
{
	m_World.Clear();
	m_World.Init(GameClient()->Collision(), m_aTuningList, GameClient()->MapBugs());
	m_World.m_GameTick = 0;
	m_World.m_WorldConfig.m_IsDDRace = true;
	m_World.m_WorldConfig.m_IsVanilla = false;
	m_World.m_WorldConfig.m_IsFNG = false;
	m_World.m_WorldConfig.m_PredictDDRace = true;
	m_World.m_WorldConfig.m_PredictTiles = true;
	m_World.m_WorldConfig.m_PredictFreeze = 1;
	m_World.m_WorldConfig.m_PredictWeapons = true;
	m_World.m_WorldConfig.m_UseTuneZones = true;
	m_World.m_WorldConfig.m_BugDDRaceInput = false;
	m_World.m_WorldConfig.m_NoWeakHookAndBounce = false;
	m_World.m_WorldConfig.m_InfiniteAmmo = false;
	m_World.m_WorldConfig.m_IsSolo = false;

	for(int i = 0; i < TuneZone::NUM; ++i)
		m_aTuningList[i] = GameClient()->GetTuning(i) ? *GameClient()->GetTuning(i) : CTuningParams();

	m_SpawnPos = GameClient()->m_LocalCharacterPos;
	if(length(m_SpawnPos) < 0.1f)
		m_SpawnPos = vec2(0.0f, 0.0f);

	CNetObj_Character CharObj = {};
	CharObj.m_X = round_to_int(m_SpawnPos.x);
	CharObj.m_Y = round_to_int(m_SpawnPos.y);
	CharObj.m_Weapon = WEAPON_HAMMER;
	CharObj.m_Emote = EMOTE_NORMAL;
	CharObj.m_Jumped = 0;
	CharObj.m_AttackTick = 0;

	CNetObj_DDNetCharacter ExtendedObj = {};
	ExtendedObj.m_Flags = 0;
	ExtendedObj.m_TeleCheckpoint = 0;
	ExtendedObj.m_StrongWeakId = 0;
	ExtendedObj.m_TuneZoneOverride = TuneZone::OVERRIDE_NONE;
	ExtendedObj.m_FreezeEnd = 0;
	ExtendedObj.m_TargetX = 0;
	ExtendedObj.m_TargetY = -1;

	m_pCharacter = new CCharacter(&m_World, TAS_INPUT_DUMMY, &CharObj, &ExtendedObj);
	m_pCharacter->m_IsLocal = true;
	m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);
	m_World.InsertEntity(m_pCharacter);
	m_World.m_LocalClientId = TAS_INPUT_DUMMY;
}

void CTAS::TickOnce(const CNetObj_PlayerInput &Input, bool RecordInput, bool StoreHistory)
{
	EnsureWorld();
	m_Tick++;
	m_World.m_GameTick = m_Tick;

	m_pCharacter->OnDirectInput(&Input);
	m_pCharacter->OnPredictedInput(&Input);
	m_World.Tick();

	if(StoreHistory)
	{
		STASTick Snapshot = {};
		Snapshot.m_Tick = m_Tick;
		Snapshot.m_Input = Input;
		Snapshot.m_Core = m_pCharacter->GetCore();
		if(static_cast<int>(m_vHistory.size()) < m_Tick)
			m_vHistory.resize(m_Tick);
		m_vHistory[m_Tick - 1] = Snapshot;
	}

	if(RecordInput)
	{
		STASTick Entry = {};
		Entry.m_Tick = m_Tick;
		Entry.m_Input = Input;
		Entry.m_Core = m_pCharacter->GetCore();
		m_vRecording.push_back(Entry);
	}
}

void CTAS::UpdateAutoTicks()
{
	if(!ShouldAutoTick())
		return;

	const int TasTps = std::clamp(g_Config.m_ClTasTps, 1, 1000);
	const double TickInterval = 1.0 / static_cast<double>(TasTps);
	const int64_t Now = time_get();
	const double DeltaSeconds = static_cast<double>(Now - m_LastTickTime) / static_cast<double>(time_freq());
	m_LastTickTime = Now;

	double StepsExact = (DeltaSeconds + m_TickRemainder) / TickInterval;
	int Steps = static_cast<int>(StepsExact);
	m_TickRemainder = (DeltaSeconds + m_TickRemainder) - Steps * TickInterval;

	Steps = std::clamp(Steps, 0, 200);
	for(int i = 0; i < Steps; i++)
	{
		if(m_Status == EStatus::PLAYING)
		{
			if(m_PlayIndex >= static_cast<int>(m_vRecording.size()))
			{
				m_Status = EStatus::IDLE;
				break;
			}
			const CNetObj_PlayerInput Input = m_vRecording[m_PlayIndex].m_Input;
			TickOnce(Input, false, true);
			m_PlayIndex++;
		}
		else if(m_Status == EStatus::RECORDING)
		{
			const CNetObj_PlayerInput Input = GetLiveInput();
			TickOnce(Input, true, true);
		}
		else
		{
			const CNetObj_PlayerInput Input = GetLiveInput();
			TickOnce(Input, false, true);
		}
	}
}

void CTAS::ApplyInputFreeze() const
{
	if(!m_Active)
		return;

	if(g_Config.m_ClTasFreezeInput)
	{
		CControls &Controls = GameClient()->m_Controls;
		Controls.ResetInput(TAS_INPUT_DUMMY);
		Controls.m_aInputData[TAS_INPUT_DUMMY].m_TargetX = 0;
		Controls.m_aInputData[TAS_INPUT_DUMMY].m_TargetY = -1;
	}
}

CNetObj_PlayerInput CTAS::GetLiveInput() const
{
	CNetObj_PlayerInput Input = {};
	Input = GameClient()->m_Controls.m_aInputData[TAS_INPUT_DUMMY];
	return Input;
}

void CTAS::SimulateToTick(int TargetTick)
{
	if(TargetTick < 0)
		TargetTick = 0;

	ResetWorld();
	m_Tick = 0;
	m_PlayIndex = 0;

	if(TargetTick == 0)
		return;

	if(m_Status == EStatus::PLAYING || m_Status == EStatus::PAUSED)
	{
		const int Limit = std::min(TargetTick, static_cast<int>(m_vRecording.size()));
		for(int i = 0; i < Limit; ++i)
		{
			const CNetObj_PlayerInput Input = m_vRecording[i].m_Input;
			TickOnce(Input, false, true);
			m_PlayIndex++;
		}
	}
	else
	{
		const int Limit = std::min(TargetTick, static_cast<int>(m_vHistory.size()));
		for(int i = 0; i < Limit; ++i)
		{
			const CNetObj_PlayerInput Input = m_vHistory[i].m_Input;
			TickOnce(Input, false, true);
		}
	}
}

bool CTAS::ShouldAutoTick() const
{
	if(!m_Active)
		return false;

	if(m_Status == EStatus::PAUSED)
		return false;

	if(g_Config.m_ClTasMode != 0)
		return false;

	return true;
}

void CTAS::ConTasEnter(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CTAS *>(pUserData)->Enter();
}

void CTAS::ConTasRecord(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CTAS *>(pUserData)->Record();
}

void CTAS::ConTasStop(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CTAS *>(pUserData)->Stop();
}

void CTAS::ConTasPlay(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CTAS *>(pUserData)->Play();
}

void CTAS::ConTasPause(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CTAS *>(pUserData)->Pause();
}

void CTAS::ConTasClear(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CTAS *>(pUserData)->Clear();
}

void CTAS::ConTasForward(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CTAS *>(pUserData)->StepForward();
}

void CTAS::ConTasRewind(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CTAS *>(pUserData)->StepRewind();
}
