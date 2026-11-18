# 构建与发布

Qt Creator 打开仓库根目录的 CMakeLists.txt。Qt 最低 6.4，推荐 6.8.3，C++17，Windows 使用 MSVC 2022 x64，Linux 使用 GCC。CMake 最低版本 3.21。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

只构建程序可增加 `-DBUILD_TESTING=OFF`。Linux 运行 `build/app/Xinqing`；Windows 使用 Ninja 时为 `build/app/Xinqing.exe`，使用 Visual Studio 多配置生成器时为 `build/app/Release/Xinqing.exe`。

Windows 打包：

```powershell
./scripts/package-windows.ps1 -BuildDir build -QtDir C:/Qt/6.8.3/msvc2022_64
```

该脚本调用 Qt 官方的 windeployqt 收集 DLL、平台插件和编译器运行库，并附上使用说明、Qt 许可和校验值。运行前先通过测试。请在没有 Qt 开发环境的 Windows 电脑上再做一次解压运行验证。

GitHub Actions 在 main 或版本标签推送时检查 Windows、Linux、Qt 与 A22，然后为 `hsbslscswda/QT-work` 生成发布包。main 构建使用唯一的 `desktop-build-编号` 标签；每个 Release 记录对应源码提交，避免源码与二进制对应不清。拉取请求只测试，不发布。

首次推送后，下载链接要等所有检查通过、Release 出现后才有效。如果 GitHub 拒绝添加 workflow 文件，需要有工作流写入权限的 GitHub 登录或 Git 凭据；本地脚本仍可直接使用。

Docker 的两个 Dockerfile 分别用于批量 API 客户端和 Linux Qt 构建。`docker/Dockerfile.qt` 构建阶段会运行 Qt 测试，运行阶段仍需显示服务器，不能直接代替 Windows 桌面程序。

第三方 Actions 来源：actions/checkout、actions/upload-artifact、actions/download-artifact、jurplel/install-qt-action、ilammy/msvc-dev-cmd。没有在仓库中存放访问令牌。
