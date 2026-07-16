# ESP32 Phase-Shift PWM Examples

이 디렉토리에는 `ps_pwm` 라이브러리를 활용하여 개발할 수 있는 두 가지 주요 예제 프로젝트가 포함되어 있습니다. 각 예제 폴더 내의 `README.md`에서 자세한 API 가이드와 동작 원리를 확인할 수 있습니다.

---

## 1. ps_pwm_basic
**기본 Phase-Shift PWM 출력 및 상태 제어 예제**

가장 기초적인 PS-PWM 신호 출력 방법을 다루는 예제입니다.

* **주요 기능**:
  * `pspwm_init_symmetrical()`을 이용한 초기화 및 데드타임 설정
  * 하드웨어 Fault 감지(GPIO) 및 래칭 시스템을 통한 안전한 차단 및 복구
  * 소프트 Fault를 이용한 출력 On/Off 토글 기능
  * Soft-Start(`pspwm_set_duty_soft()`)를 통한 부드러운 Phase-Shift Duty 전환
  * 런타임 중 주파수 동적 변경 (`pspwm_set_frequency()`)

👉 **세부 가이드**: [ps_pwm_basic/README.md](ps_pwm_basic/README.md)

---

## 2. zvs_resonant_tracking
**ZVS 공진 주파수 자동 트래킹(PI Control) 예제**

LLC 공진형 컨버터 등에서 영전압 스위칭(ZVS, Zero Voltage Switching)을 유지하기 위해, 출력 주파수를 공진 탱크의 특성에 맞춰 자동으로 추종하는 고급 예제입니다.

* **주요 기능**:
  * `pspwm_enable_tracking_capture()`를 이용한 스위칭 시작점(Start)과 영점 교차점(ZC) 사이의 지연 시간(Delay) 측정
  * 측정된 지연 시간과 목표 지연 시간의 오차를 기반으로 하는 **속도형 PI 제어 (Incremental PI Control)** 루프
  * 시스템 진동(발진)을 방지하기 위한 **PLL Lock (Dead-band)** 적용
  * 안전을 위한 주파수 Clamping (Min/Max 제한) 및 적분 누적(Wind-up) 방지
  * 메인 제어, 트래킹, 캡처 모니터링, 터미널 입력을 분리한 **멀티 태스크 아키텍처** 구현

👉 **세부 가이드**: [zvs_resonant_tracking/README.md](zvs_resonant_tracking/README.md)
