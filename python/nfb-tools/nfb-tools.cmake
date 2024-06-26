set(RPM_PYTHONTOOLS_DEP_LIST
	python3-fdt
	python3-nfb
	python3-yaml
#    python3-ofm
)

set(DEB_PYTHONTOOLS_DEP_LIST
	python3-nfb
	python3-yaml
#    python-ofm
)

set(CPACK_RPM_pythontools_PACKAGE_NAME  "python3-nfb-tools")

string(JOIN " " CPACK_RPM_pythontools_PACKAGE_REQUIRES ${RPM_PYTHONTOOLS_DEP_LIST})

string(JOIN ", " CPACK_DEBIAN_PYTHONTOOLS_PACKAGE_DEPENDS ${DEB_PYTHONTOOLS_DEP_LIST})
set(CPACK_DEBIAN_PYTHONTOOLS_PACKAGE_NAME "python3-nfb-tools")
set(CPACK_DEBIAN_PYTHONTOOLS_FILE_NAME "python3-nfb-tools-${CPACK_DEBIAN_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}.deb")
#set(RPM_PYTHON_DEP_LIST
#	ofm
#)
#
#set(DEB_PYTHON_DEP_LIST
#	python-ofm
#)
#
#string(JOIN " " CPACK_RPM_pythontools_PACKAGE_REQUIRES ${RPM_PYTHON_DEP_LIST})
#string(JOIN ", " CPACK_DEBIAN_pythontools_PACKAGE_DEPENDS ${DEB_PYTHON_DEP_LIST})

