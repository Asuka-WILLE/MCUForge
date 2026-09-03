# MCUForge policy-safe Worker 镜像
# 基于 HiClaw 官方 hiclaw-worker 打一层"策略安全"补丁：
#   - .codex/tmp 缓存不进 Worker<->MinIO 镜像（每次调用会重新生成，可能数 GiB）；
#   - 将 channel-policy.json（群聊/私聊白名单、streaming=off）固化为每次
#     openclaw.json 配置合并的不变式，防止远端重写覆盖白名单。
# 构建：运行同目录 Build-MCUForgeWorkerImage.ps1（默认即可），或：
#   docker build --build-arg BASE_IMAGE=<hiclaw-worker 镜像> -t local/mcuforge-hiclaw-worker:policy-safe-20260902-v2 .
ARG BASE_IMAGE=higress-registry.cn-hangzhou.cr.aliyuncs.com/higress/hiclaw-worker:latest
FROM ${BASE_IMAGE}

# Keep Codex command-argument staging out of the Worker<->MinIO mirror.
# These files are regenerated for each invocation and can grow to many GiB.
COPY patch_worker_entrypoint.py /usr/local/sbin/patch_worker_entrypoint.py
RUN python3 /usr/local/sbin/patch_worker_entrypoint.py \
    && rm /usr/local/sbin/patch_worker_entrypoint.py
