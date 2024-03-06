ARG OSRELEASEVER=focal
ARG OSNAME=ubuntu
FROM $OSNAME:$OSRELEASEVER as default

RUN /usr/sbin/groupadd -f -g 991 jenkins
RUN /usr/sbin/useradd jenkins -u 1000 -g 991

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive TZ=Europe/Prague apt-get -y install tzdata
RUN apt-get install -y git sudo make curl cmake pkg-config file
RUN apt-get install -y autoconf automake libfdt-dev libnuma-dev libncurses-dev libarchive-dev libconfig-dev python3-dev python3-setuptools cython3 libpci-dev pciutils

RUN echo "jenkins ALL=(ALL) NOPASSWD: ALL" >>/etc/sudoers
