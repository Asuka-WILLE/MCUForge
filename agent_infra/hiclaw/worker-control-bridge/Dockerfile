ARG BASE_IMAGE=python:3.12-alpine
FROM ${BASE_IMAGE}

WORKDIR /app
COPY worker_control_bridge.py /app/worker_control_bridge.py

ENV WORKER_CONTROL_HOST=0.0.0.0 \
    WORKER_CONTROL_PORT=18765 \
    DOCKER_SOCKET=/var/run/docker.sock

ENTRYPOINT ["python", "/app/worker_control_bridge.py"]
