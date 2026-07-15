FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates coreutils default-mysql-client jq redis-tools tar \
    && rm -rf /var/lib/apt/lists/*

COPY deploy-docker/common/load-secrets.sh /usr/local/bin/load-secrets.sh
COPY deploy-docker/ops/*.sh /usr/local/bin/
COPY deploy-docker/config/*.json /etc/smt/
COPY 01_SMT_DataStream/scripts/db.sh /opt/smt/datastream/scripts/db.sh
COPY 01_SMT_DataStream/migrations /opt/smt/datastream/migrations
COPY 02_SMT_LogTrace/scripts/db.sh /opt/smt/logtrace/scripts/db.sh
COPY 02_SMT_LogTrace/migrations /opt/smt/logtrace/migrations

RUN chmod 0555 /usr/local/bin/*.sh \
        /opt/smt/datastream/scripts/db.sh /opt/smt/logtrace/scripts/db.sh \
    && chmod -R a-w /etc/smt /opt/smt

ENTRYPOINT ["/usr/local/bin/load-secrets.sh"]
