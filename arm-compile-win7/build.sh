if [ $# != 1 ];then
    echo "usage erron"
else
    PD=`pwd`
    srcpath=$PD"/../configure"
    buildpath=$PD"/../../arm-build/$1"
    mkdir $1
    cd $1
    $srcpath --prefix=$buildpath --target-list=arm-softmmu --cross-prefix=i686-w64-mingw32-   --enable-gtk --enable-sdl --enable-debug --python=/usr/bin/python2        
    make -j4
fi 