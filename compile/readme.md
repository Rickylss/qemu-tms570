
# 编译脚本
 > build.sh为ubuntu、winxp、win7通用编译脚本。

使用命令:

```shell
$ source build.sh [newpath]
```

执行成功后，会在项目同级目录下新建`qemu-compile/[newpath]`文件夹，在该文件夹下有qemu-build和qemu-install两个子文件夹。

- qemu-build文件夹保存qemu编译信息；
- qemu-install文件夹保存qemu安装文件。

新编译的qemu安装在qemu-install文件夹中。同时脚本执行成功后会将qemu-install/bin加入到PATH中，可直接运行可执行文件。

# 启动脚本

 > `launchQemu.bat`和`launch.sh`为qemu启动脚本。
 
- 请将脚本中的可执行文件路径替换为您环境中正确的路径;
- apptestaddr后接bin文件路径（文件路径不能超过100个字符），并指定文件加载地址(加载地址为16进制)，格式为`-apptestaddr [binpath],0xxxx`。若加载多个app，只需增设-apptestaddr即可。