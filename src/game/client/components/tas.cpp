/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "tas.h"

#include "controls.h"

#include <base/system.h>
#include <base/vmath.h>

#include <algorithm>
#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>

namespace
{
constexpr int TAS_INPUT_DUMMY = 0;
constexpr int TAS_FILE_VERSION = 1;
constexpr char TAS_MAGIC[] = {'T', 'A', 'S', '1'};

struct STasFileHeader
{
	char m_aMagic[sizeof(TAS_MAGIC)];
	int m_Version;
	int m_NumTicks;
	float m_SpawnX;
	float m_SpawnY;
};

bool NormalizeTasFilename(const char *pInput, char *pOutput, size_t OutputSize)
{
	if(!pInput || pInput[0] == '\0')
		return false;

	str_copy(pOutput, pInput, OutputSize);
	if(!str_endswith(pOutput, ".tas"))
		str_append(pOutput, ".tas", OutputSize);
	return true;
}

constexpr const char *TAS_DIR = "data/tas";

void BuildTasPath(const char *pFilename, char *pBuffer, size_t BufferSize)
{
	str_format(pBuffer, BufferSize, "%s/%s", TAS_DIR, pFilename);
}
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
	m_pCharacter(nullptr),
	m_UseSpawnPosOverride(false)
{
	m_aSelectedFile[0] = '\0';
	m_LiveInput = {};
}

void CTAS::OnInit()
{
	if(g_Config.m_ClTasFile[0] != '\0')
		SetSelectedFile(g_Config.m_ClTasFile);
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
	Console()->Register("tas_save", "s[name]", CFGFLAG_CLIENT, ConTasSave, this, "Save TAS recording (default: selected file)");
	Console()->Register("tas_load", "s[name]", CFGFLAG_CLIENT, ConTasLoad, this, "Load TAS recording (default: selected file)");
	Console()->Register("tas_list", "", CFGFLAG_CLIENT, ConTasList, this, "List TAS recordings in data/tas");
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
	m_UseSpawnPosOverride = false;
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
	m_UseSpawnPosOverride = false;
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

void CTAS::SetSelectedFile(const char *pFilename)
{
	if(!pFilename || pFilename[0] == '\0')
	{
		ClearSelectedFile();
		return;
	}
	str_copy(m_aSelectedFile, pFilename, sizeof(m_aSelectedFile));
	str_copy(g_Config.m_ClTasFile, pFilename, sizeof(g_Config.m_ClTasFile));
}

void CTAS::ClearSelectedFile()
{
	m_aSelectedFile[0] = '\0';
	g_Config.m_ClTasFile[0] = '\0';
}

vec2 CTAS::EndPos() const
{
	if(m_vRecording.empty())
		return m_SpawnPos;
	return m_vRecording.back().m_Core.m_Pos;
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

	if(!m_UseSpawnPosOverride)
	{
		m_SpawnPos = GameClient()->m_LocalCharacterPos;
		if(length(m_SpawnPos) < 0.1f)
			m_SpawnPos = vec2(0.0f, 0.0f);
	}

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

void CTAS::ApplyInputFreeze(CControls &Controls)
{
	if(!m_Active)
		return;

	if(g_Config.m_ClTasFreezeInput)
	{
		Controls.ResetInput(g_Config.m_ClDummy);
		Controls.m_aInputData[g_Config.m_ClDummy].m_TargetX = 0;
		Controls.m_aInputData[g_Config.m_ClDummy].m_TargetY = -1;
	}
}

CNetObj_PlayerInput CTAS::GetLiveInput() const
{
	if(m_Active)
		return m_LiveInput;
	return GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
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

static const char *GetTasFilenameOrSelected(CTAS *pTas, IConsole::IResult *pResult, char *pOutName, size_t OutSize)
{
	const char *pArgName = pResult->NumArguments() > 0 ? pResult->GetString(0) : "";
	if(pArgName && pArgName[0] != '\0')
	{
		if(NormalizeTasFilename(pArgName, pOutName, OutSize))
		{
			pTas->SetSelectedFile(pOutName);
			return pOutName;
		}
		return nullptr;
	}

	if(pTas->SelectedFile()[0] != '\0')
	{
		if(NormalizeTasFilename(pTas->SelectedFile(), pOutName, OutSize))
		{
			pTas->SetSelectedFile(pOutName);
			return pOutName;
		}
	}

	if(g_Config.m_ClTasFile[0] != '\0')
	{
		if(NormalizeTasFilename(g_Config.m_ClTasFile, pOutName, OutSize))
		{
			pTas->SetSelectedFile(pOutName);
			return pOutName;
		}
	}

	return nullptr;
}

void CTAS::ConTasSave(IConsole::IResult *pResult, void *pUserData)
{
	CTAS *pThis = static_cast<CTAS *>(pUserData);
	if(pThis->m_vRecording.empty())
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "No TAS recording to save");
		return;
	}

	char aFilename[IO_MAX_PATH_LENGTH];
	if(GetTasFilenameOrSelected(pThis, pResult, aFilename, sizeof(aFilename)) == nullptr)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "No TAS filename specified");
		return;
	}

