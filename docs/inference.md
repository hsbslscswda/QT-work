# 附带的 A22 推理工具

心晴 v5.1 的桌面聊天使用自己的提示词直接请求模型。A22 是项目另一部分的批量推理工具，接收 JSONL 文件并输出结构化预测和通道记录。当前两者没有通过一个网关自动串在一起。

`backend/a22/` 从已交付的 A22 代码复制，算法、提示词与配置没有为课程仓库重新调整。`SHA256SUMS.json` 记录这次复制的文件校验值。旧 `start.sh` 依赖原平台的 Conda 环境，作为历史入口保留；新环境请使用下面的 Docker/API 方式。

## 使用已有 API

输入示例是 `examples/chat.jsonl`。每行含独立样本的 `id` 和完整 `history`，消息使用 `role`、`content` 字段。

不安装任何第三方 Python 包也可运行 HTTP 后端：

```bash
export EVALUATION_BACKEND=openai_compatible
export EVALUATION_BASE_URL=https://your-provider.example/v1
export EVALUATION_API_MODEL=your-model-name
export EVALUATION_API_KEY=your-own-key
python3 backend/a22/run_inference.py examples/chat.jsonl results/example
```

每个样本会执行主回复与情绪复核，按条件执行画像与 sensitive 标签检查，因此可能产生多次 API 请求及相应费用。接口必须支持非流式 Chat Completions。历史检索只使用输入会话自身的内容，不会凭空接入外部记忆库。

Docker 方式：复制 `.env.example` 为 `.env`，填入其中的 `EVALUATION_*` 参数后执行：

```bash
docker compose --profile tools run --rm batch
```

默认读取仓库中的公开示例，输出到 `results/example`。使用自己的数据时，把文件放入 `data/`，再执行：

```bash
docker compose --profile tools run --rm batch /data/input.jsonl /results/my-run
```

容器连接本项目 `model` 服务时，将 `.env` 的 `EVALUATION_BASE_URL` 改成 `http://model:8000/v1`，模型名及 Key 与模型服务保持一致，先启动 `model`。容器中的 `127.0.0.1` 指容器自身，不是宿主机。

## 结果与验证

输出含 `submission.jsonl`、`performance_report.json`、`a22_channel_audit.jsonl`、`sensitive_gate_audit.jsonl`。计时使用前 100 条；示例只有一条，因此性能报告的 `complete=false` 是预期结果，不是完成了 100 条测速。

```bash
python3 backend/tools/test_a22_boundaries.py --project backend/a22 --output results/boundary-check
```

该检查使用合成回复验证通道和异常分支，不调用真实模型，不构成准确率或延迟测试。输出目录必须尚不存在。`validate_run.py --help` 提供预测结构、输入对应关系和性能文件的检查入口。

历史比赛结果没有作为当前桌面应用的实测分数。官方基础权重不能自动恢复未提供的自定义权重，历史评测数据也没有作为普通课程源码公开打包。
