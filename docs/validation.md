# 本次验证

验证日期：2026-11-18。

本地 Linux x86_64：GCC 14.2.0、Qt 6.8.3，Release 构建通过。GitHub Actions 的 Windows MSVC/Qt 6.8.3 和 Ubuntu 24.04/Qt 构建与测试均已通过。

Qt 自动检查通过：五组 API 地址（根地址、v1、代理前缀、自定义前缀、完整端点）；本地 HTTP 服务接收真实 Qt 请求，校验路径、模型名与鉴权字段；中文 SSE 回复完成显示并写入会话文件。回复为固定的合成测试文本，没有调用真实模型。

A22 的 12 项边界检查、两项 HTTP/校验检查通过。上传过程中的换行转换问题已修复，并通过 .gitattributes 保持原文件字节；八个 A22 原文件校验值一致。

GitHub Actions 已执行并通过 Compose 配置检查、Python 批量工具镜像构建与 CLI 检查、Linux Qt 镜像构建及其中的 Qt 自动测试。

Windows 已完成 windeployqt 打包并发布 Release。实际下载检查了构建产物：ZIP 完整、包含 Xinqing.exe、Qt6Core/Network/Widgets DLL、platforms/qwindows.dll、Qt 许可和 vc_redist.x64.exe。部分 Windows 电脑首次需要运行附带的 C++ 运行库安装程序。尚未在没有 Qt/开发工具的全新 Windows 电脑上人工打开界面。

通过的构建：https://github.com/hsbslscswda/QT-work/actions/runs/44229674562

构建与二进制对应源码：3f85f979f0502b12d6f3bc74d0931cfb7ab7b906。后续说明文字修订不会改变该二进制的源码对应记录。

没有 GPU 或模型权重加载实测，也没有调用真实商业 API 测量回复质量或延迟。这些结果仅验证程序、接口、打包及容器构建，不构成模型能力评分。
