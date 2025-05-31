#include "shield.h"
#include "character.h"

#include <game/generated/protocol.h>
#include <game/mapitems.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

std::vector<CShield *> CShield::s_ActiveShields;
int CShield::s_ShieldsLifeEndTick = -1;

// спавн
CShield::CShield(CGameWorld *pGameWorld, vec2 Pos)
    : CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, Pos, 14)
{
    GameWorld()->InsertEntity(this);
    GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);

    for(int i = 0; i < 4; i++)
        m_LaserIDs[i] = Server()->SnapNewId();

    s_ActiveShields.push_back(this);

    if(s_ActiveShields.size() == 2)
    {
        // Устанавливаем таймер для обоих щитов
        s_ShieldsLifeEndTick = Server()->Tick() + Server()->TickSpeed() * 10; // 30 сек
        // Обновляем у второго щита тоже
        for(auto pShield : s_ActiveShields)
        {
            pShield->m_LifeTimeTicks = s_ShieldsLifeEndTick;
        }
    }
    else
    {
        // Если щит один — живём вечно (или очень долго)
        m_LifeTimeTicks = -1;
    }
}


CShield::~CShield()
{
    for(int i = 0; i < 4; i++)
        Server()->SnapFreeId(m_LaserIDs[i]);

    auto it = std::find(s_ActiveShields.begin(), s_ActiveShields.end(), this);
    if(it != s_ActiveShields.end())
        s_ActiveShields.erase(it);

    // Если щитов стало меньше двух — сбрасываем таймер
    if(s_ActiveShields.size() < 2)
        s_ShieldsLifeEndTick = -1;

    // Если остался один щит — он живёт вечно
    if(s_ActiveShields.size() == 1)
        s_ActiveShields[0]->m_LifeTimeTicks = -1;
}

void CShield::Tick()
{
    
    // Удаляем щит если время вышло
    if(m_LifeTimeTicks != -1 && Server()->Tick() > m_LifeTimeTicks)
    {
        // Удаляем все активные щиты сразу
        for(auto pShield : s_ActiveShields)
        {
            GameWorld()->RemoveEntity(pShield);
            delete pShield;
        }
        s_ActiveShields.clear();
        s_ShieldsLifeEndTick = -1;
        return;
    }


    // Телепорт игроков между двумя щитами
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        CPlayer *pPlayer = GameServer()->m_apPlayers[i];
        if(!pPlayer) continue;

        CCharacter *pChr = pPlayer->GetCharacter();
        if(!pChr) continue;

        if(distance(pChr->m_Pos, m_Pos) < 32.0f && pChr->CanTeleport(Server()->Tick(), Server()->TickSpeed() * 1)) // 3 секунды таймаут
        {
            dbg_msg("shield", "Player %d teleporting", pPlayer->GetCid());
        
            for(CShield *pOtherShield : s_ActiveShields)
            {
                if(pOtherShield != this)
                {
                    pChr->m_Pos = pOtherShield->m_Pos + vec2(0, -32);
                    CCharacterCore *pCore = pChr->Core();
                    pCore->m_Pos = pChr->m_Pos;
                    pCore->m_Vel = vec2(0, 0);
                    pChr->MarkTeleported(Server()->Tick());

                    GameServer()->CreateSound(pChr->m_Pos, SOUND_PLAYER_SPAWN);
                    
                    break;
                }
            }
        }
    }
       
/*  // Периодичность импульса (каждые 1 секунду)
    const int PulseInterval = Server()->TickSpeed() * 1;

    // Проверяем, когда последний раз был импульс
    static int LastPulseTick = 0;

    if(Server()->Tick() - LastPulseTick >= PulseInterval)
    {
        LastPulseTick = Server()->Tick();

        // Проходим по всем игрокам
        for(int i = 0; i < MAX_CLIENTS; i++)
        {
            CPlayer *pPlayer = GameServer()->m_apPlayers[i];
            if(!pPlayer)
                continue;

            CCharacter *pChr = pPlayer->GetCharacter();
            if(!pChr)
                continue;

            vec2 dir = pChr->m_Pos - m_Pos;
            float dist = length(dir);

            // Радиус действия импульса
            const float Radius = 128.0f;

            if(dist < Radius && dist > 0.01f)
            {
                vec2 forceDir = normalize(dir);

                // Сила отталкивания уменьшается с расстоянием
                float strength = (Radius - dist) * 0.5f;

                // Применяем импульс к скорости игрока
                pChr->AddVelocity(forceDir * strength);
            }
        }

        // Можно добавить тут визуальный и звуковой эффект
        // Например, вызвать взрыв или генерацию частиц вокруг щита
    } */
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
    if(Server()->Tick() % (Server()->TickSpeed() * 1) == 0)
    {
        CNetEvent_Explosion *pExplosion = static_cast<CNetEvent_Explosion *>(Server()->SnapNewItem(NETEVENTTYPE_EXPLOSION, GetId(), sizeof(CNetEvent_Explosion)));
        if(pExplosion)
        {
            pExplosion->m_X = (int)m_Pos.x;
            pExplosion->m_Y = (int)m_Pos.y;
        }
    }
}