# PS-PWM 컴포넌트 API 사용 가이드

본 문서는 `ps_pwm` 컴포넌트를 이용해 위상천이 풀브릿지(Phase-Shift Full-Bridge) 제어 애플리케이션을 구현할 때 필요한 핵심 API의 호출 순서와 사용 방법을 요약합니다.

## 1. 초기화 (Initialization)
가장 먼저 하드웨어 핀 및 기본 타이밍을 설정하기 위해 초기화 함수를 호출해야 합니다.

* **함수:** `pspwm_init_symmetrical()` 또는 `pspwm_init()`
* **사용법:** 사용할 GPIO 핀(LEAD/LAG 다리 각각 2개), 초기 주파수, 초기 위상천이 듀티(0.0~1.0), 데드타임(Dead-time) 등을 설정합니다.
* **출력 대기 설정:** 전원을 켰을 때 출력이 바로 나가지 않게 하려면 초기화 시 매개변수 `output_enabled = false` 로 주거나, 초기화 직후 `pspwm_disable_output()`을 호출하는 것이 좋습니다.

---

## 2. 주파수 및 듀티 설정 (출력 ON 전/후 동일)
주파수나 듀티값은 출력의 활성화 여부(ON/OFF)와 무관하게 언제든 설정이 가능합니다. 출력 상태가 ON일 때 설정하면 즉시 실시간으로 파형이 변경됩니다 (내부 섀도우 레지스터를 통해 파형 깨짐 없이 반영됨).

### 2.1. 주파수(Frequency) 변경
* **함수:** `pspwm_set_frequency(mcpwm_unit_t mcpwm_num, float frequency)`
* **예제:** 100kHz로 설정
  ```c
  pspwm_set_frequency(MCPWM_UNIT_0, 100e3f);
  ```

### 2.2. 위상천이 듀티(Phase-Shift Duty) 즉시 변경
위상천이 값을 0.0 (0%) 에서 1.0 (180도, 최대 출력) 사이로 설정합니다.
* **함수:** `pspwm_set_ps_duty(mcpwm_unit_t mcpwm_num, float ps_duty)`
* **예제:** 45% 위상천이 설정
  ```c
  pspwm_set_ps_duty(MCPWM_UNIT_0, 0.45f);
  ```

### 2.3. 듀티 소프트 스타트 (점진적 변경)
돌입 전류(Inrush Current)를 방지하기 위해 듀티 값을 서서히 목표값으로 변화시킵니다.
* **함수:** `pspwm_set_duty_soft(mcpwm_unit_t mcpwm_num, float target_duty, uint32_t duration_ms)`
* **예제:** 10초(10000ms) 동안 듀티를 100% (1.0f)로 서서히 올리기
  ```c
  pspwm_set_duty_soft(MCPWM_UNIT_0, 1.0f, 10000);
  ```

---

## 3. 출력 ON / OFF 제어

### 3.1. 출력 활성화 (Turn ON)
PWM 출력을 시작(Enable)합니다. 초기 위상 세트포인트와 동기화하기 위해 리싱크(Resync)와 함께 출력을 활성화합니다.
* **주의:** 듀티값이 0.0보다 크다면, 이 함수가 호출되는 즉시 실제 전력이 전달되기 시작합니다. 안전을 위해 ON을 하기 직전에 주파수와 듀티를 0.0으로 맞춰두고 ON 한 뒤 `pspwm_set_duty_soft()`로 올리는 방식을 권장합니다.
* **함수:** `pspwm_resync_enable_output(mcpwm_unit_t mcpwm_num)`
* **에러 해제 후 출력:** 하드웨어 폴트(과전류 핀 트리거 등)가 발생했었다면 락(Latch)이 걸려 출력되지 않습니다. 이 경우 아래 함수로 폴트를 명시적으로 클리어한 뒤 ON 해야 합니다.
  ```c
  pspwm_clear_hw_fault_shutdown_occurred(MCPWM_UNIT_0);
  pspwm_resync_enable_output(MCPWM_UNIT_0);
  ```

### 3.2. 출력 비활성화 (Turn OFF)

