/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "tas_player.h"

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/external/json-parser/json.h>

#include <generated/protocol.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <game/client/components/players.h>

#include <base/math.h>
#include <base/system.h>

#include <algorithm>

namespace
{
bool JsonIsNumber(const json_value &Value)
{
	return Value.type == json_integer || Value.type == json_double;
}

int JsonToInt(const json_value &Value, int DefaultValue)
{
	if(Value.type == json_integer)
		return static_cast<int>(Value.u.integer);
	if(Value.type == json_double)
		return static_cast<int>(Value.u.dbl);
	if(Value.type == json_boolean)
		return Value.u.boolean ? 1 : 0;
	return DefaultValue;
}
}

CTasPlayer::CTasPlayer()
{
	m_aFileName[0] = '\0';
	m_aRecordFileName[0] = '\0';
	m_aMapName[0] = '\0';
}

void CTasPlayer::OnConsoleInit()
{
	Console()->Register("cl_tas_play", "s[file]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) {
		auto *pSelf = static_cast<CTasPlayer *>(pUserData);
		if(pSelf->Load(pResult->GetString(0)))
		{
			pSelf->Start();
		}
	}, this, "Load and start TAS playback from file");

	Console()->Register("cl_tas_stop", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUserData) {
		auto *pSelf = static_cast<CTasPlayer *>(pUserData);
		if(pSelf->IsRecording())
			pSelf->StopRecording(true, true);
		else
			pSelf->Stop();
	}, this, "Stop TAS playback or recording and clear state");

	Console()->Register("cl_tas_pause", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUserData) {
		static_cast<CTasPlayer *>(pUserData)->TogglePause();
	}, this, "Toggle TAS playback pause");

	Console()->Register("cl_tas_speed", "f[speed]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) {
		static_cast<CTasPlayer *>(pUserData)->SetSpeed(pResult->GetFloat(0));
	}, this, "Set TAS playback speed (0.1 - 10.0)");

	Console()->Register("cl_tas_seek", "i[tick]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) {
		static_cast<CTasPlayer *>(pUserData)->Seek(pResult->GetInteger(0));
	}, this, "Seek to TAS tick (updates playback index only)");

	Console()->Register("cl_tas_info", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUserData) {
		static_cast<CTasPlayer *>(pUserData)->PrintInfo();
	}, this, "Print information about loaded TAS");

	Console()->Register("cl_tas_record", "s[file]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) {
		static_cast<CTasPlayer *>(pUserData)->StartRecord(pResult->GetString(0));
	}, this, "Start TAS recording to file (client-side ghost)");
}

void CTasPlayer::OnRender()
{
	RenderRecordingGhost();

	if(!m_Active || !g_Config.m_ClTasHud)
		return;

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "TAS %s t=%d/%d spd=%.2f %s",
		m_Paused ? "PAUSE" : "PLAY",
		m_CurrentTick,
		TotalTicks(),
		m_Speed,
		m_aFileName);

	const float FontSize = 6.0f;
	const float Spacing = 4.0f;
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->Text(Spacing, Spacing, FontSize, aBuf);
}

void CTasPlayer::OnReset()
{
	Stop(false);
	StopRecording(false, false);
}

void CTasPlayer::OnShutdown()
{
	Stop(false);
	StopRecording(false, false);
}

bool CTasPlayer::ApplyInput(CNetObj_PlayerInput &Input, int GameTick)
{
	if(m_Recording)
	{
		const CNetObj_PlayerInput RecordedInput = Input;
		ApplyRecordingInput(RecordedInput, GameTick);
		ApplyNeutralInput(Input);
		return true;
	}

	if(!m_Active)
		return false;

	if(m_Paused)
	{
		ApplyNeutralInput(Input);
		return true;
	}

	if(!GameClient()->m_Snap.m_pLocalCharacter)
	{
		WarnNoLocalCharacter();
		ApplyNeutralInput(Input);
		return true;
	}

	m_WarnedNoLocalCharacter = false;

	if(m_vTicks.empty())
	{
		Stop();
		return true;
	}

	const float ClampedSpeed = std::clamp(m_Speed, 0.1f, 10.0f);
	m_SpeedAccumulator += ClampedSpeed;

	int StepsToAdvance = static_cast<int>(m_SpeedAccumulator);
	if(StepsToAdvance > 0)
		m_SpeedAccumulator -= StepsToAdvance;

	const int StepsToProcess = StepsToAdvance > 0 ? StepsToAdvance : 1;
	CTasTick LastTick = m_vTicks[m_CurrentTick];

	for(int i = 0; i < StepsToProcess; ++i)
	{
		if(m_CurrentTick >= TotalTicks())
			break;

		const CTasTick &Tick = m_vTicks[m_CurrentTick];
		LastTick = Tick;
		if(Tick.m_Fire)
		{
			if((m_FireCounter & 1) == 0)
				++m_FireCounter;
		}
		else
		{
			if((m_FireCounter & 1) != 0)
				++m_FireCounter;
		}

		if(StepsToAdvance > 0)
		{
			if(!AdvanceTick())
				break;
		}
	}

	ApplyTick(LastTick, &Input);

	(void)GameTick;
	return true;
}

