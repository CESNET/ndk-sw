ARG OSRELEASEVER=8
ARG OSNAME=centos
FROM $OSNAME:$OSRELEASEVER as default

RUN /usr/sbin/groupadd -f -g 991 jenkins
RUN /usr/sbin/useradd jenkins -u 1000 -g 991
RUN echo "jenkins ALL=(ALL) NOPASSWD: ALL" >>/etc/sudoers

RUN yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm
RUN dnf config-manager --set-enabled ol8_codeready_builder;

RUN yum install -y epel-release git make gcc rpm-build autoconf automake sudo curl
RUN yum install -y libfdt-devel numactl-devel ncurses-devel libarchive-devel libconfig libconfig-devel
RUN yum install -y cmake
RUN yum clean all
