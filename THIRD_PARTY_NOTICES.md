# 第三方组件

心晴使用 Qt Widgets、Qt Network 和 Qt Core。Windows 发布包动态链接 Qt 6.8.3，Qt 代码和组件保留其自身许可；相关许可随包放在 `licenses/Qt`。动态库可替换，不对用户修改 Qt 库施加技术限制。

Qt 许可与源码：

- https://www.qt.io/licensing/open-source-lgpl-obligations
- https://code.qt.io/cgit/qt/qtbase.git/tree/?h=v6.8.3
- https://download.qt.io/archive/qt/6.8/6.8.3/submodules/

Qt 包含的第三方组件与许可应结合该版本源码中的 LICENSES 和各组件的 qt_attribution.json 阅读。打包脚本从 Qt 官方源码包提取许可、第三方通知及归属文件，保留相对路径；若安装目录提供 SBOM，也随包复制。

本地模型示例为 Qwen2.5-14B-Instruct，权重来源和 Apache 2.0 许可见：https://huggingface.co/Qwen/Qwen2.5-14B-Instruct 。仓库不包含模型权重。

Docker 使用官方 Python、Ubuntu、vLLM 镜像，各镜像及附带软件遵循其自己的许可。模型服务的依赖与 Qt 桌面程序分开部署。

仓库公开可见不等于项目作者已为全部原创源码选择某个开源许可。项目源码的再分发授权由作者决定，本文不代为添加 MIT 或 Apache 授权。