void CTasPlayer::ApplyRecordingInput(const CNetObj_PlayerInput &Input, int GameTick)
{
	if(m_Paused)
		return;

	if(!GameClient()->m_Snap.m_pLocalCharacter)
	{
		WarnNoLocalCharacter();
		return;
	}

	m_WarnedNoLocalCharacter = false;

	const float ClampedSpeed = std::clamp(m_Speed, 0.1f, 10.0f);
	m_RecordSpeedAccumulator += ClampedSpeed;

	int StepsToAdvance = static_cast<int>(m_RecordSpeedAccumulator);
	if(StepsToAdvance > 0)
		m_RecordSpeedAccumulator -= StepsToAdvance;

	const int StepsToProcess = StepsToAdvance > 0 ? StepsToAdvance : 1;

	for(int i = 0; i < StepsToProcess; ++i)
	{
		if(StepsToAdvance > 0 && m_RecordGhostReady)
			m_RecordGhostTick++;

		UpdateRecordingGhost(Input);

		CTasTick Tick{};
		Tick.m_Direction = std::clamp(Input.m_Direction, -1, 1);
		Tick.m_Jump = Input.m_Jump ? 1 : 0;
		Tick.m_Fire = (Input.m_Fire & 1) != 0 ? 1 : 0;
		Tick.m_Hook = Input.m_Hook ? 1 : 0;
		Tick.m_WantedWeapon = std::clamp(Input.m_WantedWeapon, 0, static_cast<int>(NUM_WEAPONS));
		Tick.m_TargetX = Input.m_TargetX;
		Tick.m_TargetY = Input.m_TargetY;
		m_vTicks.push_back(Tick);

		if(StepsToAdvance == 0)
			break;
	}

	m_RecordInput = Input;
	(void)GameTick;
}

bool CTasPlayer::Load(const char *pFilename)
{
	Stop(false);

	if(!pFilename || pFilename[0] == '\0')
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS: missing filename");
		return false;
	}

	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!Storage()->ReadFile(pFilename, IStorage::TYPE_SAVE_OR_ABSOLUTE, &pFileData, &FileSize))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS: failed to read file");
		return false;
	}

	json_settings JsonSettings{};
	char aError[256];
	json_value *pData = json_parse_ex(&JsonSettings, static_cast<const json_char *>(pFileData), FileSize, aError);
	free(pFileData);

	if(!pData)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "TAS: parse error: %s", aError);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", aBuf);
		return false;
	}

	if(pData->type != json_object)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS: root must be object");
		json_value_free(pData);
		return false;
	}

	const json_value &Version = (*pData)["version"];
	if(!JsonIsNumber(Version))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS: missing or invalid version");
		json_value_free(pData);
		return false;
	}
	m_Version = JsonToInt(Version, 0);

	const json_value &MapName = (*pData)["map"];
	if(MapName.type == json_string)
	{
		str_copy(m_aMapName, MapName.u.string.ptr, sizeof(m_aMapName));
	}
	else
	{
		m_aMapName[0] = '\0';
	}

	const json_value &Ticks = (*pData)["ticks"];
	if(Ticks.type != json_array)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS: ticks must be array");
		json_value_free(pData);
		return false;
	}

	m_vTicks.clear();
	m_vTicks.reserve(Ticks.u.array.length);

	for(unsigned i = 0; i < Ticks.u.array.length; ++i)
	{
		const json_value &JsonTick = Ticks[i];
		if(JsonTick.type != json_object)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS: tick entry must be object");
			m_vTicks.clear();
			json_value_free(pData);
			return false;
		}

		const json_value &Dir = JsonTick["dir"];
		const json_value &Jump = JsonTick["jump"];
		const json_value &Fire = JsonTick["fire"];
		const json_value &Hook = JsonTick["hook"];
		const json_value &Weapon = JsonTick["weapon"];
		const json_value &TargetX = JsonTick["tx"];
		const json_value &TargetY = JsonTick["ty"];
		const json_value &LegacyTargetX = JsonTick["wx"];
		const json_value &LegacyTargetY = JsonTick["wy"];

		CTasTick Tick{};
		Tick.m_Direction = std::clamp(JsonToInt(Dir, 0), -1, 1);
		Tick.m_Jump = JsonToInt(Jump, 0) ? 1 : 0;
		Tick.m_Fire = JsonToInt(Fire, 0) ? 1 : 0;
		Tick.m_Hook = JsonToInt(Hook, 0) ? 1 : 0;
		Tick.m_WantedWeapon = std::clamp(JsonToInt(Weapon, 0), 0, static_cast<int>(NUM_WEAPONS));
		Tick.m_TargetX = JsonToInt(TargetX, JsonToInt(LegacyTargetX, 0));
		Tick.m_TargetY = JsonToInt(TargetY, JsonToInt(LegacyTargetY, 0));

		m_vTicks.push_back(Tick);
	}

	json_value_free(pData);

	str_copy(m_aFileName, pFilename, sizeof(m_aFileName));
	return true;
}

