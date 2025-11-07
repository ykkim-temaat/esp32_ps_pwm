# Espressif 공식 Docker 이미지를 기반으로 합니다.
ARG DOCKER_TAG=latest
FROM espressif/idf:${DOCKER_TAG}

# 필요한 경우 여기에 추가적인 리눅스 패키지를 설치할 수 있습니다.
# (예: RUN apt-get update && apt-get install -y vim)

# 터미널이 열릴 때 ESP-IDF 환경 변수가 자동으로 로드되도록 설정
RUN echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> ~/.bashrc

# 컨테이너 시작 시 실행될 기본 스크립트 설정 (ESP-IDF 베이스 이미지에 포함됨)
ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]
CMD ["/bin/bash", "-c"]