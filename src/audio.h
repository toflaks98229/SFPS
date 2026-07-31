/**
 * @file audio.h
 * @brief 런타임에 합성된 사운드를 처리합니다.
 *
 * 이 모듈은 샘플된 오디오를 사용하지 않습니다. 사운드는 assets/sounds.txt에 있는
 * 몇 개의 정수 값으로 정의되며, 이 값들은 어떤 오실레이터를 실행하고 피치와 볼륨이
 * 어떻게 변하는지를 명시합니다. 믹서는 재생 중에 파형을 생성합니다. 이는 텍스처와
 * 동일한 트레이드오프입니다: 샷건 발사음은 16비트 PCM으로 약 60KB 대신, 약 30바이트의
 * 레시피로 표현됩니다.
 *
 * 백엔드는 winmm의 waveOut을 사용하며, 이미 링크되어 있어 추가 라이브러리가 필요 없습니다.
 */
#ifndef AUDIO_H
#define AUDIO_H

/**
 * @brief 오디오 장치를 열고 믹싱 스레드를 시작합니다.
 *
 * 사용 가능한 출력 장치가 없으면 0을 반환합니다. 이 경우 다른 모든 호출은
 * 무해한 no-op이 되며, 게임은 시작을 거부하는 대신 조용히 실행됩니다.
 * @return 성공 시 1, 실패 시 0을 반환합니다.
 */
int audio_init(void);

/**
 * @brief 오디오 장치를 닫고 믹싱 스레드를 종료합니다.
 */
void audio_shutdown(void);

/**
 * @brief 지정된 이름의 사운드를 재생합니다.
 *
 * 믹서가 실행 중인 동안 게임 스레드에서 호출해도 안전합니다. 보이스 테이블은
 * 락으로 보호됩니다. 알 수 없는 이름은 무시됩니다.
 * @param name 재생할 사운드의 이름.
 * @param gain 사운드 레시피의 자체 레벨을 조절하는 0-100 사이의 게인 값.
 *             거리 감쇠 등은 레시피가 아닌 호출자에게 달려 있습니다.
 */
void audio_play(const char *name, int gain);

/**
 * @brief 파싱된 캐시를 삭제하여 다음 재생 시 레시피 텍스트를 다시 읽도록 합니다.
 *
 * 핫 리로딩 경로에서 호출됩니다. 텍스트가 변경될 수 없는 릴리스 빌드에서는
 * no-op 비용입니다.
 */
void audio_reload(void);

/**
 * @brief 하나의 사운드를 장치를 건드리지 않고 `out` 버퍼에 렌더링합니다.
 *
 * 믹서와 동일한 신디사이저를 실행하므로, 도구에서 보는 것이 게임에서 재생되는 것과
 * 동일합니다. 별도의 오프라인 복사본은 동기화되지 않을 수 있습니다.
 * 샘플 레이트는 audio_rate() 함수로 얻을 수 있습니다.
 * @param name 렌더링할 사운드의 이름.
 * @param out 렌더링된 오디오 데이터를 저장할 버퍼.
 * @param max_frames 렌더링할 최대 프레임 수.
 * @return 기록된 프레임 수를 반환합니다.
 *
 * @warning Offline/tool use only. This is the one entry point that works with
 *          no device open (tools/sndtest.c never calls audio_init), but it
 *          renders from a recipe pointer held across the render loop. Calling
 *          it concurrently with audio_reload() from another thread is NOT
 *          supported -- the reload could rewrite the recipe mid-render. The
 *          game never does this: it uses audio_play(), which is fully locked.
 * @warning 오프라인/도구 전용입니다. 장치가 열리지 않은 상태에서 동작하는 유일한
 *          진입점이지만(tools/sndtest.c는 audio_init을 호출하지 않습니다), 렌더링
 *          루프 전체에 걸쳐 레시피 포인터를 보유한 채 렌더링합니다. 다른 스레드의
 *          audio_reload()와 동시에 호출하는 것은 지원하지 않습니다. 리로드가 렌더링
 *          도중 레시피를 재작성할 수 있기 때문입니다. 게임은 이런 호출을 하지 않으며,
 *          완전히 락으로 보호되는 audio_play()를 사용합니다.
 */
int audio_render(const char *name, short *out, int max_frames);

/**
 * @brief 오디오 시스템의 샘플 레이트를 반환합니다.
 *
 * @return 초당 샘플 수 (Hz).
 */
int audio_rate(void);

#endif
