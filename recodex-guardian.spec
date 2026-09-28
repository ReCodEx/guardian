%define name recodex-guardian
%define short_name guardian
%define version 0.1.0
%define unmangled_version 3bb24b7f599d8f2237fa1013ce83c449ce0104ec
%define release 1

# yaml-cpp is fetched by CMakeLists.txt via FetchContent, pinned to this exact
# commit; vendored below (Source1) so configure needs no network in the
# COPR/mock build chroot. Keep this in sync with the GIT_TAG in CMakeLists.txt.
%global yamlcpp_commit 356643ba267039367d87fa70ce3d8f87144f55e9

Summary: ReCodEx guardian - built-in default sandbox for ReCodEx worker
Name: %{name}
Version: %{version}
Release: %{release}
License: MIT
URL: https://github.com/ReCodEx/%{short_name}
Source0: https://github.com/ReCodEx/%{short_name}/archive/%{unmangled_version}/%{short_name}-%{unmangled_version}.tar.gz
Source1: https://github.com/jbeder/yaml-cpp/archive/%{yamlcpp_commit}/yaml-cpp-%{yamlcpp_commit}.tar.gz

# C++23 <format> needs GCC 13+/Clang 17+ (CMakeLists.txt's HAVE_STD_FORMAT
# gate); el10's stock gcc-c++ satisfies this directly.
BuildRequires: gcc-c++
BuildRequires: cmake
BuildRequires: libcap-devel
# -static-libstdc++/-static-libgcc (src/CMakeLists.txt) link libstdc++.a, which
# only libstdc++-static ships — gcc-c++ alone only gives the shared libstdc++.so.
BuildRequires: libstdc++-static
Requires: libcap

%global debug_package %{nil}

%description
A sandbox for ReCodEx worker designed for safe execution of tested user solutions. The guardian is inspired by isolate, a sandbox originally designed for the IOI contest.

%prep
%autosetup -n %{short_name}-%{unmangled_version}
tar -xzf %{SOURCE1} -C %{_builddir}

%build
# cmake defaults to -DBUILD_SHARED_LIBS=ON, which yaml-cpp's own
# YAML_BUILD_SHARED_LIBS option mirrors — building it SHARED instead of the
# intended static link, and leaving the binary needing a libyaml-cpp.so.0.9
# that (YAML_CPP_INSTALL=OFF) is never packaged. Force it back off.
%cmake -DBUILD_SHARED_LIBS=OFF \
       -DFETCHCONTENT_SOURCE_DIR_YAML-CPP=%{_builddir}/yaml-cpp-%{yamlcpp_commit}
%cmake_build

%install
%cmake_install

%files
%attr(4755,root,root) %{_bindir}/recodex-guardian
%dir %attr(0755,root,root) /var/lib/recodex-guardian
%dir %attr(0755,root,root) /var/lib/recodex-guardian/boxes
%{_mandir}/man1/recodex-guardian.1*

%changelog

