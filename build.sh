#!/bin/bash
#
# Top-level script to help with CMake build

set -e      # exit on any error
set -u      # report error on unset variables

# ----[ SETTINGS ]------------------------------------------------------------ #
BUILDDIR=cmake-build        # build directory
AUTOINSTALL=true            # whether dependencies should be installed with '-y'
RELTYPE=Release             # Release type (Debug/Release)

FILENAME_3RDPARTY="3rdparty.tar.gz"
SRC_3RDPARTY_VERSION="v6.24.0"
URL_3RDPARTY="https://github.com/CESNET/ndk-sw/releases/download/$SRC_3RDPARTY_VERSION/$FILENAME_3RDPARTY"

EPEL8_DPDK_NFB_REPO_REMOTE="https://copr.fedorainfracloud.org/coprs/g/CESNET/dpdk-nfb/repo/epel-8/group_CESNET-dpdk-nfb-epel-8.repo"
EPEL9_DPDK_NFB_REPO_REMOTE="https://copr.fedorainfracloud.org/coprs/g/CESNET/dpdk-nfb/repo/epel-9/group_CESNET-dpdk-nfb-epel-9.repo"
DPDK_NFB_REPO_LOCAL="/etc/yum.repos.d/_copr:copr.fedorainfracloud.org:group_CESNET:dpdk-nfb.repo"

# ----[ USEFUL VARIABLES ]---------------------------------------------------- #
SCRIPT_PATH="$(dirname $(readlink -f $0))"

# ----[ HELPER FUNCTIONS ]---------------------------------------------------- #
usage()
{
    progname=$(basename $0)
    echo "$progname - script for help with CMake build and release"
    echo ""
    echo "SYNOPSIS"
    echo "  $progname [ -h|--help ]            Print this help message"
    echo "  $progname <parameters> <action>    Run <action> (see below)"
    echo ""
    echo "PARAMETERS"
    echo "  -y              Enable automatic installation $($AUTOINSTALL && echo "(default)" ||:)"
    echo "  -n              Disable automatic installation $($AUTOINSTALL || echo "(default)")"
    echo "  --debug         Build a package with debugging enabled"
    echo ""
    echo "ACTIONS"
    echo "  --bootstrap         Prepare for build (install prerequisities and dependencies)"
    echo "  --bootstrap-dpdk    Install dpdk dependencies (run after bootstrap)"
    echo "  --make              Build project"
    echo "  --make-dpdk         Build project with ndp-tool-dpdk (adds -DUSE_DPDK to cmake call)"
    echo "  --clean             Remove build files"
    echo "  --purge             Remove entire build directory"
    echo "  --rpm               Build RPM package"
    echo "  --deb               Build DEB package"
    echo "  --rpm-dpdk          Build RPM packages including ndp-tool-dpdk package"
}

function item_in_list() {
    VALUE=$1
    LIST=$2
    DELIMITER=" "
    echo $LIST | tr "$DELIMITER" '\n' | grep -F -q -x "$VALUE"
}

get_os_version()
{
    . /etc/os-release
    if [ -z "$VERSION_ID" ]; then
        echo "unknown"
        return
    fi
    echo "$ID"
}

get_os_number()
{
    . /etc/os-release
    if [ -z "$VERSION" ]; then
        echo "unknown"
        return
    fi
    echo $VERSION | cut -d'.' -f1
}

get_os_id()
{
    supported_os="ubuntu centos scientific arch fedora ol manjaro rocky arcolinux"

    . /etc/os-release

    if [ -z "$ID" ]; then
        echo "unknown"
        return
    fi

    if ! item_in_list "$ID" "$supported_os"; then
        echo >&2 "WARNING: Unknown OS"
    fi

    echo "$ID"
}



get_install_command()
{
    os=$(get_os_id)
    if item_in_list "$os" "ubuntu"; then
        echo "apt-get -qy install"
    elif item_in_list "$os" "centos scientific fedora ol rocky"; then
        echo "yum -y install"
    elif item_in_list "$os" "arch manjaro arcolinux"; then
        echo "pacman -S --needed"
    fi
}

