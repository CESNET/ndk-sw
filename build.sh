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
URL_3RDPARTY="https://github.com/CESNET/ndk-sw/releases/download/v6.16.0/$FILENAME_3RDPARTY"

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
    echo "  --bootstrap   Prepare for build (install prerequisities and dependencies)"
    echo "  --make        Build project"
    echo "  --clean       Remove build files"
    echo "  --purge       Remove entire build directory"
    echo "  --rpm         Build RPM package"
    echo "  --deb         Build DEB package"
}

get_os_version()
{
    supported_os=(ubuntu centos scientific arch fedora ol manjaro)

    . /etc/os-release

    if [ -z "$ID" ]; then
        echo "unknown"
        return
    fi

    for i in "${supported_os[@]}"
    do
        if [ "$i" = "$ID" ] ; then
            echo "$ID"
            return
        fi
    done

    echo >&2 "WARNING: Unknown OS"
    echo $ID
}

get_install_command()
{
    os=$(get_os_version)
    if [ "$os" = "ubuntu" ]; then
        echo "apt-get -qy install"
    elif [ "$os" = "centos" ] || [ "$os" = "scientific" ] || [ "$os" = "fedora" ] || [ "$os" = "ol" ]; then
        echo "yum -y install"
    elif [ "$os" = "arch" ] || [ "$os" = "manjaro" ]; then
        echo "pacman -S --needed"
    fi
}

get_prerequisities()
{
    os=$(get_os_version)

    ret=""
    ret="$ret git"
    ret="$ret curl"
    if [ "$os" = "ubuntu" ]; then
        ret="$ret cmake"
        ret="$ret pkg-config"
    elif [ "$os" = "centos" ] || [ "$os" = "scientific" ] || [ "$os" = "ol" ]; then
        ret="$ret epel-release"
        ret="$ret cmake3"
        ret="$ret make"
        ret="$ret gcc"
        ret="$ret rpm-build"
    elif [ "$os" = "fedora" ]; then
        ret="$ret cmake3"
        ret="$ret make"
        ret="$ret gcc"
        ret="$ret rpm-build"
    elif [ "$os" = "arch" ] || [ "$os" = "manjaro" ]; then
        ret="$ret cmake"
    fi

    echo $ret
}

get_dependencies()
{
    os=$(get_os_version)

    ret=""
    ret="$ret autoconf"
    ret="$ret automake"

    if [ "$os" = "ubuntu" ]; then
        ret="$ret libfdt-dev"
        ret="$ret libnuma-dev"
        ret="$ret libncurses-dev"
        ret="$ret libarchive-dev"
    elif [ "$os" = "centos" ] || [ "$os" = "scientific" ] || [ "$os" = "fedora" ] || [ "$os" = "ol" ]; then
        ret="$ret libfdt-devel"
        ret="$ret numactl-devel"
        ret="$ret ncurses-devel"
        ret="$ret libarchive-devel"
        ret="$ret libconfig"
        ret="$ret libconfig-devel"
    elif [ "$os" = "arch" ] || [ "$os" = "manjaro" ]; then
        ret="$ret dtc"
        ret="$ret numactl"
        ret="$ret ncurses"
        ret="$ret libarchive"
        ret="$ret libconfig"
    fi

    echo $ret
}

get_3rdparty_code()
{
    curl -L $URL_3RDPARTY --output $FILENAME_3RDPARTY
    tar -xf $FILENAME_3RDPARTY
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
    mkdir -p "$SCRIPT_PATH/$BUILDDIR"
    cd "$SCRIPT_PATH/$BUILDDIR"
    $cmake -DCMAKE_BUILD_TYPE=$RELTYPE ..
    make
}


# ----[ MAIN function ]------------------------------------------------------- #
cmake=cmake
cpack=cpack
os=$(get_os_version)

if [ "$os" = "centos" ] || [ "$os" = "scientific" ] || [ "$os" = "fedora" ]; then
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
            get_3rdparty_code
            ;;
        --prepare)
            get_3rdparty_code
            ;;
        --make)
            Build
            ;;
        --clean)
            cd "$SCRIPT_PATH/$BUILDDIR" || continue
            make clean
            ;;
        --purge)
            rm -rf "$SCRIPT_PATH/$BUILDDIR"
            ;;
        --rpm)
            if [ "$os" != "centos" ] && [ "$os" != "scientific" ] && [ "$os" != "fedora" ] && [ "$os" != "ol" ] ; then
                echo >&2 "ERROR: Cannot build RPM package on other OS than CentOS"
                exit 1
            fi
            Build
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
    esac
done
