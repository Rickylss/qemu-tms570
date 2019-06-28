if [ $# != 1];then
	echo "usage erron"
else
	PD=`pwd`
	srcpath=$PD"/../configure"
	buildpath=$PD"/../../arm-build/"$1
	mkdir $1
	cd $1
	$srcpath --prefix=$buildpath --target-list=arm-softmmu,ppc-softmmu --cross-prefix=i686-w64-mingw32- --disable-docs --disable-gnutls --disable-curl --disable-libssh2  --enable-gtk  --enable-sdl --disable-pie #--enable-debug
	make -j3

fi
