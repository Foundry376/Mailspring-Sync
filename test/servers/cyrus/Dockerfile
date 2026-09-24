FROM debian:bookworm-slim
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends cyrus-imapd sasl2-bin libsasl2-modules procps && rm -rf /var/lib/apt/lists/*
COPY imapd.conf /etc/imapd.conf
COPY cyrus.conf /etc/cyrus.conf
COPY entrypoint.sh /entrypoint.sh
EXPOSE 143
ENTRYPOINT ["/entrypoint.sh"]
