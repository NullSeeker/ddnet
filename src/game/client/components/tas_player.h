/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_TAS_PLAYER_H
#define GAME_CLIENT_COMPONENTS_TAS_PLAYER_H

#include <game/client/component.h>
#include <game/prng.h>
#include <game/teamscore.h>

#include <generated/protocol.h>
#include <game/gamecore.h>

#include <vector>

class CTasPlayer : public CComponent
{
public:
	CTasPlayer();

	void OnConsoleInit() override;
	void OnRender() override;
	void OnReset() override;
	void OnShutdown() override;
	int Sizeof() const override { return sizeof(*this); }

	bool ApplyInput(CNetObj_PlayerInput &Input, int GameTick);

	bool IsActive() const { return m_Active; }
	bool IsPaused() const { return m_Paused; }
	bool IsRecording() const { return m_Recording; }
	int CurrentTick() const { return m_CurrentTick; }
	int TotalTicks() const { return static_cast<int>(m_vTicks.size()); }
	float TimeScale() const { return m_TimeScale; }
	const char *FileName() const { return m_aFileName; }
	const char *MapName() const { return m_aMapName; }
	int Version() const { return m_Version; }
	bool RecordingGhostPosition(vec2 &Pos) const;

	bool Load(const char *pFilename);
	void Start();
	void Stop(bool PrintMessage = true);
	void SetPaused(bool Paused);
	void SetTimeScale(float TimeScale);
	void Step();
	bool Seek(int Tick);
	void PrintInfo() const;
	void StartRecord();
	void StopRecording(bool PrintMessage = true);
	bool SaveRecordingToFile(const char *pFilename) const;
	void ClearRecording();
	void SetShowGhost(bool Show);
	void SetGhostAlpha(float Alpha);

private:
	struct CTasTick
	{
		int m_Direction;
		int m_Jump;
		int m_Fire;
		int m_Hook;
		int m_WantedWeapon;
		int m_TargetX;
		int m_TargetY;
	};

	std::vector<CTasTick> m_vTicks;
	char m_aFileName[512];
	char m_aRecordFileName[512];
	char m_aMapName[64];
	int m_Version = 0;
	bool m_Active = false;
	bool m_Paused = false;
	bool m_Recording = false;
	bool m_ShowGhost = true;
	float m_GhostAlpha = 0.35f;
	float m_TimeScale = 1.0f;
	float m_TimeScaleAccumulator = 0.0f;
	float m_RecordTimeScaleAccumulator = 0.0f;
	int m_PendingSteps = 0;
	int m_CurrentTick = 0;
	int m_FireCounter = 0;
	bool m_WarnedNoLocalCharacter = false;
	int m_GhostTick = 0;
	int m_GhostAttackTick = 0;
	bool m_GhostReady = false;
	CNetObj_PlayerInput m_GhostInput = {};
	CNetObj_Character m_GhostPrevChar = {};
	CNetObj_Character m_GhostCurChar = {};
	CPrng m_GhostPrng;
	CWorldCore m_GhostWorld;
	CCharacterCore m_GhostCore;

	void ResetPlaybackState();
	void ApplyTick(const CTasTick &Tick, CNetObj_PlayerInput *pInput);
	void ApplyNeutralInput(CNetObj_PlayerInput &Input);
	void ApplyRecordingInput(const CNetObj_PlayerInput &Input, int GameTick);
	bool AdvanceTick();
	void AddChatLine(const char *pMessage) const;
	void WarnNoLocalCharacter();
	void UpdateGhost(const CNetObj_PlayerInput &Input);
	void ResetGhostState();
	void BuildGhostNetChar(CNetObj_Character &Out, const CCharacterCore &Core) const;
	void RenderGhost();
	int ConsumeAdvanceSteps(float &Accumulator);
};

#endif