get_prerequisities()
{
    os=$(get_os_id)

    ret=""
    ret="$ret git"
    ret="$ret curl"
    if item_in_list "$os" "ubuntu"; then
        ret="$ret cmake"
        ret="$ret pkg-config"
    elif item_in_list "$os" "centos scientific ol rocky"; then
        ret="$ret epel-release"
        ret="$ret cmake3"
        ret="$ret make"
        ret="$ret gcc"
        ret="$ret rpm-build"
    elif item_in_list "$os" "fedora"; then
        ret="$ret cmake3"
        ret="$ret make"
        ret="$ret gcc"
        ret="$ret rpm-build"
    elif item_in_list "$os" "arch manjaro arcolinux"; then
        ret="$ret cmake"
    fi

    echo $ret
}

get_dependencies()
{
    os=$(get_os_id)
    os_version=$(get_os_version)
    os_version_major=$(echo $os_version | cut -d'.' -f1)

    ret=""
    ret="$ret autoconf"
    ret="$ret automake"

    if item_in_list "$os" "ubuntu"; then
        ret="$ret libfdt-dev"
        ret="$ret libnuma-dev"
        ret="$ret libncurses-dev"
        ret="$ret libarchive-dev"
        ret="$ret libconfig-dev"
        ret="$ret python3-dev"
        ret="$ret python3-setuptools"
        ret="$ret cython3"
        ret="$ret libpci-dev"
        ret="$ret zlib1g zlib1g-dev"
    elif item_in_list "$os" "centos scientific fedora ol rocky"; then
        ret="$ret libfdt-devel"
        ret="$ret numactl-devel"
        ret="$ret ncurses-devel"
        ret="$ret libarchive-devel"
        ret="$ret libconfig"
        ret="$ret libconfig-devel"
        ret="$ret pciutils-devel pciutils-libs"
        ret="$ret zlib zlib-devel"
        ret="$ret python3-devel"
        ret="$ret python3-setuptools"
        if [ "$os_version" = "7" ]; then
            ret="$ret python36-Cython"
        else
            ret="$ret python3-Cython"
        fi
    elif item_in_list "$os" "arch manjaro arcolinux"; then
        ret="$ret dtc"
        ret="$ret numactl"
        ret="$ret ncurses"
        ret="$ret libarchive"
        ret="$ret libconfig"
        ret="$ret pciutils"
        ret="$ret zlib"
        ret="$ret python-setuptools"
        ret="$ret cython"
        # for packaging
        ret="$ret fakeroot pkgconfig"
    fi

    echo $ret
}

get_dpdk_dependencies()
{
    os=$(get_os_id)
    os_version=$(get_os_version)
    os_number=$(get_os_number)

    if item_in_list "$os" "centos scientific fedora ol rocky"; then
        if  item_in_list "$os_number" "8 9" ; then
            if [ "$os_number" = "8" ] ; then
                if ! wget "${EPEL8_DPDK_NFB_REPO_REMOTE}" -O "${DPDK_NFB_REPO_LOCAL}"; then
                    echo >&2 "Could not obtain repository ${EPEL8_DPDK_NFB_REPO_REMOTE}"
                    return 1
                fi
            elif [ "$os_number" = "9" ] ; then
                if ! wget "${EPEL9_DPDK_NFB_REPO_REMOTE}" -O "${DPDK_NFB_REPO_LOCAL}"; then
                    echo >&2 "Could not obtain repository ${EPEL9_DPDK_NFB_REPO_REMOTE}"
                    return 1
                fi  
            fi
            ret=""
            ret="$ret dpdk-nfb"
            ret="$ret dpdk-nfb-devel"
            ret="$ret dpdk-nfb-tools"
            echo $ret
        fi
    else 
        echo >&2 "ERROR: NFB DPDK libraries only exist for redhat 8 and redhat 9 derived linux"
        exit 1
    fi
}

get_xdp_dependencies()
{
    os=$(get_os_id)
    os_version=$(get_os_version)
    os_number=$(get_os_number)
	ret=""
    if item_in_list "$os" "ubuntu"; then
        ret="$ret libbpf-dev"
        ret="$ret libxdp-dev"
    elif item_in_list "$os" "centos scientific fedora ol rocky"; then
        ret="$ret libbpf-devel"
        ret="$ret libxdp-devel"
    fi
	echo $ret
}

get_3rdparty_code()
{
    curl -L $URL_3RDPARTY --output $FILENAME_3RDPARTY
    tar -xf $FILENAME_3RDPARTY -C $SCRIPT_PATH/
}

try_get_3rdparty_code()
{
    if [ ! -e $SCRIPT_PATH/drivers/kernel/drivers/fdt/fdt.h ]; then
        get_3rdparty_code
    fi
}