void CTasPlayer::Start()
{
	StopRecording(false, false);
	ResetPlaybackState();
	m_Active = true;
	m_Paused = false;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "TAS: playing %s, ticks=%d, speed=%.2f", m_aFileName, TotalTicks(), m_Speed);
	AddChatLine(aBuf);
}

void CTasPlayer::Stop(bool PrintMessage)
{
	if(!m_Active && m_vTicks.empty())
		return;

	m_Active = false;
	m_Paused = false;
	m_SpeedAccumulator = 0.0f;
	m_CurrentTick = 0;
	m_FireCounter = 0;
	m_vTicks.clear();
	m_aFileName[0] = '\0';
	m_aMapName[0] = '\0';
	m_Version = 0;
	m_WarnedNoLocalCharacter = false;

	if(PrintMessage)
		AddChatLine("TAS: stopped");
}

void CTasPlayer::TogglePause()
{
	if(!m_Active && !m_Recording)
		return;

	m_Paused = !m_Paused;
	AddChatLine(m_Paused ? "TAS: paused" : "TAS: resumed");
}

void CTasPlayer::SetSpeed(float Speed)
{
	m_Speed = std::clamp(Speed, 0.1f, 10.0f);

	if(m_Active)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "TAS: speed set to %.2f", m_Speed);
		AddChatLine(aBuf);
	}
}

bool CTasPlayer::Seek(int Tick)
{
	if(m_vTicks.empty())
		return false;

	const int TargetTick = std::clamp(Tick, 0, TotalTicks() - 1);
	ResetPlaybackState();
	m_CurrentTick = TargetTick;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "TAS: seek to %d/%d", m_CurrentTick, TotalTicks());
	AddChatLine(aBuf);
	return true;
}

void CTasPlayer::PrintInfo() const
{
	if(m_vTicks.empty())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS: no file loaded");
		return;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "TAS: file=%s version=%d map=%s ticks=%d current=%d speed=%.2f",
		m_aFileName[0] ? m_aFileName : "<none>",
		m_Version,
		m_aMapName[0] ? m_aMapName : "<none>",
		TotalTicks(),
		m_CurrentTick,
		m_Speed);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", aBuf);
}

void CTasPlayer::StartRecord(const char *pFilename)
{
	if(!pFilename || pFilename[0] == '\0')
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS: missing filename");
		return;
	}

	Stop(false);
	StopRecording(false, false);

	if(!GameClient() || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", "TAS: local character missing");
		return;
	}

	str_copy(m_aRecordFileName, pFilename, sizeof(m_aRecordFileName));
	str_copy(m_aMapName, Client()->GetCurrentMap(), sizeof(m_aMapName));

	m_vTicks.clear();
	m_Version = 1;
	m_Recording = true;
	m_Paused = false;
	m_RecordSpeedAccumulator = 0.0f;
	m_RecordGhostTick = 0;
	m_RecordAttackTick = 0;
	m_RecordGhostReady = false;
	m_RecordInput = {};
	m_RecordPrevChar = {};
	m_RecordCurChar = {};

	ResetRecordingState();

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "TAS: recording to %s (speed %.2f)", m_aRecordFileName, m_Speed);
	AddChatLine(aBuf);
}

