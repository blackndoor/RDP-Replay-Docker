FROM ubuntu:14.04

ENV LANG en_GB.UTF-8
ENV XTERM_LOCALE en_GB.UTF-
ENV LC_COLLATE C

RUN apt-get update
RUN apt-get install -y build-essential git-core cmake libssl-dev libx11-dev libxext-dev libxinerama-dev libxcursor-dev libxdamage-dev libxv-dev libxkbfile-dev libasound2-dev libcups2-dev libxml2 libxml2-dev libxrandr-dev libgstreamer0.10-dev libgstreamer-plugins-base0.10-dev libavutil-dev libavcodec-dev libavformat-dev libpcap-dev libreadline-dev x11-xkb-utils

COPY RDP-Replay/ /RDP-Replay

RUN cd /RDP-Replay && make

WORKDIR /home

ENTRYPOINT ["/RDP-Replay/replay/rdp_replay"]
