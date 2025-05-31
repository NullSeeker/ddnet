#include "shield.h"
#include "character.h"

#include <game/generated/protocol.h>
#include <game/mapitems.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>


// спавн
CShield::CShield(CGameWorld *pGameWorld, vec2 Pos)
    : CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, Pos, 14)
{
    GameWorld()->InsertEntity(this);

    for(int i = 0; i < 4; i++)
        m_LaserIDs[i] = Server()->SnapNewId();
}

CShield::~CShield()
{
    for(int i = 0; i < 4; i++)
        Server()->SnapFreeId(m_LaserIDs[i]);
}

// отрисовка

void CShield::Snap(int SnappingClient)
{
    if(!Server() || NetworkClipped(SnappingClient))
        return;

    // Центральная позиция — как обычно
    CNetObj_Pickup *pPickup = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetId(), sizeof(CNetObj_Pickup)));
    if(!pPickup)
        return;

    pPickup->m_X = round_to_int(m_Pos.x);
    pPickup->m_Y = round_to_int(m_Pos.y);
    pPickup->m_Type = POWERUP_NINJA;

    // 🔲 Квадрат из лазеров
    vec2 p1 = m_Pos + vec2(-32, -32);
    vec2 p2 = m_Pos + vec2( 32, -32);
    vec2 p3 = m_Pos + vec2( 32,  32);
    vec2 p4 = m_Pos + vec2(-32,  32);

    // Каждому лазеру нужен уникальный ID

    // Лазер 1: p1 -> p2
    if(CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_LaserIDs[0], sizeof(CNetObj_Laser))))
    {
        pLaser->m_X = (int)p2.x;
        pLaser->m_Y = (int)p2.y;
        pLaser->m_FromX = (int)p1.x;
        pLaser->m_FromY = (int)p1.y;
        pLaser->m_StartTick = Server()->Tick();
    }

    // Лазер 2: p2 -> p3
    if(CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_LaserIDs[1], sizeof(CNetObj_Laser))))
    {
        pLaser->m_X = (int)p3.x;
        pLaser->m_Y = (int)p3.y;
        pLaser->m_FromX = (int)p2.x;
        pLaser->m_FromY = (int)p2.y;
        pLaser->m_StartTick = Server()->Tick();
    }

    // Лазер 3: p3 -> p4
    if(CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_LaserIDs[2], sizeof(CNetObj_Laser))))
    {
        pLaser->m_X = (int)p4.x;
        pLaser->m_Y = (int)p4.y;
        pLaser->m_FromX = (int)p3.x;
        pLaser->m_FromY = (int)p3.y;
        pLaser->m_StartTick = Server()->Tick();
    }

    // Лазер 4: p4 -> p1
    if(CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_LaserIDs[3], sizeof(CNetObj_Laser))))
    {
        pLaser->m_X = (int)p1.x;
        pLaser->m_Y = (int)p1.y;
        pLaser->m_FromX = (int)p4.x;
        pLaser->m_FromY = (int)p4.y;
        pLaser->m_StartTick = Server()->Tick();
    }

    for(int i = 0; i < 4; i++)
    {
        float Angle = 2.0f * pi * i / 4.0f + (Server()->Tick() % 360) * pi / 180.0f;
        vec2 Offset = vec2(cosf(Angle), sinf(Angle)) * 28.0f;
        vec2 ParticlePos = m_Pos + Offset;
        vec2 Velocity = vec2(cosf(Angle + pi / 2.0f), sinf(Angle + pi / 2.0f)) * 0.01f;
    
        CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(
            Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, Server()->SnapNewId(), sizeof(CNetObj_Projectile))
        );
    
        if(pProj)
        {
            pProj->m_X = (int)(ParticlePos.x);
            pProj->m_Y = (int)(ParticlePos.y);
            pProj->m_VelX = (int)(Velocity.x * 100.0f); // масштабируем
            pProj->m_VelY = (int)(Velocity.y * 100.0f);
            pProj->m_Type = WEAPON_SHOTGUN; // можно поменять на WEAPON_SHOTGUN, NINJA и т.д.
            pProj->m_StartTick = Server()->Tick();
        }
    }
}