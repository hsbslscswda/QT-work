# A18 + Qwen2.5-14B 推理工作台（Qt Creator / C++ 版）

原生 C++ Qt Widgets 桌面软件，把 A18 交接包里的命令行推理流程包装成可交互界面。
严格遵循《Qt封装说明.md》的分层约定：

- Qt 层只负责**启动推理进程（QProcess）、传入输入 JSONL 路径、读取结果 JSONL、显示日志**；
- **不嵌入模型权重**，模型单独部署在宿主机 `/mnt/storage/models/Qwen2.5-14B-Instruct`；
- **不改动** A18 的提示词、输出结构、后处理和任何推理代码，底层仍然是 `bash start.sh`。

## 功能

| 模块 | 说明 |
| --- | --- |
| **对话（智能体模式）** | 与 A18 智能体多轮聊天：气泡对话界面 + 每轮情绪/画像/记忆引用分析面板；vLLM 服务常驻，首轮加载模型约 1-2 分钟 |
| 路径配置 | 工具目录（participant_v02A18）、快捷数据集（smoke / dev220 / val100 / full2354 / 自定义）、结果目录 |
| 环境变量 | VLLM_MODEL_DIR、VLLM_SERVED_MODEL_NAME、VLLM_HOST、VLLM_PORT、EVALUATION_BACKEND，对应 start.sh 的覆盖变量 |
| 运行控制 | QProcess 启动 / 停止 `bash start.sh <input.jsonl> <result_dir>` |
| 运行日志 | 实时显示所有进程的 stdout / stderr（推理、评分、vLLM 服务） |
| 结果浏览 | 表格列出 submission.jsonl（情绪标签带中文对照），点击行查看对话历史 + 完整回复 + 用户画像；顶部显示 performance_report.json 摘要 |
| 评分 | 一键调用 dev/tools/score_submission.py，界面直接展示指标 JSON |

### 对话模式说明

对话模式同样**不改动任何推理代码**：每条消息把当前会话历史打包成单条 JSONL
（`{"id": "...", "history": [...]}`，与官方测试集格式一致），调用
`run_inference.py` 走完整的 A18 提示词与后处理管线，直连常驻的 vLLM 服务
（EVALUATION_BACKEND=openai_compatible）。

- 「启动服务」执行的 vLLM 命令与 start.sh 内部完全一致（tensor-parallel-size 2、离线模式），
  区别只是服务保持常驻供多轮对话复用；也可勾选「使用外部已启动的服务」连接自己管理的 vLLM。
- 「检测状态」请求 `http://<host>:<port>/health`，绿灯后即可发送消息。
- 会话临时文件保存在 `results/chat_session/`（每轮的输入与输出），便于核对。
- 某轮推理失败时该轮用户消息自动撤回，可直接重发。

## 用 Qt Creator 打开

1. Qt Creator → 文件 → 打开文件或项目 → 选择 `qt_creator_app/CMakeLists.txt`；
2. 选择 Desktop Qt 6.5.3 MinGW 64-bit 套件（msvc2019_64 也可以），配置项目；
3. 直接构建运行（Ctrl+R）。

启动后工具目录会自动从可执行文件位置向上查找 `v02_experiment/participant_v02A18`；
找不到就手动「浏览…」选择。

## 使用流程

1. **快捷数据集**选 smoke / dev220 / val100 / full2354，输入与 gold 路径自动填好；
2. 确认环境变量（默认已是 14B 交接配置）；
3. 点**开始推理**，日志页实时滚动；结束后自动切到结果页；
4. 需要指标时切到**评分**页，点「运行评分」。

建议运行顺序（沿用交接约束）：先 dev220，再 val100，都确认后才跑 full2354。

## 部署到 GPU 宿主机（Linux）

推理必须在装有 NVIDIA 驱动 + CUDA + vLLM、并挂载模型目录的 Linux 主机上运行。
本工程是标准 CMake Qt 工程，跨平台：

```bash
# 宿主机安装 Qt6 开发包（或在线安装器装 Qt 6.5+）后：
cd qt_creator_app
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/A18Workbench        # 无显示器时用 X11 转发 / VNC
```

Windows 下已验证可构建运行：`build/A18Workbench.exe`（已用 windeployqt 收集依赖）。
如需脱离 Qt 环境分发，直接打包整个 build 目录即可。

## 目录结构

```
qt_creator_app/
├── CMakeLists.txt     # Qt Creator 直接打开
├── main.cpp           # 程序入口
├── mainwindow.h/.cpp  # 主窗口：配置面板 / 日志 / 结果 / 评分
├── resultloader.h/.cpp# submission.jsonl / performance_report.json / 输入 JSONL 解析
└── README.md
```

## 注意事项

- 「停止」按钮会杀掉 start.sh 进程；脚本内的 EXIT trap 会负责清理它拉起的 vLLM 子进程。
- 结果目录允许已存在；每次运行会覆盖其中的 submission.jsonl 与 performance_report.json。
- 更换模型（如 32B-AWQ）时，按交接要求必须重新跑 dev/val 并保留评分文件；界面里只需改
  VLLM_MODEL_DIR / VLLM_SERVED_MODEL_NAME 两个变量。
- 评分耗时统计只看前 100 条（performance_rounds），与原始脚本行为一致。
- 仓库里另有 `qt_app/`（PySide6 版本），功能相同；如果宿主机不想装 Qt 开发包，
  在服务器上 `pip install PySide6` 后 `python main.py` 即可，任选其一。