void CTasPlayer::ResetPlaybackState()
{
	m_CurrentTick = 0;
	m_FireCounter = 0;
	m_SpeedAccumulator = 0.0f;
	m_WarnedNoLocalCharacter = false;
}

void CTasPlayer::ApplyTick(const CTasTick &Tick, CNetObj_PlayerInput *pInput)
{
	pInput->m_Direction = Tick.m_Direction;
	pInput->m_Jump = Tick.m_Jump;
	pInput->m_Hook = Tick.m_Hook;

	pInput->m_Fire = m_FireCounter;
	pInput->m_WantedWeapon = Tick.m_WantedWeapon;
	pInput->m_TargetX = Tick.m_TargetX;
	pInput->m_TargetY = Tick.m_TargetY;
}

void CTasPlayer::ApplyNeutralInput(CNetObj_PlayerInput &Input)
{
	Input.m_Direction = 0;
	Input.m_Jump = 0;
	Input.m_Hook = 0;
	if((Input.m_Fire & 1) != 0)
		Input.m_Fire++;
	Input.m_Fire &= INPUT_STATE_MASK;
}

bool CTasPlayer::AdvanceTick()
{
	++m_CurrentTick;
	if(m_CurrentTick >= TotalTicks())
	{
		if(g_Config.m_ClTasLoop)
		{
			m_CurrentTick = 0;
			return true;
		}
		else
		{
			Stop();
			return false;
		}
	}
	return true;
}

void CTasPlayer::AddChatLine(const char *pMessage) const
{
	constexpr int SERVER_MSG_CLIENT_ID = -1;

	if(GameClient() && GameClient()->m_Chat.IsActive())
	{
		GameClient()->m_Chat.AddLine(SERVER_MSG_CLIENT_ID, 0, pMessage);
		return;
	}

	if(GameClient())
		GameClient()->m_Chat.AddLine(SERVER_MSG_CLIENT_ID, 0, pMessage);
	else
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tas", pMessage);
}

void CTasPlayer::WarnNoLocalCharacter()
{
	if(m_WarnedNoLocalCharacter)
		return;

	m_WarnedNoLocalCharacter = true;
	AddChatLine("TAS: local character missing, input suppressed");
}

void CTasPlayer::StopRecording(bool SaveFile, bool PrintMessage)
{
	if(!m_Recording)
		return;

	m_Recording = false;
	m_Paused = false;

	if(SaveFile && m_aRecordFileName[0] != '\0')
	{
		if(!SaveRecording(m_aRecordFileName))
			AddChatLine("TAS: failed to save recording");
		else if(PrintMessage)
			AddChatLine("TAS: recording saved");
	}
	else if(PrintMessage)
	{
		AddChatLine("TAS: recording stopped");
	}

	m_aRecordFileName[0] = '\0';
}

bool CTasPlayer::SaveRecording(const char *pFilename) const
{
	IOHANDLE File = Storage()->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE);
	if(!File)
		return false;

	io_write(File, "{\n", 2);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "  \"version\": %d,\n", m_Version);
	io_write(File, aBuf, str_length(aBuf));

	str_format(aBuf, sizeof(aBuf), "  \"map\": \"%s\",\n", m_aMapName);
	io_write(File, aBuf, str_length(aBuf));

	io_write(File, "  \"ticks\": [\n", 13);

	for(size_t i = 0; i < m_vTicks.size(); ++i)
	{
		const CTasTick &Tick = m_vTicks[i];
		str_format(aBuf, sizeof(aBuf),
			"    {\"dir\":%d,\"jump\":%d,\"fire\":%d,\"hook\":%d,\"weapon\":%d,\"tx\":%d,\"ty\":%d}%s\n",
			Tick.m_Direction,
			Tick.m_Jump,
			Tick.m_Fire,
			Tick.m_Hook,
			Tick.m_WantedWeapon,
			Tick.m_TargetX,
			Tick.m_TargetY,
			i + 1 == m_vTicks.size() ? "" : ",");
		io_write(File, aBuf, str_length(aBuf));
	}

	io_write(File, "  ]\n}\n", 7);
	io_close(File);
	return true;
}

