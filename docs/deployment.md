# 本地模型部署

客户端和模型服务可以在同一台机器，也可以分别运行在 Windows 电脑与 Linux GPU 服务器上。使用商业或第三方 API 时跳过本文件，直接在客户端配置服务地址即可。

## 权重下载

官方入口：https://huggingface.co/Qwen/Qwen2.5-14B-Instruct

在 Python 环境安装 Hugging Face 的下载工具，然后下载完整模型：

```bash
python -m pip install huggingface_hub
hf download Qwen/Qwen2.5-14B-Instruct --local-dir models/Qwen2.5-14B-Instruct
```

目录中应有 `config.json`、分词器配置和完整的 safetensors 分片。不要只下载某个权重分片。下载可能需要较长时间和约 30GB 磁盘空间，运行时还需要额外缓存空间。

## 启动服务

需要 Linux、适配 GPU 的 NVIDIA 驱动、Docker Engine、Compose v2，以及已配置好的 NVIDIA Container Toolkit。先确认 `nvidia-smi` 和 Docker GPU 访问可用。驱动要求以所使用的 vLLM 镜像和 NVIDIA 官方文档为准。

```bash
cp .env.example .env
docker compose up -d model
docker compose ps
docker compose logs -f model
```

服务首次加载模型需要等待。健康检查通过后，在心晴中设置：地址 `http://127.0.0.1:8000/v1`，模型名 `Qwen2.5-14B-Instruct`。`.env` 中的 `MODEL_API_KEY` 与客户端填写的 Key 保持一致。

默认镜像固定为 `vllm/vllm-openai:v0.19.0`，模型挂载只读，推理禁止联网补下载。`GPU_COUNT` 同时控制容器申请的 GPU 数和张量并行数。单卡部署可改成 1，但必须有足够显存；不要因为容器能启动就认为 14B BF16 能放进 8GB/16GB 显卡。

`MODEL_DIR` 指向宿主机权重目录；`MODEL_MAX_LEN` 默认 8192，主要限制上下文缓存用量，并非模型训练上下文的声明。

默认端口只绑定 `127.0.0.1`。若客户端在另一台电脑，优先使用 SSH 转发：

```bash
ssh -L 8000:127.0.0.1:8000 your-user@your-gpu-server
```

保持该连接后，客户端仍填写本机地址。不要直接把无鉴权的模型端口开放到公网。

停止：`docker compose down`。模型目录和批量结果位于宿主机，不会因为停止容器而删除。

## 检查与故障

```bash
curl -H "Authorization: Bearer 你在env中填写的Key" http://127.0.0.1:8000/v1/models
```

找不到模型：核对挂载目录和分片是否完整。显存不足：检查实际空闲显存、并行数和上下文长度，或者改用 API。`401`：核对密钥；网络可达但不能生成：检查模型名、服务日志和 API 对流式 Chat Completions 的支持。

Docker GPU 服务需要在有模型和 GPU 的机器上实测。仓库中的本地检查不代表已经完成该硬件验证。
