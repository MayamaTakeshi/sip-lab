FROM debian:11-slim

ARG http_proxy
ARG https_proxy
ARG no_proxy

ENV http_proxy=$http_proxy
ENV https_proxy=$https_proxy
ENV no_proxy=$no_proxy

RUN apt -y update && \
  apt install -y build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev flite-dev cmake

RUN apt install -y wget git nodejs npm

SHELL ["/bin/bash", "--login", "-c"]

RUN <<EOF cat >> /etc/profile
export http_proxy=$http_proxy
export https_proxy=$http_proxy
export no_proxy=$no_proxy
EOF

RUN groupadd --gid 1000 node && useradd --uid 1000 --gid node --shell /bin/bash --create-home node

USER node
ENV HOME /home/node

RUN mkdir /home/node/app
COPY ./build_deps.sh /home/node/app/
RUN cd /home/node/app && ./build_deps.sh

RUN cd /home/node/app && rm ./build_deps.sh

RUN rm -fr /home/node/app/pocketsphinx

# Disable npm update check
ENV NO_UPDATE_NOTIFIER true
ENV npm_config_update_notifier false

WORKDIR /app