상황에 따라 '정상 정지(Normal Stop)'와 '비상 정지(Emergency Stop)' 두 가지 방식으로 출력을 차단하는 것이 전력 전자 제어의 정석입니다.

#### 1) 정상적인 정지 (Normal Stop)
인덕터 등에 남아있는 에너지를 안전하게 해소하고 과도 응답(Transient)을 방지하기 위해, 듀티를 0%로 줄인 뒤에 출력을 끄는 방식입니다.
```c
// 1. 듀티를 0%로 즉시(또는 Soft Start로 서서히) 변경
pspwm_set_ps_duty(MCPWM_UNIT_0, 0.0f); 

// (옵션) 듀티가 0이 적용될 때까지 1~2 주기 정도 짧게 대기
vTaskDelay(pdMS_TO_TICKS(1)); 

// 2. 출력을 완전히 차단 (소프트웨어 Trip)
pspwm_disable_output(MCPWM_UNIT_0);

// (옵션) 혹시 실행 중일 수 있는 Soft Start 태스크 리셋
pspwm_set_duty_soft(MCPWM_UNIT_0, 0.0f, 0);
```

#### 2) 비상 정지 (Emergency Stop / Fault)
시스템 폴트나 비상 상황 발생 시에는 딜레이 없이 하드웨어 트립(Trip-zone)을 발생시켜 즉각적으로 모든 스위치를 안전한 상태(주로 LOW)로 끕니다.
```c
pspwm_disable_output(MCPWM_UNIT_0);
pspwm_set_duty_soft(MCPWM_UNIT_0, 0.0f, 0); // 동작 중이던 Soft Start 취소
```

---

## 4. 공진 주파수 시뮬레이션 및 터미널 제어 테스트 (HIL)
오존셀 구동 등 공진형 풀브릿지 인버터의 위상 지연(Phase Delay)을 시뮬레이션하고, 속도형 PI 제어(Incremental PI)와 데드밴드(Dead-band)를 통한 완벽한 주파수 트래킹(PLL Lock) 기능을 테스트할 수 있습니다.

### 4.1. 터미널 명령어 목록
* `on` : PWM 출력 시작 (지정된 Soft Start 시간에 맞춰 듀티 점진 상승)
* `off` : PWM 출력 차단
* `freq [값]` : 주파수 수동 설정 (예: `freq 20000`)
* `duty [값]` : 목표 듀티 설정 (예: `duty 0.75`)
* `soft [ms]` : Soft Start 상승 시간 설정 (예: `soft 2000`)
* `delay [us]` : 공진 탱크의 전류 ZC 딜레이 시뮬레이션 값 수동 주입 (예: `delay 2`)
* `track on` / `track off` : 자동 주파수 트래킹(PI 제어기) 활성화/비활성화
* `zvs [us]` : 목표(Target) ZVS 딜레이 마진 설정 (예: `zvs 5.0`)
* `kp [값]` / `ki [값]` : PI 제어기의 비례(Kp) 및 적분(Ki) 게인 실시간 설정 (빠른 추적을 위해 `ki 2000` 등 높은 값 권장)

### 4.2. 트래킹 테스트(PLL Lock) 시나리오
1. **하드웨어 연결:** `GPIO 4` (LEAD 출력)와 `GPIO 11` (LEAD 캡처)를 점퍼선으로 연결하고, `GPIO 10` (가상 ZC 펄스 출력)과 `GPIO 9` (ZC 캡처)를 연결합니다.
2. 터미널에서 `on`을 입력하여 PWM을 20kHz로 활성화합니다.
3. `track on`을 입력하여 트래킹 제어 루프를 시작합니다.
4. `delay [값]` 명령어를 통해 딜레이를 주입하며 가상의 공진 탱크 물리적 변화를 시뮬레이션합니다.
5. 터미널 로그(`[Auto-Track]`)를 통해 측정된 딜레이(Meas Delay)가 목표치(Target, 예: 5.0µs)와의 오차 범위(데드밴드 ±0.1µs) 내에 들어오는 순간 주파수 증감이 정지하며(Lock) 완벽하게 추적되는 것을 확인합니다.
