srpm:
	dnf -y install cmake3 git curl gcc  make rpm-build libfdt-devel numactl-devel ncurses-devel libarchive-devel libconfig libconfig-devel
	./build.sh --prepare; mkdir build; cd build; cmake3 ..; cpack3 -G RPM --config ./CPackSourceConfig.cmake; cp *.src.rpm $(outdir)

srpm-dpdk:
	./build.sh --prepare; mkdir build; cd build; cmake3 -DUSE_DPDK=true ..; cpack3 -G RPM --config ./CPackSourceConfig.cmake;
	cd /workdir; cp ./ndk-sw/build/_CPack_Packages/Linux-Source/RPM/SOURCES/*.tar.gz .; cp ./ndk-sw/build/_CPack_Packages/Linux-Source/RPM/SPECS/*.spec .
	cd /workdir; sed -i 's/\%define _srcrpmdir \%_topdir\/SRPMS//g' ./*.spec

srpm-xdp:
	./build.sh --prepare; mkdir build; cd build; cmake3 -DUSE_XDP=true ..; cpack3 -G RPM --config ./CPackSourceConfig.cmake;
	cd /workdir; cp ./ndk-sw/build/_CPack_Packages/Linux-Source/RPM/SOURCES/*.tar.gz .; cp ./ndk-sw/build/_CPack_Packages/Linux-Source/RPM/SPECS/*.spec .
	cd /workdir; sed -i 's/\%define _srcrpmdir \%_topdir\/SRPMS//g' ./*.spec