install_dependencies()
{
    install_cmd=$(get_install_command)
    dependencies=$(get_dependencies)

    for dep in $dependencies; do
        $install_cmd $dep
    done
}

Build()
{
    try_get_3rdparty_code
    mkdir -p "$SCRIPT_PATH/$BUILDDIR"
    cd "$SCRIPT_PATH/$BUILDDIR"
    $cmake -DCMAKE_BUILD_TYPE=$RELTYPE ..
    $make_command
}

Build_dpdk()
{
    try_get_3rdparty_code
    mkdir -p "$SCRIPT_PATH/$BUILDDIR"
    cd "$SCRIPT_PATH/$BUILDDIR"
    $cmake -DCMAKE_BUILD_TYPE=$RELTYPE -DUSE_DPDK=true ..
    $make_command
}

Build_xdp()
{
    try_get_3rdparty_code
    mkdir -p "$SCRIPT_PATH/$BUILDDIR"
    cd "$SCRIPT_PATH/$BUILDDIR"
    $cmake -DCMAKE_BUILD_TYPE=$RELTYPE -DUSE_XDP=true ..
    $make_command
}


# ----[ MAIN function ]------------------------------------------------------- #
cmake=cmake
cpack=cpack
make_command="make -j"
os=$(get_os_id)

if item_in_list "$os" "centos scientific fedora"; then
    cmake=cmake3
    cpack=cpack3
fi

if [ $# -eq 0 ]; then
    usage
    exit 0
fi

for opt in "$@"; do
    case "$opt" in
        -h|--help)
            usage
            exit 0
            ;;
        -y)
            AUTOINSTALL=true
            ;;
        -n)
            AUTOINSTALL=false
            ;;
        --debug)
            RELTYPE=Debug
            ;;
        --bootstrap)
            install_cmd=$(get_install_command)
            prereqs=$(get_prerequisities)
            deps=$(get_dependencies)
            echo ">> Installing prerequisities ..."
            $install_cmd $prereqs
            echo ">> Installing package build dependencies ..."
            $install_cmd $deps
            ;;
        --bootstrap-dpdk)
            install_cmd=$(get_install_command)
            deps=$(get_dpdk_dependencies)
            echo ">> Installing package build dependencies ..."
            $install_cmd $deps
            ;;
        --bootstrap-xdp)
            install_cmd=$(get_install_command)
            deps=$(get_xdp_dependencies)
            echo ">> Installing package build dependencies ..."
            $install_cmd $deps
            ;;
        --prepare)
            echo ">> Downloading and extractiong 3rd party code ..."
            get_3rdparty_code
            ;;
        --make)
            Build
            ;;
        --make-dpdk)
            Build_dpdk
            ;;
        --make-xdp)
            Build_xdp
            ;;
        --clean)
            cd "$SCRIPT_PATH/$BUILDDIR" || continue
            make clean
            ;;
        --purge)
            rm -rf "$SCRIPT_PATH/$BUILDDIR"
            ;;
        --rpm)
            if ! item_in_list "$os" "centos scientific fedora ol rocky"; then
                echo >&2 "ERROR: Cannot build RPM package on other OS than CentOS"
                exit 1
            fi
            Build
            $cpack -G RPM --config ./CPackConfig.cmake
            ;;
        --rpm-dpdk)
            if ! item_in_list "$os" "centos scientific fedora ol rocky"; then
                echo >&2 "ERROR: Cannot build RPM package on other OS than CentOS"
                exit 1
            fi
            Build_dpdk
            $cpack -G RPM --config ./CPackConfig.cmake
            ;;
		--rpm-xdp)
            if ! item_in_list "$os" "centos scientific fedora ol rocky"; then
                echo >&2 "ERROR: Cannot build RPM package on other OS than CentOS"
                exit 1
            fi
            Build_xdp
            $cpack -G RPM --config ./CPackConfig.cmake
            ;;
        --deb)
            if [ "$os" != "ubuntu" ]; then
                echo >&2 "ERROR: Cannot build DEB package on other OS than Ubuntu"
                exit 1
            fi
            Build
            $cpack -G DEB --config ./CPackConfig.cmake
            ;;
		--deb-xdp)
            if [ "$os" != "ubuntu" ]; then
                echo >&2 "ERROR: Cannot build DEB package on other OS than Ubuntu"
                exit 1
            fi
            Build_xdp
            $cpack -G DEB --config ./CPackConfig.cmake
            ;;
    esac
done
