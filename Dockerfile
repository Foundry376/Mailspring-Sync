# Create ubuntu as base image
FROM ubuntu:16.04

# Install packages
ENV CXX g++
ENV CC gcc
ENV DEBIAN_FRONTEND noninteractive
RUN cp /etc/apt/sources.list /etc/apt/sources.list~
RUN sed -Ei 's/^# deb-src /deb-src /' /etc/apt/sources.list
RUN apt-get -y update
RUN apt-get -yq install libc-ares-dev libicu-dev libctemplate-dev libtidy-dev uuid-dev libxml2-dev libsasl2-dev liblzma-dev libxml2-dev
RUN apt-get -yq install wget sudo cmake autoconf automake libtool-bin gcc-5 g++-5 libglib2.0-dev

# Copy Source code
WORKDIR /app
COPY . .

# Add user ubuntu and allow sudo
RUN adduser --disabled-password --gecos "" ubuntu
RUN usermod -g sudo ubuntu
RUN passwd -d ubuntu
RUN chown -R ubuntu:ubuntu /app
USER ubuntu