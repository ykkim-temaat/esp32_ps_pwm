# ESP32 Phase-Shift PWM 기능 추가 계획 및 설계 문서

## 1. 현재 PS-PWM 구조 및 안정성 검토
* **파형 전환 안정성 (Shadow Register):** `pspwm_set_frequency()` 및 `pspwm_set_ps_duty()` 호출 시 값이 섀도우 레지스터에 임시 저장되었다가 주기가 끝나는(Timer 0 리셋) 시점에 일괄 업데이트되므로 파형이 깨지지 않음.
* **상하단 스위치 동시 도통 (Shoot-through) 방지:** 하드웨어 데드타임 생성기(DTG)가 최종 출력 직전 에지(Edge) 기반의 턴온 지연을 강제하므로, 소프트웨어적으로 주파수나 듀티를 실시간으로 변동하더라도 상하단 암쇼트(Arm-short)는 원천 차단됨.

## 2. 추가 기능 1: Soft Start (점진적 듀티 변경)
**목표:** 듀티 비율이 0%에서 목표 듀티까지 순차적으로 부드럽게 상승(Ramp-up)하거나 하강(Ramp-down)하게 하여 돌입 전류(Inrush Current)를 방지.

**구현 계획:**
1. **제어 루프 구성:** FreeRTOS Task(또는 `esp_timer`)를 활용하여 메인 로직과 독립적으로 동작하는 백그라운드 Soft Start 처리 태스크를 구성.
2. **소프트 스타트 래퍼(Wrapper) 함수 구현:** 
   * `pspwm_set_duty_soft(target_duty, step_size, delay_ms)` 구조 적용.
   * 목표 듀티와 현재 듀티를 비교하여 지정된 지연 시간(`delay_ms`)마다 지정된 변화량(`step_size`, 예: 1%)씩 듀티를 가감.
   * 내부적으로 `pspwm_set_ps_duty()`를 반복 호출 (섀도우 레지스터 덕분에 파형 글리치 없이 부드러운 전환 보장).

## 3. 추가 기능 2: 공진 주파수 추적 (Resonant Frequency Tracking)
**목표:** 풀브릿지 공진 탱크의 전류 Zero-Crossing(ZC) 펄스를 입력받아 LEAD Leg 출력과의 위상차(Phase delay)를 측정하고, 이를 바탕으로 최적의 공진 주파수를 실시간 추적 및 변경.

**구현 계획:**
1. **ESP32 MCPWM Capture (CAP) 모듈 적용:**
   * 고주파 대역에서 소프트웨어 인터럽트의 처리 지연 오차를 없애기 위해, 하드웨어 타이머 값을 즉시 기록하는 **MCPWM Capture 기능**을 사용.
   * 전용 GPIO 핀을 ZC 펄스 입력으로 할당. 해당 핀에 에지(Edge) 발생 시, 현재 구동 중인 LEAD Leg 타이머의 카운터 값을 자동으로 하드웨어 캡처(저장).
2. **위상차(Phase Delay) 계산:**
   * LEAD Leg 타이머가 0에서 카운트를 시작하므로, 캡처된 타이머(Tick) 값 자체가 LEAD Leg 출력 시점과 공진 전류 ZC 펄스 간의 위상차 시간이 됨.
3. **주파수 추적 제어 루프 (Frequency Tracking Loop):**
   * 캡처된 위상차 값을 피드백(Feedback)으로 읽어와 목표 공진 상태(약간의 유도성 영역, ZVS 보장)와 비교.
   * PI 제어기나 미세 스텝 제어를 통해 `pspwm_set_frequency()`를 호출하여 공진점을 능동적으로 추적(Tracking).

## 4. 향후 작업 순서 (Action Items)
1. **[진행 대기]** 본 문서의 컨셉 검토 및 확정.
2. `ps_pwm` 컴포넌트 (혹은 `main` 어플리케이션) 내 FreeRTOS 기반 Soft Start 함수 및 태스크 구현.
3. `mcpwm_prelude`의 Capture 기능 관련 초기화 로직 구현 및 GPIO 핀 할당.
4. ZC 펄스 캡처 콜백 로직 및 위상차 계산 코드를 포함한 주파수 제어 루프 구현.
