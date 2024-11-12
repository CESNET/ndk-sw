ARG OSRELEASEVER=9
ARG OSNAME=centos
FROM $OSNAME:$OSRELEASEVER as default

RUN /usr/sbin/useradd jenkins
RUN echo "jenkins ALL=(ALL) NOPASSWD: ALL" >>/etc/sudoers

RUN yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
RUN dnf config-manager --set-enabled ol9_codeready_builder;
RUN dnf copr enable @CESNET/dpdk-nfb

RUN yum install -y epel-release git make gcc rpm-build autoconf automake sudo curl
RUN yum install -y libfdt-devel numactl-devel ncurses-devel libarchive-devel libconfig libconfig-devel python3-devel pciutils-devel pciutils-libs
RUN yum install -y cmake
RUN yum install -y wget

RUN yum clean all
RUN pip install cython