	pThis->Storage()->CreateFolder("data", IStorage::TYPE_SAVE);
	pThis->Storage()->CreateFolder(TAS_DIR, IStorage::TYPE_SAVE);

	char aPath[IO_MAX_PATH_LENGTH];
	BuildTasPath(aFilename, aPath, sizeof(aPath));
	IOHANDLE File = pThis->Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "Failed to open TAS file for writing");
		return;
	}

	STasFileHeader Header = {};
	mem_copy(Header.m_aMagic, TAS_MAGIC, sizeof(Header.m_aMagic));
	Header.m_Version = TAS_FILE_VERSION;
	Header.m_NumTicks = static_cast<int>(pThis->m_vRecording.size());
	Header.m_SpawnX = pThis->m_SpawnPos.x;
	Header.m_SpawnY = pThis->m_SpawnPos.y;

	io_write(File, &Header, sizeof(Header));
	for(const auto &Tick : pThis->m_vRecording)
	{
		io_write(File, &Tick.m_Input, sizeof(Tick.m_Input));
	}
	io_close(File);

	char aMsg[IO_MAX_PATH_LENGTH + 32];
	str_format(aMsg, sizeof(aMsg), "Saved TAS to %s", aPath);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", aMsg);
}

void CTAS::ConTasLoad(IConsole::IResult *pResult, void *pUserData)
{
	CTAS *pThis = static_cast<CTAS *>(pUserData);

	char aFilename[IO_MAX_PATH_LENGTH];
	if(GetTasFilenameOrSelected(pThis, pResult, aFilename, sizeof(aFilename)) == nullptr)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "No TAS filename specified");
		return;
	}

	char aPath[IO_MAX_PATH_LENGTH];
	BuildTasPath(aFilename, aPath, sizeof(aPath));

	void *pData = nullptr;
	unsigned DataSize = 0;
	if(!pThis->Storage()->ReadFile(aPath, IStorage::TYPE_ALL, &pData, &DataSize))
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "Failed to read TAS file");
		return;
	}

	if(DataSize < sizeof(STasFileHeader))
	{
		free(pData);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS file too small");
		return;
	}

	const STasFileHeader *pHeader = static_cast<const STasFileHeader *>(pData);
	if(mem_comp(pHeader->m_aMagic, TAS_MAGIC, sizeof(TAS_MAGIC)) != 0 || pHeader->m_Version != TAS_FILE_VERSION)
	{
		free(pData);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "Unsupported TAS file format");
		return;
	}

	const size_t ExpectedSize = sizeof(STasFileHeader) + sizeof(CNetObj_PlayerInput) * static_cast<size_t>(pHeader->m_NumTicks);
	if(DataSize < ExpectedSize)
	{
		free(pData);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS file is truncated");
		return;
	}

	const CNetObj_PlayerInput *pInputs = reinterpret_cast<const CNetObj_PlayerInput *>(static_cast<const unsigned char *>(pData) + sizeof(STasFileHeader));
	pThis->m_vRecording.clear();
	pThis->m_vHistory.clear();
	pThis->m_PlayIndex = 0;
	pThis->m_Tick = 0;
	pThis->m_Status = EStatus::IDLE;
	pThis->m_StatusBeforePause = pThis->m_Status;
	pThis->m_UseSpawnPosOverride = true;
	pThis->m_SpawnPos = vec2(pHeader->m_SpawnX, pHeader->m_SpawnY);
	pThis->ResetWorld();

	for(int i = 0; i < pHeader->m_NumTicks; ++i)
	{
		pThis->TickOnce(pInputs[i], true, true);
	}

	pThis->m_vHistory.clear();
	pThis->m_Tick = 0;
	pThis->m_PlayIndex = 0;
	pThis->ResetWorld();

	free(pData);

	char aMsg[IO_MAX_PATH_LENGTH + 32];
	str_format(aMsg, sizeof(aMsg), "Loaded TAS from %s", aPath);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", aMsg);
}

void CTAS::ConTasList(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CTAS *pThis = static_cast<CTAS *>(pUserData);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "Available TAS files:");

	auto Callback = [](const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser) {
		CTAS *pTas = static_cast<CTAS *>(pUser);
		if(IsDir)
			return 0;
		if(!str_endswith(pInfo->m_pName, ".tas"))
			return 0;
		pTas->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", pInfo->m_pName);
		return 0;
	};

	pThis->Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, TAS_DIR, Callback, pThis);
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
