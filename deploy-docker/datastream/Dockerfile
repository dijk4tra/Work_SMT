FROM ubuntu:22.04 AS dependencies

ARG DEBIAN_FRONTEND=noninteractive
ARG WORKFLOW_COMMIT=6bd5f0157304cae04da8a6bb2574041a4ff37d15
ARG WFREST_COMMIT=16aee9067b8a8c92c5057d3b9bc3b14222782e6a

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake git libssl-dev zlib1g-dev \
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

FROM dependencies AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libfmt-dev nlohmann-json3-dev libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

COPY 01_SMT_DataStream /src/01_SMT_DataStream
RUN cmake -S /src/01_SMT_DataStream -B /src/01_SMT_DataStream/build-container \
        -DCMAKE_BUILD_TYPE=Release -DDATASTREAM_BUILD_TESTS=OFF \
    && cmake --build /src/01_SMT_DataStream/build-container --parallel 2

FROM ubuntu:22.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates curl libfmt8 libspdlog1 libssl3 zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10000 smt-archive \
    && useradd --uid 10001 --gid 10000 --no-create-home --shell /usr/sbin/nologin smt-datastream \
    && mkdir -p /data/upload_tmp /data/archive /var/log/smt-datastream \
    && chown -R 10001:10000 /data /var/log/smt-datastream

COPY --from=dependencies /usr/local/lib/libworkflow.so* /usr/local/lib/
COPY --from=build /src/01_SMT_DataStream/build-container/datastream_server /usr/local/bin/datastream_server
COPY deploy-docker/common/load-secrets.sh /usr/local/bin/load-secrets.sh
COPY deploy-docker/config/datastream.container.json /etc/smt/datastream.json
RUN chmod 0555 /usr/local/bin/datastream_server /usr/local/bin/load-secrets.sh \
    && chmod 0444 /etc/smt/datastream.json \
    && ldconfig

USER 10001:10000
WORKDIR /data
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/load-secrets.sh"]
CMD ["/usr/local/bin/datastream_server", "--config", "/etc/smt/datastream.json"]
