#!/bin/bash

yum -y update

#yum -y install \
#  curl \
#  git \
#  libasound2-dev \
#  libc6-dev \
#  libexpat1-dev \
#  libgtk-3-dev \
#  libnspr4-dev \
#  libnss3-dev \
#  libpulse-dev \
#  libudev-dev \
#  libxrandr-dev \
#  python3 \
#  rsync \
#  sudo \
#  vim \
#  wget
yum -y install \
  curl \
  git \
  glibc-devel \
  expat-devel \
  nspr-devel \
  nss-devel \
  pulseaudio-libs \
  libXrandr-devel \
  python3 \
  rsync \
  sudo \
  vim \
  ncurses-compat-libs \
  wget
yum -y groupinstall "Development Tools"

sudo alternatives --set python /usr/bin/python3
