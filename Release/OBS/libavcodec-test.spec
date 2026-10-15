# Copyright (c) 2026 info@mediaarea.net
# This file and all modifications and additions to the pristine
# package are under the same license as the package itself.

# norootforbuild

Name:           libavcodec-test
Version:        %VERSION%
Release:        1
Summary:        FFmpeg decoding test tool with patches from MediaArea.net
Group:          Productivity/Multimedia/Other
License:        GPL-3.0-or-later
URL:            https://mediaarea.net
Source0:        libavcodec-test_%VERSION%.tar.xz
Prefix:         %{_prefix}
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  ffmpeg-ma-devel

%description
Decodes a file through the libav*-ma and libsw*-ma libraries and reports
the decoding throughput.

%prep
%setup -q -n libavcodec-test

%build
%__make CFLAGS="%{optflags} -I/usr/include/ffmpeg-ma" FFMPEG_SUFFIX=-ma

%install
%__install -D -m 0755 dec_tx_test %{buildroot}/%{_bindir}/dec_tx_test

%clean
[ -d "%{buildroot}" -a "%{buildroot}" != "" ] && %__rm -rf "%{buildroot}"

%files
%defattr(-,root,root,-)
%{_bindir}/dec_tx_test
