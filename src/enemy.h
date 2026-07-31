/**
 * @file enemy.h
 * @brief 스프라이트 기반 몬스터의 AI 및 충돌을 관리합니다.
 *
 * Doom 스타일: 월드는 3D이지만 몬스터는 항상 카메라를 향하는 평면 빌보드입니다.
 * 이 빌보드는 절차적으로 생성된 스프라이트 시트(sprite.c 참조)에서 그려집니다.
 * 이는 수백 킬로바이트 예산으로 얻을 수 있는 결과물입니다. 다각형 몬스터는
 * 모델 데이터에 훨씬 더 많은 비용이 들 것입니다.
 *
 * AI와 충돌 로직은 GL과 관련 없이 여기에 있으며, 윈도우 없이도 몬스터를
 * 테스트할 수 있습니다. tools/enemytest.c는 몬스터를 바닥에 놓고 플레이어를
 * 향해 걷는지, 공격하기 위해 멈추는지, 총에 맞으면 죽는지 등을 확인합니다.
 * 추격 버그는 게임 내에서 보이지 않으므로, 이동 버그와 동일한 방식으로 분리하는
 * 것이 가치가 있습니다.
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "level.h"

/* --- 매크로 및 상수 --- */
#define ENEMY_MAX       64      ///< @brief 레벨 당 최대 몬스터 수.
#define ENEMY_MAX_SHOTS  48     ///< @brief 동시에 활성화될 수 있는 최대 몬스터 발사체 수.
#define SHOT_RADIUS      0.22f  ///< @brief 발사체의 충돌 반경 (미터).

/* --- 열거형 --- */

/**
 * @enum MonTypeID
 * @brief 몬스터 종류를 식별합니다.
 *
 * 이 인덱스는 스프라이트 아틀라스(sprite.h 참조)에서 생물체의 행과도
 * 일치하므로 두 열거형은 동기화되어야 합니다.
 */
enum MonTypeID {
    MON_IMP,        /**< 임프 */
    MON_BRUTE,      /**< 브루트 */
    MON_HOUND,      /**< 하운드 */
    MON_CASTER,     /**< 캐스터 */
    MON_TYPES       /**< 몬스터 종류의 총 수 */
};

/**
 * @enum EState
 * @brief 몬스터의 AI 상태를 정의합니다.
 */
typedef enum {
    E_IDLE,    /**< 플레이어를 보지 못한 상태 */
    E_CHASE,   /**< 플레이어를 향해 걷는 중 */
    E_ATTACK,  /**< 공격 준비 또는 재사용 대기 중 */
    E_HURT,    /**< 피격 후 잠시 경직 상태 */
    E_DEAD     /**< 시체: 그려지지만 충돌 및 히트스캔 없음 */
} EState;

/* --- 구조체 --- */

/**
 * @struct MonType
 * @brief 한 종류의 몬스터를 다른 몬스터와 다르게 만드는 모든 특성을 정의합니다.
 *
 * 새로운 타입을 추가하는 것은 이 테이블에 한 행을 추가하고 sprite.c에 바디를
 * 추가하는 것으로 충분하며, 새로운 코드 경로는 필요하지 않습니다.
 */
typedef struct {
    int   hp;           /**< 체력. */
    float speed;        /**< 이동 속도 (m/s). */
    float radius;       /**< 충돌 반경 (미터). */
    float height;       /**< 신장 (미터). */
    float eye;          /**< 발 위의 시선 높이 (미터). */
    float sight;        /**< 플레이어를 처음 감지할 수 있는 거리 (미터). */
    float attack;       /**< 근접 공격 거리 또는 원거리 공격 사정거리 (미터). */
    int   damage;       /**< 공격 당 피해량. */
    float windup;       /**< 공격 전 준비 시간 (초). */
    float cooldown;     /**< 공격 후 재사용 대기 시간 (초). */
    float aspect;       /**< 스프라이트의 가로/세로 비율. */
    float shot_speed;   /**< 0이 아니면 근접 공격 대신 이 속도(m/s)로 발사체를 발사합니다. */
} MonType;

/**
 * @struct Enemy
 * @brief 레벨에 있는 단일 몬스터의 상태입니다.
 */
typedef struct {
    int    type;        /**< 몬스터 종류 (MonTypeID). */
    v3     pos;         /**< 발 위치 (월드 좌표). */
    float  vel_y;       /**< 수직 속도. */
    float  yaw;         /**< 현재 마주보는 방향 (라디안). */
    int    health;      /**< 현재 체력. */
    EState state;       /**< 현재 AI 상태. */
    float  timer;       /**< 상태 내에서 사용하는 타이머 (예: 공격 준비, 재사용 대기). */
    float  anim;        /**< 걷기 사이클을 위한 자유 실행 시계. */
    float  flash;       /**< 피격 시 흰색 섬광 효과 (0으로 감소). */
    int    swung;       /**< 현재 공격의 피해가 이미 적용되었는지 여부. */
    int    active;      /**< 이 슬롯이 사용 중인지 여부. */
} Enemy;

