/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_CONTROLS_H

#include <base/types.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/console.h>

#include <generated/protocol.h>

#include <game/client/component.h>

#include <vector>

class CControls : public CComponent
{
public:
	float GetMinMouseDistance() const;
	float GetMaxMouseDistance() const;

	enum class EMouseInputType
	{
		ABSOLUTE,
		RELATIVE,
		AUTOMATED,
	};

	vec2 m_aMousePos[NUM_DUMMIES];
	vec2 m_aMousePosOnAction[NUM_DUMMIES];
	vec2 m_aTargetPos[NUM_DUMMIES];

	EMouseInputType m_aMouseInputType[NUM_DUMMIES];

	int m_aAmmoCount[NUM_WEAPONS];

	int64_t m_LastSendTime;
	CNetObj_PlayerInput m_aInputData[NUM_DUMMIES];
	CNetObj_PlayerInput m_aLastData[NUM_DUMMIES];
	int m_aInputDirectionLeft[NUM_DUMMIES];
	int m_aInputDirectionRight[NUM_DUMMIES];
	int m_aShowHookColl[NUM_DUMMIES];

	CControls();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnConsoleInit() override;
	virtual void OnPlayerDeath();

	int SnapInput(int *pData);
	void ClampMousePos();
	void ResetInput(int Dummy);

private:
	struct CTasInput
	{
		CNetObj_PlayerInput m_Input{};
		ivec2 m_Position = ivec2(0, 0);
		bool m_HasPosition = false;
	};

	static void ConKeyInputState(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputSet(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData);

	static void ConTasRecord(IConsole::IResult *pResult, void *pUserData);
	static void ConTasPlay(IConsole::IResult *pResult, void *pUserData);
	static void ConTasStop(IConsole::IResult *pResult, void *pUserData);

	void StartTasRecording(const char *pFilename);
	void StopTasRecording(bool SaveToFile);
	void StartTasPlayback(const char *pFilename);
	void StopTasPlayback();
	bool SaveTasFile(const char *pFilename) const;
	bool LoadTasFile(const char *pFilename);
	static bool ParseTasLine(const char *pLine, CTasInput &Input, bool Extended);

	bool m_TasRecording = false;
	bool m_TasPlaying = false;
	size_t m_TasPlaybackIndex = 0;
	int m_TasPlaybackSlowFactor = 1;
	int m_TasPlaybackSlowCounter = 0;
	bool m_TasPlaybackCheckedStart = false;
	bool m_TasPlaybackHasStartPosition = false;
	ivec2 m_TasPlaybackStartPosition = ivec2(0, 0);
	bool m_TasHasPositionData = false;
	std::vector<CTasInput> m_vTasInputs;
	char m_aTasFilename[IO_MAX_PATH_LENGTH] = {};
};
#endif
