if [ $# != 1 ];then
    echo "usage erron"
else

    case `uname` in
        Linux) target='arm-softmmu,arm-linux-user,ppc-softmmu,ppc-linux-user'
               cross=''
               disable=''
               enable=''
               ;;
        CYGWIN_NT-5.1) target='arm-softmmu,ppc-softmmu'
               cross='i686-w64-mingw32-'
               disable='--disable-docs --disable-gnutls --disable-curl --disable-libssh2 --disable-pie'
               enable='--enable-gtk --enable-sdl'
               ;;
        MSYS_NT-*) target='arm-softmmu,ppc-softmmu'
               cross='i686-w64-mingw32-'
               disable=''
               enable='--enable-gtk --enable-sdl --python=/usr/bin/python2'
               ;;
        *) return
        ;;
    esac

    cd ..
    qemupath=`pwd`
    srcpath="$qemupath/configure"
    compilepath="$qemupath/../qemu-compile/$1"
    buildpath="$compilepath/qemu-build"
    installpath="$compilepath/qemu-install"
    mkdir -p $installpath
    mkdir -p $buildpath
    cd $buildpath

    $srcpath --prefix=$installpath --target-list=$target --cross-prefix=$cross $enable $disable  #--enable-debug     
    make -j4 install

    cd $installpath/bin
    export OLD_PATH=$PATH
    export PATH=$OLD_PATH:`pwd`
    echo $PATH
fi 