void CTasPlayer::UpdateRecordingGhost(const CNetObj_PlayerInput &Input)
{
	if(!m_RecordGhostReady)
	{
		m_RecordGhostReady = true;
		const CCharacterCore &BaseCore = GameClient()->m_PredictedChar;
		m_RecordCore = BaseCore;
		m_RecordCore.m_Id = 0;
		m_RecordCore.m_Input = Input;
		m_RecordCore.m_HookTeleBase = BaseCore.m_Pos;
		m_RecordCore.m_TriggeredEvents = 0;
		m_RecordCore.SetCoreWorld(&m_RecordWorld, Collision(), &GameClient()->m_Teams);
		m_RecordWorld.m_apCharacters[0] = &m_RecordCore;
		m_RecordWorld.m_pPrng = &m_RecordPrng;
		uint64_t aSeed[2] = {static_cast<uint64_t>(time_get()), static_cast<uint64_t>(time_get() ^ 0x9e3779b97f4a7c15ULL)};
		m_RecordPrng.Seed(aSeed);
		m_RecordGhostTick = GameClient()->m_Snap.m_pLocalCharacter->m_Tick;
		m_RecordAttackTick = m_RecordGhostTick;
		BuildRecordNetChar(m_RecordCurChar, m_RecordCore);
		m_RecordPrevChar = m_RecordCurChar;
		return;
	}

	m_RecordPrevChar = m_RecordCurChar;
	m_RecordCore.m_Input = Input;

	const int PrevFire = m_RecordInput.m_Fire & INPUT_STATE_MASK;
	const int CurFire = Input.m_Fire & INPUT_STATE_MASK;
	const CInputCount FireCount = CountInput(PrevFire, CurFire);
	if(FireCount.m_Presses > 0)
		m_RecordAttackTick = m_RecordGhostTick;

	m_RecordCore.Tick(true);
	m_RecordCore.Move();
	m_RecordCore.Quantize();

	BuildRecordNetChar(m_RecordCurChar, m_RecordCore);
	m_RecordCurChar.m_AttackTick = m_RecordAttackTick;
	m_RecordCurChar.m_Tick = m_RecordGhostTick;
	m_RecordPrevChar.m_Tick = m_RecordGhostTick - 1;
}

void CTasPlayer::ResetRecordingState()
{
	m_RecordWorld.m_pPrng = &m_RecordPrng;
	for(auto &pChar : m_RecordWorld.m_apCharacters)
		pChar = nullptr;
	m_RecordWorld.m_vSwitchers.clear();
	m_RecordCore.SetCoreWorld(&m_RecordWorld, Collision(), &GameClient()->m_Teams);
}

void CTasPlayer::BuildRecordNetChar(CNetObj_Character &Out, const CCharacterCore &Core) const
{
	mem_zero(&Out, sizeof(Out));
	Out.m_X = round_to_int(Core.m_Pos.x);
	Out.m_Y = round_to_int(Core.m_Pos.y);
	Out.m_VelX = round_to_int(Core.m_Vel.x * 256.0f);
	Out.m_VelY = round_to_int(Core.m_Vel.y * 256.0f);
	Out.m_Angle = Core.m_Angle;
	Out.m_Direction = Core.m_Direction;
	Out.m_Weapon = Core.m_ActiveWeapon;
	Out.m_HookState = Core.m_HookState;
	Out.m_HookX = round_to_int(Core.m_HookPos.x);
	Out.m_HookY = round_to_int(Core.m_HookPos.y);
	Out.m_AttackTick = m_RecordAttackTick;
	Out.m_Tick = m_RecordGhostTick;
}

void CTasPlayer::RenderRecordingGhost()
{
	if(!m_Recording || !m_RecordGhostReady)
		return;

	if(GameClient()->m_Snap.m_LocalClientId < 0)
		return;

	const CTeeRenderInfo *pRenderInfo = &GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_RenderInfo;
	const float IntraTick = Client()->PredIntraGameTick(g_Config.m_ClDummy);

	GameClient()->m_Players.RenderHook(&m_RecordPrevChar, &m_RecordCurChar, pRenderInfo, -2, IntraTick);
	GameClient()->m_Players.RenderPlayer(&m_RecordPrevChar, &m_RecordCurChar, pRenderInfo, -2, IntraTick);
}
