FROM ubuntu:22.04 AS dependencies

ARG DEBIAN_FRONTEND=noninteractive
ARG WORKFLOW_COMMIT=6bd5f0157304cae04da8a6bb2574041a4ff37d15
ARG WFREST_COMMIT=16aee9067b8a8c92c5057d3b9bc3b14222782e6a
ARG PROTOBUF_VERSION=3.20.1
ARG SRPC_VERSION=0.10.2

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake git liblz4-dev libsnappy-dev libssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/sogou/workflow.git /tmp/workflow \
    && git -C /tmp/workflow checkout "$WORKFLOW_COMMIT" \
    && cmake -S /tmp/workflow -B /tmp/workflow/build \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/workflow/build --parallel 2 \
    && cmake --install /tmp/workflow/build \
    && rm -rf /tmp/workflow

RUN git clone https://github.com/wfrest/wfrest.git /tmp/wfrest \
    && git -C /tmp/wfrest checkout "$WFREST_COMMIT" \
    && cmake -S /tmp/wfrest -B /tmp/wfrest/build \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/wfrest/build --parallel 2 \
    && cmake --install /tmp/wfrest/build \
    && rm -rf /tmp/wfrest

RUN git clone --branch "v${PROTOBUF_VERSION}" --depth 1 \
        https://github.com/protocolbuffers/protobuf.git /tmp/protobuf \
    && cmake -S /tmp/protobuf/cmake -B /tmp/protobuf/build \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=ON \
    && cmake --build /tmp/protobuf/build --parallel 2 \
    && cmake --install /tmp/protobuf/build \
    && rm -rf /tmp/protobuf

RUN git clone --branch "v${SRPC_VERSION}" --depth 1 \
        https://github.com/sogou/srpc.git /tmp/srpc \
    && cmake -S /tmp/srpc -B /tmp/srpc/build \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/srpc/build --parallel 2 \
    && cmake --install /tmp/srpc/build \
    && rm -rf /tmp/srpc

FROM dependencies AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libfmt-dev nlohmann-json3-dev libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

COPY 02_SMT_LogTrace /src/02_SMT_LogTrace
RUN cmake -S /src/02_SMT_LogTrace -B /src/02_SMT_LogTrace/build-container \
        -DCMAKE_BUILD_TYPE=Release -DLOGTRACE_BUILD_TESTS=OFF \
    && cmake --build /src/02_SMT_LogTrace/build-container --parallel 2

FROM ubuntu:22.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates curl libfmt8 liblz4-1 libsnappy1v5 libspdlog1 libssl3 zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10000 smt-archive \
    && groupadd --gid 10002 smt-logtrace \
    && useradd --uid 10002 --gid 10002 --groups 10000 --no-create-home \
        --shell /usr/sbin/nologin smt-logtrace \
    && mkdir -p /index /var/log/smt-logtrace \
    && chown -R 10002:10002 /index /var/log/smt-logtrace

COPY --from=dependencies /usr/local/lib/libworkflow.so* /usr/local/lib/
COPY --from=dependencies /usr/local/lib/libprotobuf.so* /usr/local/lib/
COPY --from=dependencies /usr/local/lib/libsrpc.so* /usr/local/lib/
COPY --from=build /src/02_SMT_LogTrace/build-container/logsearch_server /usr/local/bin/logsearch_server
COPY --from=build /src/02_SMT_LogTrace/build-container/logtrace_gateway /usr/local/bin/logtrace_gateway
COPY --from=build /src/02_SMT_LogTrace/build-container/logtrace_admin /usr/local/bin/logtrace_admin
COPY deploy-docker/common/load-secrets.sh /usr/local/bin/load-secrets.sh
COPY deploy-docker/config/logtrace.container.json /etc/smt/logtrace.json
RUN chmod 0555 /usr/local/bin/logsearch_server /usr/local/bin/logtrace_gateway \
        /usr/local/bin/logtrace_admin /usr/local/bin/load-secrets.sh \
    && chmod 0444 /etc/smt/logtrace.json \
    && ldconfig

USER 10002:10002
WORKDIR /index
ENTRYPOINT ["/usr/local/bin/load-secrets.sh"]
