# 源码来源与整理记录

桌面程序来源：用户提供的 Xinqing_v5.1_SilentHorizon_NoLabels.zip，原目录名为 A18_QtWorkbench，实际窗口和交互为心晴 v5.1。

原压缩包 SHA-256：`c5b399e7bdca1ba0dbd0eac0dae9a89df2fd7f1b9ae1826ace4150ee67d4eada`。

本次整理将程序目标名改为 Xinqing，补齐根目录 CMake、Windows 依赖打包脚本、GitHub 自动构建与 Release 发布、模型部署 Compose、批量工具 Dockerfile 和课程说明。

程序修复包括：补齐 QScreen 头文件；API 地址保留已有路径前缀并处理末尾斜杠；模型列表检测使用同一 API 基址；连接检测不再把模型列表可达说成已验证生成；增加临时环境变量配置入口。Qt 界面布局和陪伴提示词沿用收到的版本。

A22 批量代码来自此前交付的 runtime_A22，具体文件校验值见 backend/a22/SHA256SUMS.json，未在本轮调整算法。旧版说明保留在 docs/archive/README_A18_original.md，它描述历史工作台，不能替代本仓库当前的使用文档。
