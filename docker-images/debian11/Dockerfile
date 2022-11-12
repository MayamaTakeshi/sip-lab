FROM debian:11-slim

RUN apt -y update && \
  apt install -y build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev cmake wget git nodejs npm

SHELL ["/bin/bash", "--login", "-c"]

RUN groupadd --gid 1000 node && useradd --uid 1000 --gid node --shell /bin/bash --create-home node

USER node
ENV HOME /home/node

RUN mkdir /home/node/app
COPY ./build_deps.sh /home/node/app/
RUN cd /home/node/app && ./build_deps.sh

RUN cd /home/node/app && rm ./build_deps.sh

# Disable npm update check
ENV NO_UPDATE_NOTIFIER true
ENV npm_config_update_notifier false

WORKDIR /app