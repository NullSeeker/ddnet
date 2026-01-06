/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_TAS_PLAYER_H
#define GAME_CLIENT_COMPONENTS_TAS_PLAYER_H

#include <game/client/component.h>

#include <vector>

struct CNetObj_PlayerInput;

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
	int CurrentTick() const { return m_CurrentTick; }
	int TotalTicks() const { return static_cast<int>(m_vTicks.size()); }
	float Speed() const { return m_Speed; }
	const char *FileName() const { return m_aFileName; }
	const char *MapName() const { return m_aMapName; }
	int Version() const { return m_Version; }

	bool Load(const char *pFilename);
	void Start();
	void Stop(bool PrintMessage = true);
	void TogglePause();
	void SetSpeed(float Speed);
	bool Seek(int Tick);
	void PrintInfo() const;

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
	char m_aMapName[64];
	int m_Version = 0;
	bool m_Active = false;
	bool m_Paused = false;
	float m_Speed = 1.0f;
	float m_SpeedAccumulator = 0.0f;
	int m_CurrentTick = 0;
	int m_FireCounter = 0;
	bool m_WarnedNoLocalCharacter = false;

	void ResetPlaybackState();
	void ApplyTick(const CTasTick &Tick, CNetObj_PlayerInput *pInput);
	void ApplyNeutralInput(CNetObj_PlayerInput &Input);
	bool AdvanceTick();
	void AddChatLine(const char *pMessage) const;
	void WarnNoLocalCharacter();
};

#endif
