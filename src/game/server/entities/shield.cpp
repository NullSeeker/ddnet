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
    for(int i = 0; i < 14; i++)
        m_ProjectileIDs[i] = Server()->SnapNewId();

    s_ActiveShields.push_back(this);

    if(s_ActiveShields.size() == 2)
    {
        // Устанавливаем таймер для обоих щитов
        s_ShieldsLifeEndTick = Server()->Tick() + Server()->TickSpeed() * 10; // 10 сек
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
    for(int i = 0; i < 14; i++)
        Server()->SnapFreeId(m_ProjectileIDs[i]);

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
        // Взрыв гранатомёта на позициях всех щитов перед удалением
        for(auto pShield : s_ActiveShields)
        {
            GameServer()->CreateSound(pShield->m_Pos, SOUND_GRENADE_EXPLODE);
    
        }
    
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

        if(distance(pChr->m_Pos, m_Pos) < 32.0f && pChr->CanTeleport(Server()->Tick(), Server()->TickSpeed() * 1)) // 1 секунда задержка воизбежании багов
        {
            dbg_msg("shield", "Player %d teleporting", pPlayer->GetCid());
        
            for(CShield *pOtherShield : s_ActiveShields)
            {
                if(pOtherShield != this)
                {
                    pChr->m_Pos = pOtherShield->m_Pos + vec2(0, -32);
                    CCharacterCore *pCore = pChr->Core();
                    pCore->m_Pos = pChr->m_Pos;
                    //pCore->m_Vel = vec2(0, 0); сброс скорости игрока на 0 
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

    // Центральный объект — щит
    CNetObj_Pickup *pPickup = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetId(), sizeof(CNetObj_Pickup)));
    if(!pPickup)
        return;

    pPickup->m_X = round_to_int(m_Pos.x);
    pPickup->m_Y = round_to_int(m_Pos.y);
    pPickup->m_Type = POWERUP_ARMOR;

    // Параметры анимации
    int tick = Server()->Tick();
    float time = tick / 60.0f; // Время в секундах

    // --- Пульсирующий круг ---
    float pulseRadius = 40.0f + 10.0f * sinf(time * 3.0f); // Радиус пульсирует от 30 до 50

    // Отрисовка пульсирующего круга через лазеры (4 линии)
    for(int i = 0; i < 4; i++)
    {
        float angle1 = 2.0f * pi * i / 4.0f + time;
        float angle2 = 2.0f * pi * (i+1) / 4.0f + time;

        vec2 from = m_Pos + vec2(cosf(angle1), sinf(angle1)) * pulseRadius;
        vec2 to   = m_Pos + vec2(cosf(angle2), sinf(angle2)) * pulseRadius;

        // Используем SnapNewId() для частиц или лазеров
        int id = m_LaserIDs[i];

        if(CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, id, sizeof(CNetObj_Laser))))
        {
            pLaser->m_X = (int)to.x;
            pLaser->m_Y = (int)to.y;
            pLaser->m_FromX = (int)from.x;
            pLaser->m_FromY = (int)from.y;
            pLaser->m_StartTick = tick;
            // Прозрачность лазеров задается на клиенте, на сервере нельзя, но визуал можно варьировать через позицию и частоту обновления
        }
    }

    int idIndex = 0;
    if(idIndex >= 14)
        return;

    // --- Вращающиеся частицы по кругу ---
    int particleCount = 8;
    for(int i = 0; i < particleCount; i++)
    {
        if(idIndex < 14)
        {
            float angle = 2.0f * pi * i / particleCount + time * 2.0f;
            float radius = 30.0f;

            vec2 particlePos = m_Pos + vec2(cosf(angle), sinf(angle)) * radius;

            CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(
                NETOBJTYPE_PROJECTILE, m_ProjectileIDs[idIndex], sizeof(CNetObj_Projectile)));
            if(pProj)
            {
                pProj->m_X = (int)particlePos.x;
                pProj->m_Y = (int)particlePos.y;

                // Двигаются по касательной с небольшой скоростью
                vec2 vel = vec2(-sinf(angle), cosf(angle)) * 0.1f;
                pProj->m_VelX = (int)(vel.x * 100);
                pProj->m_VelY = (int)(vel.y * 100);

                pProj->m_Type = WEAPON_GRENADE; // выбери яркий тип
                pProj->m_StartTick = tick;
            }
            idIndex++;
        }
    }

    // --- Энергетический вихрь в центре ---
    // Несколько маленьких частиц, меняющих радиус и позицию
    int swirlParticles = 6;
    for(int i = 0; i < swirlParticles; i++)
    {
        if(idIndex < 14)
        {
            float angle = 2.0f * pi * i / swirlParticles - time * 3.0f;
            float swirlRadius = 8.0f + 4.0f * sinf(time * 5.0f + i);

            vec2 swirlPos = m_Pos + vec2(cosf(angle), sinf(angle)) * swirlRadius;

            CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(
                NETOBJTYPE_PROJECTILE, m_ProjectileIDs[idIndex], sizeof(CNetObj_Projectile)));
            if(pProj)
            {
                pProj->m_X = (int)swirlPos.x;
                pProj->m_Y = (int)swirlPos.y;
                pProj->m_VelX = 0;
                pProj->m_VelY = 0;
                pProj->m_Type = WEAPON_HAMMER; // другой цвет для разнообразия
                pProj->m_StartTick = tick;
            }
            idIndex++;
        }
    }
}