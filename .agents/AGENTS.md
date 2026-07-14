# Antigravity Workspace Rules

- `idf.py build` 명령어는 사용자 별도 승인 없이 실행을 제안 및 수행할 수 있습니다.
- 코드를 수정한 후에는 반드시 빌드(성공 여부 검증)를 수행하고 성공한 상태의 결과를 확인하여 사용자에게 보고해야 합니다.
- 비대화형 쉘 환경에서 빌드를 수행할 때는 사용자의 알리아스 설정을 활성화하기 위해 OS와 쉘 종류에 맞는 설정 파일을 소싱하고 `get_idf` 명령어를 실행한 후에 `idf.py build`를 수행합니다.
  - Linux (Bash): `~/.bashrc`를 소싱하고 `get_idf` 실행 (예: `bash -i -c "get_idf && idf.py build"`)
  - macOS (Zsh): `~/.zshrc`를 소싱하고 `get_idf` 실행 (예: `zsh -i -c "get_idf && idf.py build"`)
- 버전을 릴리즈/업데이트 할 때는 반드시 엔트리 소스 파일(`main.c`, `simulation_test.c`, `mcpwm_phase_shift_pwm_example.c` 등)의 최상단 주석(Header) 영역에 날짜, 작성자, 버전 번호, 업데이트 내역을 한 줄로 기록해야 합니다.
- 이 주석에 기록되는 버전 업데이트 내역은 Git 커밋(Commit) 메시지의 내용과 완벽하게 동일해야 합니다.
