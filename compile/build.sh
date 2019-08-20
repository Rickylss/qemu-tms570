if [ $# != 1 ];then
    echo "usage erron"
else

    case `uname` in
        Linux) target='arm-softmmu,arm-linux-user,ppc-softmmu,ppc-linux-user'
               cross=''
               disable=''
               enable='--enable-tcg-interpreter'
               ;;
        CYGWIN_NT-5.1) target='arm-softmmu,ppc-softmmu'
               cross='i686-w64-mingw32-'
               disable='--disable-docs --disable-gnutls --disable-curl --disable-libssh2 --disable-pie'
               enable='--enable-gtk --enable-sdl --enable-tcg-interpreter'
               ;;
        MSYS_NT-7*) target='arm-softmmu,ppc-softmmu'
               cross='i686-w64-mingw32-'
               disable=''
               enable='--enable-gtk --enable-sdl --enable-tcg-interpreter --python=/usr/bin/python2'
               ;;
        MSYS_NT-10*) target='arm-softmmu,ppc-softmmu'
               cross='x86_64-w64-mingw32-'
               disable=''
               enable='--disable-werror --enable-gtk --enable-sdl --enable-tcg-interpreter --python=/usr/bin/python2'
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
    mkdir -p $installpath/bin
    mkdir -p $buildpath
    cd $buildpath

    $srcpath --prefix=$installpath --bindir=$installpath/bin --target-list=$target --cross-prefix=$cross $enable $disable  --enable-debug     
    make -j4 install

    cd $installpath/bin
    if [ -z "$OLD_PATH" ];then
        export OLD_PATH=$PATH
    else
        echo "OLD_PATH already set"
    fi
    export PATH=$OLD_PATH:`pwd`
    echo $PATH
fi 
