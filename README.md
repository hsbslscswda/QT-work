# 心晴

心晴是我们《高级语言编程实训2》的课程项目。用 C++17 和 Qt Widgets 做了一个桌面聊天窗口，支持流式回复、多会话、本地记录和伴读浮窗。回复里的情绪标签会改变窗口顶栏的颜色。

## 下载与运行

[下载 Windows 64 位免安装版](https://github.com/hsbslscswda/QT-work/releases/latest/download/Xinqing-Windows-x64.zip) · [查看发布记录](https://github.com/hsbslscswda/QT-work/releases)

1. 解压整个 ZIP，再双击 `Xinqing.exe`。如果系统提示缺少 `MSVCP140.dll` 或 `VCRUNTIME140.dll`，先运行包内的 `vc_redist.x64.exe`。
2. 只看界面可以直接打开。要试聊，在左下角打开「服务连接配置」，填写地址、模型名和 API Key，保存后发一条消息。
3. 用远程 API 时，本机无需下载模型权重或准备独立显卡。服务账号与调用费用由使用者自行准备。

## 连接模型

两种方式任选其一：

- 已有 API：填写服务商给出的 OpenAI 兼容地址、模型名和密钥。例如地址可以是 `https://api.deepseek.com`，模型名以服务商当前提供的列表为准。
- 自己部署：使用下文的 Docker 配置启动 Qwen，客户端地址填 `http://127.0.0.1:8000/v1`，模型名填 `Qwen2.5-14B-Instruct`。

地址也支持 `/compatible-mode/v1` 等路径前缀以及完整的 `/chat/completions` 地址。“检测连接”会检查模型列表或服务健康状态；实际能否生成回复，以发送消息的结果为准。

## 从源码构建

需要 CMake 3.21+、C++17 编译器、Qt 6.4+（推荐 6.8.3；Widgets、Network，运行测试还需要 Test）。在 Qt Creator 打开仓库根目录的 `CMakeLists.txt`，选择桌面套件，构建运行即可。

命令行方式：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="你的Qt安装目录"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Windows 下用 Ninja 构建时，程序在 `build/app/Xinqing.exe`；用 Visual Studio 生成器时，在 `build/app/Release/Xinqing.exe`。Linux 下通常是 `build/app/Xinqing`。只编译客户端可在配置时加 `-DBUILD_TESTING=OFF`。完整流程见 [构建与发布](docs/build.md)。

## 本地模型与 Docker

官方权重：[Qwen/Qwen2.5-14B-Instruct](https://huggingface.co/Qwen/Qwen2.5-14B-Instruct)。权重遵循模型页面的 Apache 2.0 许可，单独下载，不放入本仓库和应用安装包。

这是官方基础权重；它不代表我们历史实验可能使用过的定制权重。使用不同模型或 API，回复与实验结果可能不同。

Docker 用于模型服务和附带的批量推理工具；桌面界面在 Windows 上直接运行。

准备支持 NVIDIA GPU 的 Linux 主机、Docker Compose 和 NVIDIA Container Toolkit，将权重完整下载到 `models/Qwen2.5-14B-Instruct` 后：

```bash
cp .env.example .env
docker compose up -d model
docker compose logs -f model
```

默认使用两张 GPU，张量并行数为 2，参考原先双 24GB 显卡环境。14B 的原始 BF16 权重本身约需 30GB，还要留出推理缓存；普通笔记本建议选 API。具体驱动、显存和修改方法见 [本地部署](docs/deployment.md)。

## 程序与推理逻辑

`app/` 是心晴 Qt 源码，`app/prompts/` 是随客户端打包的提示词。当前 v5.1 客户端直接调用模型接口，并解析回复中的情绪与文本。

`backend/a22/` 保留 A22 批量推理代码，包含情绪复核、历史片段检索、画像复核和 sensitive 标签检查。它作为独立工具随项目提供；**当前桌面聊天没有自动执行 A22 的全部复核通道**。两者的调用关系与复现方法见 [推理说明](docs/inference.md)。

## 课程展示

课堂演示可以从新建会话开始：发一条消息，看回复逐段出现，再切换会话、试一下伴读浮窗。关闭后重新打开，可以检查聊天记录是否恢复。源码入口和实现说明见 [课程项目说明](docs/course-project.md)，已做过的检查见 [验证记录](docs/validation.md)。

聊天记录和服务设置保存在本机应用数据目录。每次发送消息时，程序会把当前会话的历史发给你配置的模型服务。通过界面保存的 API Key 会以明文写入本机配置文件。在共享电脑上演示时，可以用 `XINQING_API_BASE`、`XINQING_MODEL`、`XINQING_API_KEY` 临时启动程序。

Qt 和其他依赖保留各自许可，详见 [第三方说明](THIRD_PARTY_NOTICES.md)。
