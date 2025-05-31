#ifndef GAME_SERVER_ENTITIES_MEGASHIL_H
#define GAME_SERVER_ENTITIES_MEGASHIL_H

#include <game/server/entity.h>


class CShield : public CEntity
{
public:

    CShield(CGameWorld *pGameWorld, vec2 Pos); //конструктор
	~CShield(); //дестркутор
	virtual void Snap(int SnappingClient) override;
	virtual void Tick() override;

	static std::vector<CShield *> s_ActiveShields;
	static int s_ShieldsLifeEndTick;

	int m_ProjectileIDs[14]; // 8 + 6 = 14 projectiles (внешние + вихрь)

private:
	
	int m_LaserIDs[4]; // для отрисовки
	
	int m_LifeTimeTicks; //время жизини щита

};

#endif