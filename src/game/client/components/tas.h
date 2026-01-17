/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_TAS_H
#define GAME_CLIENT_COMPONENTS_TAS_H

#include <engine/console.h>

#include <base/system.h>

#include <game/client/component.h>
#include <game/client/prediction/gameworld.h>
#include <game/gamecore.h>

#include <generated/protocol.h>

#include <vector>

class CCharacter;
class CControls;

class CTAS : public CComponent
{
public:
	enum class EStatus
	{
		IDLE,
		RECORDING,
		PLAYING,
		PAUSED,
	};

	enum class EMode
	{
		DEFAULT,
		BINDS,
	};


 enum class EPlaybackSpeed
        {
                REWIND_FAST = -4,
                REWIND_MEDIUM = -2,
                REWIND_SLOW = -1,
                STOPPED = 0,
                PLAYBACK_SLOW = 1,
                PLAYBACK_MEDIUM = 2,
                PLAYBACK_FAST = 4,
        };
	struct STASTick
	{
		int m_Tick;
		CNetObj_PlayerInput m_Input;
		CCharacterCore m_Core;
	};

	CTAS();
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnUpdate() override;
	void OnConsoleInit() override;

	void Enter();
	void Record();
	void Stop();
	void Play();
	void Pause();
	void Clear();
	void StepForward();
	void StepRewind();

	bool IsActive() const { return m_Active; }
	EStatus Status() const { return m_Status; }
	const char *StatusName() const;
	const char *ModeName() const;
	int CurrentTick() const { return m_Tick; }
	int RecordedTicks() const { return static_cast<int>(m_vRecording.size()); }
	vec2 SpawnPos() const { return m_SpawnPos; }
	vec2 EndPos() const;
	bool HasRecording() const { return !m_vRecording.empty(); }
	const char *SelectedFile() const { return m_aSelectedFile; }
	void SetSelectedFile(const char *pFilename);
	
	// Time manipulation methods
	void SetPlaybackSpeed(EPlaybackSpeed Speed);
	EPlaybackSpeed GetPlaybackSpeed() const { return m_PlaybackSpeed; }
	void AdjustTime(float Factor); // For slow motion/fast forward

private:
	void EnsureWorld();
	void ResetWorld();
	void TickOnce(const CNetObj_PlayerInput &Input, bool RecordInput, bool StoreHistory);
	void UpdateAutoTicks();
	void ApplyInputFreeze(CControls &Controls);
	CNetObj_PlayerInput GetLiveInput() const;
	void SimulateToTick(int TargetTick);
	bool ShouldAutoTick() const;
	const char *StatusName(EStatus Status) const;
	void ClearSelectedFile();

 // Time manipulation methods
        void SetPlaybackSpeed(EPlaybackSpeed Speed);
        EPlaybackSpeed GetPlaybackSpeed() const { return m_PlaybackSpeed; }
        void AdjustTime(float Factor); // For slow motion/fast forward

	static void ConTasEnter(IConsole::IResult *pResult, void *pUserData);
	static void ConTasRecord(IConsole::IResult *pResult, void *pUserData);
	static void ConTasStop(IConsole::IResult *pResult, void *pUserData);
	static void ConTasPlay(IConsole::IResult *pResult, void *pUserData);
	static void ConTasPause(IConsole::IResult *pResult, void *pUserData);
	static void ConTasClear(IConsole::IResult *pResult, void *pUserData);
	static void ConTasForward(IConsole::IResult *pResult, void *pUserData);
	static void ConTasRewind(IConsole::IResult *pResult, void *pUserData);
	static void ConTasSave(IConsole::IResult *pResult, void *pUserData);
	static void ConTasLoad(IConsole::IResult *pResult, void *pUserData);
	static void ConTasList(IConsole::IResult *pResult, void *pUserData);
	// New console commands for time manipulation
	static void ConTasSpeedUp(IConsole::IResult *pResult, void *pUserData);
	static void ConTasSlowDown(IConsole::IResult *pResult, void *pUserData);
	static void ConTasNormalSpeed(IConsole::IResult *pResult, void *pUserData);
	static void ConTasRewindBack(IConsole::IResult *pResult, void *pUserData);
	static void ConTasFastForward(IConsole::IResult *pResult, void *pUserData);

 // New console commands for time manipulation
        static void ConTasSpeedUp(IConsole::IResult *pResult, void *pUserData);
        static void ConTasSlowDown(IConsole::IResult *pResult, void *pUserData);
        static void ConTasNormalSpeed(IConsole::IResult *pResult, void *pUserData);
        static void ConTasRewindBack(IConsole::IResult *pResult, void *pUserData);
        static void ConTasFastForward(IConsole::IResult *pResult, void *pUserData);

	bool m_Active;
	EStatus m_Status;
	EStatus m_StatusBeforePause;
	int m_Tick;
	int m_PlayIndex;
	int64_t m_LastTickTime;
	double m_TickRemainder;
	vec2 m_SpawnPos;
	char m_aSelectedFile[IO_MAX_PATH_LENGTH];
	bool m_UseSpawnPosOverride;
	CNetObj_PlayerInput m_LiveInput;

	CGameWorld m_World;
	CCharacter *m_pCharacter;
	CTuningParams m_aTuningList[TuneZone::NUM];

	std::vector<STASTick> m_vRecording;
	std::vector<STASTick> m_vHistory;

 // Time manipulation variables
        EPlaybackSpeed m_PlaybackSpeed;
        int64_t m_LastAdjustedTickTime;
        double m_AdjustedTickRemainder;

};

#endif
