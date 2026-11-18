# 本次验证

验证日期：2026-11-18。

Linux x86_64 环境，GCC 14.2.0，Qt 6.8.3，Release 构建完成，生成 Xinqing 程序。

Qt 自动检查通过：五组 API 地址（根地址、v1、代理前缀、自定义前缀、完整端点）；本地 HTTP 服务接收真实 Qt 请求，校验路径、模型名与鉴权字段；中文 SSE 回复完成显示并写入会话文件。回复为固定的合成测试文本，没有调用真实模型。

A22 的 12 项边界检查全部通过；另两项 Python 检查验证 A22 文件校验值及实际 HTTP 调用、情绪复核、sensitive 标签移除、输出文件与单样本性能报告。A22 的八个原文件校验值保持一致。

Compose 和 GitHub workflow 文件已通过 YAML 解析。当前环境没有 Docker CLI、Docker 守护进程和 GPU，因此未在本机执行镜像构建、Compose 启动或 GPU 模型加载。GitHub workflow 配置了 Linux 容器构建和配置检查，但截至本次源码交付尚未在 GitHub 运行。

Windows 自动编译、windeployqt 打包和 Release 发布脚本已准备；尚未在 Windows runner 执行，也未完成无 Qt 环境的 Windows 解压运行验证。当前压缩包为源码与部署配置，不是 Windows 免安装程序。
