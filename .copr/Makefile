srpm:
	dnf -y install cmake3 git curl gcc  make rpm-build libfdt-devel numactl-devel ncurses-devel libarchive-devel libconfig libconfig-devel
	./build.sh --prepare; mkdir build; cd build; cmake3 ..; cpack3 -G RPM --config ./CPackSourceConfig.cmake; cp *.src.rpm $(outdir)
