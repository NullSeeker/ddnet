#ifndef GAME_SERVER_ENTITIES_MEGASHIL_H
#define GAME_SERVER_ENTITIES_MEGASHIL_H

#include <game/server/entity.h>


class CShield : public CEntity
{
public:

    CShield(CGameWorld *pGameWorld, vec2 Pos); //конструктор
	~CShield(); //дестркутор
	virtual void Snap(int SnappingClient) override;

	int Type() const { return m_Type; }
	int Subtype() const { return m_Subtype; }

private:
	int m_Type;
	int m_Subtype;
	int m_LaserIDs[4];

	// DDRace

	void Move();
	vec2 m_Core;
};

#endif