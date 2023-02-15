ARG OSRELEASEVER=7.9.2009
ARG OSNAME=centos
FROM $OSNAME:$OSRELEASEVER as default

RUN /usr/sbin/groupadd -f -g 991 jenkins
RUN /usr/sbin/useradd jenkins -u 1000 -g 991
RUN echo "jenkins ALL=(ALL) NOPASSWD: ALL" >>/etc/sudoers

RUN yum install -y epel-release git make gcc rpm-build autoconf automake sudo curl
RUN yum install -y libfdt-devel numactl-devel ncurses-devel libarchive-devel libconfig libconfig-devel python3-devel python36-Cython
RUN yum install -y cmake3
RUN yum clean all