/**
 * @struct Shot
 * @brief 몬스터가 발사한 비행 중인 발사체입니다.
 *
 * weapon.c가 자신의 트레이서와 임팩트를 소유하는 것처럼, 이 구조체는
 * enemy 모듈에 의해 소유됩니다. 이들은 몬스터가 발사했기 때문에 존재하며,
 * 레벨과 함께 사라집니다.
 */
typedef struct {
    v3    pos;      /**< 현재 위치. */
    v3    vel;      /**< 속도 벡터. */
    float life;     /**< 발사체가 사라지기까지 남은 시간 (초). 0이면 슬롯이 비어있음. */
    int   damage;   /**< 피해량. */
    int   active;   /**< 이 슬롯이 활성 상태인지 여부. */
} Shot;

/* --- 함수 --- */

/**
 * @brief 몬스터 타입 인덱스로 몬스터의 기본 스탯을 가져옵니다.
 * @param type 몬스터 타입 ID (MON_*).
 * @return MonType 구조체에 대한 const 포인터.
 */
const MonType *mon_stats(int type);

/**
 * @brief 엔티티 종류 문자열에 해당하는 몬스터 타입을 반환합니다.
 * @param kind 엔티티 종류 문자열 (예: "imp", "brute").
 * @return 해당 몬스터 타입 ID, 없으면 -1.
 */
int mon_type_for(const char *kind);

/**
 * @brief 현재 활성화된 발사체의 수를 반환합니다 (비활성 슬롯 포함).
 * @return 스캔할 총 발사체 슬롯 수.
 */
int enemy_shot_count(void);

/**
 * @brief 지정된 인덱스의 발사체 정보를 가져옵니다.
 * @param i 발사체 인덱스.
 * @return Shot 구조체에 대한 const 포인터, 유효하지 않은 인덱스 시 NULL.
 */
const Shot *enemy_shot_at(int i);

/**
 * @brief 모든 몬스터와 발사체를 초기화합니다. (재)스폰 전에 호출해야 합니다.
 */
void enemy_reset(void);

/**
 * @brief 레벨의 "spawn" 종류 엔티티 위치에 몬스터를 스폰합니다.
 * @param l 몬스터를 스폰할 레벨 데이터.
 */
void enemy_spawn_level(const Level *l);

/**
 * @brief 현재 레벨의 총 몬스터 수를 반환합니다 (시체 포함).
 * @return 몬스터 수.
 */
int enemy_count(void);

/**
 * @brief 살아있는 몬스터의 수를 반환합니다 (E_DEAD 상태 제외).
 * @return 살아있는 몬스터 수.
 */
int enemy_alive(void);

/**
 * @brief 지정된 인덱스의 몬스터 정보를 가져옵니다.
 * @param i 몬스터 인덱스.
 * @return Enemy 구조체에 대한 const 포인터, 유효하지 않은 인덱스 시 NULL.
 */
const Enemy *enemy_at(int i);

/**
 * @brief 모든 몬스터와 발사체의 상태를 한 프레임 업데이트합니다.
 *
 * 이 프레임 동안 플레이어에게 가해진 총 피해량(근접 공격 및 발사체 피격 모두)을
 * 반환합니다. 이 모듈이 플레이어의 체력에 직접 접근하는 대신, 호출자가
 * 플레이어 체력을 관리하도록 합니다.
 * @param l 현재 레벨 데이터.
 * @param player_eye 플레이어의 시점 위치.
 * @param dt 마지막 프레임 이후 경과 시간 (초).
 * @return 플레이어가 입은 총 피해량.
 */
int enemy_update(const Level *l, v3 player_eye, float dt);

/**
 * @brief 광선이 부딪히는 가장 가까운 살아있는 몬스터를 찾습니다 (수직 실린더로 판정).
 *
 * 샷건에서 사용되어 탄환이 몬스터를 통과하지 않고 멈추도록 합니다.
 * @param o 광선의 원점.
 * @param d 광선의 방향 벡터.
 * @param maxdist 최대 탐지 거리.
 * @param out_t [out] 광선이 몬스터에 부딪힌 거리를 저장할 포인터.
 * @param out_idx [out] 부딪힌 몬스터의 인덱스를 저장할 포인터.
 * @return `maxdist` 내에서 몬스터를 맞추면 1, 그렇지 않으면 0.
 */
int enemy_hitscan(v3 o, v3 d, float maxdist, float *out_t, int *out_idx);

/**
 * @brief 몬스터에게 피해를 입히고, 경직시키거나 죽입니다.
 * @param idx 피해를 입힐 몬스터의 인덱스.
 * @param dmg 피해량.
 * @param dir 사격 방향, 향후 넉백 및 사망 방향에 사용.
 */
void enemy_hurt(int idx, int dmg, v3 dir);

#endif
