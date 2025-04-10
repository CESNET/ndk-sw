ARG OSRELEASEVER=noble
ARG OSNAME=ubuntu
FROM $OSNAME:$OSRELEASEVER AS default
# since 24.04 LTS there is default user 'ubuntu' with uid 1000
RUN userdel -r ubuntu
RUN /usr/sbin/groupadd -f -g 991 jenkins
RUN /usr/sbin/useradd jenkins -u 1000 -g 991

RUN apt-get update && DEBIAN_FRONTEND=noninteractive TZ=Europe/Prague apt-get -y install tzdata
RUN apt-get update && apt-get install -y git sudo make curl cmake pkg-config file
RUN apt-get update && apt-get install -y autoconf automake libfdt-dev libnuma-dev libncurses-dev libarchive-dev libconfig-dev python3-dev python3-setuptools cython3 libpci-dev pciutils dpkg-dev

RUN echo "jenkins ALL=(ALL) NOPASSWD: ALL" >>/etc/sudoers