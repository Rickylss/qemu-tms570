if [ $# != 1 ];then
    echo "usage erron"
else
    PD=`pwd`
    srcpath=$PD"/../configure"
    buildpath=$PD"/../../qemu-build/$1"
    mkdir $1
    cd $1
    $srcpath --prefix=$buildpath --target-list=arm-softmmu,ppc-softmmu --cross-prefix=i686-w64-mingw32-   --enable-gtk --enable-sdl --python=/usr/bin/python2   #--enable-debug     
    make -j4
fi 
