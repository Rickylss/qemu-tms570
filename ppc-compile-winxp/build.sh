if [ $# != 1 ];then
    echo "usage erron"
else
    PD=`pwd`
    srcpath=$PD"/../configure"
    buildpath=$PD"/../../ppc-build/$1"
    mkdir $1
    cd $1
    $srcpath --prefix=$buildpath --target-list=ppc-softmmu --cross-prefix=i686-w64-mingw32- --disable-docs --disable-gnutls --disable-curl  --disable-libssh2  --enable-gtk --enable-sdl --enable-debug --disable-pie        #--prefix=buildpath用于指定生成的可执行文件路径,--#target-list=arm-softmmu，生成的qemu的运行模式为系统模式
    make -j4 install