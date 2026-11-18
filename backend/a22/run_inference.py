#!/usr/bin/env python3
"""Run local Transformers or OpenAI-compatible inference and timing in one pass.

Public CLI contract: python run_inference.py TEST_FILE RESULT_DIR
"""
# Responsibility: validate CLI/configuration, run A22 inference channels, normalize
# official output, and record first-100 end-to-end latency. No training occurs here.
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import statistics
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parent
CONFIG_PATH = ROOT / "configs" / "inference_config.json"
EMOTIONS = {
    "joy", "gratitude", "relaxed", "care", "pride", "neutral", "surprise",
    "mixed", "sadness", "loneliness", "anxiety", "anger", "fear", "disgust",
    "shame", "helplessness",
}
PROFILE_LABELS = {
    "personality_traits": {"extroverted", "introverted", "open", "conservative", "high_conscientiousness", "casual", "agreeable", "assertive", "emotionally_stable", "sensitive"},
    "interests": {"study_exam", "programming_technology", "reading_writing", "film_animation", "music", "games", "sports_fitness", "travel_outdoor", "pets", "social", "career_development", "art_design"},
    "style": {"brief", "detailed", "colloquial", "formal", "direct", "indirect", "humorous", "rational", "high_emotional_expression", "low_emotional_expression", "emoji_user"},
}
MEMORY_RE = re.compile(r"^mem_[0-9]{6}$")
PROMPT_OVERRIDE_PATH = ROOT / "configs" / "system_prompt.txt"
SENSITIVE_GATE_PROMPT_PATH = ROOT / "configs" / "sensitive_gate_prompt.txt"


def load_system_prompt() -> str:
    """优先读取外部预渲染纯文本提示词（已内联枚举、正文含 JSON 花括号）。
    存在则原样返回，严禁再走 str.format —— 否则 JSON 花括号会触发格式化错误；
    覆盖文件缺失时，回退官方内置 SYSTEM_PROMPT 的 .format 逻辑，保证与原版一致。"""
    if PROMPT_OVERRIDE_PATH.is_file():
        return PROMPT_OVERRIDE_PATH.read_text(encoding="utf-8").strip()
    return SYSTEM_PROMPT.format(
        emotions=", ".join(EMOTIONS),
        personality=", ".join(sorted(PROFILE_LABELS["personality_traits"])),
        interests=", ".join(sorted(PROFILE_LABELS["interests"])),
        styles=", ".join(sorted(PROFILE_LABELS["style"])),
    )


def load_sensitive_gate_prompt() -> str:
    """Prompt used only to decide whether an existing sensitive label is retained."""
    if not SENSITIVE_GATE_PROMPT_PATH.is_file():
        raise FileNotFoundError(f"缺少 sensitive 复核提示词: {SENSITIVE_GATE_PROMPT_PATH}")
    return SENSITIVE_GATE_PROMPT_PATH.read_text(encoding="utf-8").strip()


SYSTEM_PROMPT = """你正在参加数字情感陪伴比赛。根据截止到最新用户消息的对话历史，完成下一轮 Assistant 回复预测。

只输出一个 JSON object，不要输出 Markdown 或解释。object 必须且只能包含：
- response_text: 非空中文回复，应自然、贴合上下文和用户偏好；
- emotion_label: 最新用户消息的情绪，只能从 {emotions} 中选择；
- user_profile: object，包含 personality_traits、interests、style 三个字符串数组；
- memory_refs: 固定输出 []，因为测试集没有公开 memory bank。

画像标签只能使用以下枚举；没有充分证据时宁可输出空数组：
personality_traits: {personality}
interests: {interests}
style: {styles}
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="本地模型单轮推理并计时")
    parser.add_argument("test_file", type=Path, help="具体测试 JSONL 文件")
    parser.add_argument("result_dir", type=Path, help="结果输出目录")
    return parser.parse_args()


# Input IDs are preserved verbatim, including dev/val IDs in local experiments.
# Uniqueness is checked within a file; each row supplies its own complete history.
def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open(encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{number}: JSON 解析失败: {exc.msg}") from exc
            if not isinstance(row, dict) or not isinstance(row.get("id"), str) or not isinstance(row.get("history"), list):
                raise ValueError(f"{path}:{number}: 必须包含字符串 id 和数组 history")
            rows.append(row)
    if not rows:
        raise ValueError(f"测试文件为空: {path}")
    if len({row["id"] for row in rows}) != len(rows):
        raise ValueError("测试文件包含重复 id")
    return rows


def validate_test_file(test_file: Path) -> Path:
    if not test_file.is_file():
        raise ValueError(f"测试文件不存在或不是文件: {test_file}")
    if test_file.suffix.lower() != ".jsonl":
        raise ValueError(f"测试文件必须使用 .jsonl 扩展名: {test_file}")
    return test_file


def prepare_result_dir(path: Path) -> None:
    if path.exists() and not path.is_dir():
        raise ValueError(f"结果路径已存在但不是目录: {path}")
    path.mkdir(parents=True, exist_ok=True)


def load_config() -> dict[str, Any]:
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    environment_overrides = {
        "EVALUATION_BACKEND": "backend",
        "EVALUATION_BASE_URL": "base_url",
        "EVALUATION_API_MODEL": "api_model",
        "EVALUATION_API_KEY": "api_key",
    }
    for environment_name, config_name in environment_overrides.items():
        if environment_name in os.environ and (
            config_name == "api_key" or os.environ[environment_name]
        ):
            config[config_name] = os.environ[environment_name]
    if os.environ.get("EVALUATION_MODEL_PATH"):
        config["model_path"] = os.environ["EVALUATION_MODEL_PATH"]
    if os.environ.get("EVALUATION_HARDWARE_LABEL"):
        config["hardware_label"] = os.environ["EVALUATION_HARDWARE_LABEL"]
    config.setdefault("backend", "transformers")
    required = {"backend", "generation", "performance_rounds", "hardware_label"}
    if not required <= set(config):
        raise ValueError(f"推理配置缺少字段: {sorted(required - set(config))}")
    if config["backend"] not in {"transformers", "openai_compatible"}:
        raise ValueError("backend 必须为 transformers 或 openai_compatible")
    if config["backend"] == "transformers" and not config.get("model_path"):
        raise ValueError("transformers 模式必须配置 model_path")
    if config["backend"] == "openai_compatible":
        missing = [name for name in ("base_url", "api_model") if not config.get(name)]
        if missing:
            raise ValueError(f"openai_compatible 模式缺少字段: {missing}")
        api_endpoint(config["base_url"])
        timeout = config.get("request_timeout_seconds", 600)
        if not isinstance(timeout, (int, float)) or isinstance(timeout, bool) or timeout <= 0:
            raise ValueError("request_timeout_seconds 必须为正数")
        if not isinstance(config.get("api_options", {}), dict):
            raise ValueError("api_options 必须为 JSON object")
    if not isinstance(config["performance_rounds"], int) or config["performance_rounds"] <= 0:
        raise ValueError("performance_rounds 必须为正整数")
    return config


def model_path(config: dict[str, Any]) -> Path:
    path = Path(config["model_path"])
    resolved = path if path.is_absolute() else ROOT / path
    if not resolved.exists():
        raise FileNotFoundError(f"本地模型目录不存在: {resolved}")
    return resolved.resolve()


def api_endpoint(base_url: str) -> str:
    if not isinstance(base_url, str):
        raise ValueError("base_url 必须为字符串")
    normalized = base_url.rstrip("/")
    parsed = urlparse(normalized)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ValueError(f"base_url 必须是有效的 HTTP(S) URL: {base_url}")
    if normalized.endswith("/chat/completions"):
        return normalized
    return normalized + "/chat/completions"


def api_generation_options(generation: dict[str, Any]) -> dict[str, Any]:
    allowed = {
        "temperature", "top_p", "stop", "seed", "frequency_penalty",
        "presence_penalty", "min_p", "top_k",
    }
    options = {name: value for name, value in generation.items() if name in allowed}
    if "max_new_tokens" in generation:
        options["max_tokens"] = generation["max_new_tokens"]
    elif "max_tokens" in generation:
        options["max_tokens"] = generation["max_tokens"]
    if generation.get("do_sample") is False:
        options["temperature"] = 0
    return options


def response_content(payload: Any) -> str:
    try:
        choice = payload["choices"][0]
        content = choice["message"]["content"]
    except (KeyError, IndexError, TypeError) as exc:
        raise ValueError("API 响应缺少 choices[0].message.content") from exc
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts = []
        for item in content:
            if isinstance(item, str):
                parts.append(item)
            elif isinstance(item, dict) and isinstance(item.get("text"), str):
                parts.append(item["text"])
        if parts:
            return "".join(parts)
    raise ValueError("API 响应的 message.content 必须为字符串或文本片段数组")


# Transport only: assemble the local OpenAI-compatible request and extract text.
# Network/HTTP failures remain explicit. Main-channel and gate errors currently
# propagate; review-channel fallbacks below deliberately preserve the base output.
def request_chat_completion(
    config: dict[str, Any],
    messages: list[dict[str, str]],
    generation: dict[str, Any],
) -> str:
    reserved = {"model", "messages"}
    api_options = dict(config.get("api_options", {}))
    if reserved & set(api_options):
        raise ValueError(f"api_options 不得覆盖字段: {sorted(reserved & set(api_options))}")
    payload = {
        "model": config["api_model"],
        "messages": messages,
        **api_generation_options(generation),
        **api_options,
    }
    headers = {"Content-Type": "application/json"}
    api_key = config.get("api_key", "")
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"
    request = urllib.request.Request(
        api_endpoint(config["base_url"]),
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers=headers,
        method="POST",
    )
    try:
        with urllib.request.urlopen(
            request,
            timeout=float(config.get("request_timeout_seconds", 600)),
        ) as response:
            raw_response = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")[:1000]
        raise RuntimeError(f"API 请求失败: HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"无法连接本地 Base URL: {exc.reason}") from exc
    try:
        return response_content(json.loads(raw_response))
    except json.JSONDecodeError as exc:
        raise ValueError("API 响应不是有效 JSON") from exc


def _retrieval_terms(text: str) -> set[str]:
    """Use Chinese bigrams/trigrams plus alphanumeric words, not whole sentences."""
    text = text.lower()
    stop = {"用户", "觉得", "现在", "真的", "事情", "可能", "已经", "还是", "然后", "但是", "因为", "这个", "自己"}
    terms: set[str] = set(re.findall(r"[a-z0-9_]{2,}", text))
    for run in re.findall(r"[\u4e00-\u9fff]+", text):
        if len(run) >= 2:
            terms.update(run[i:i + 2] for i in range(len(run) - 1))
        if len(run) >= 3:
            terms.update(run[i:i + 3] for i in range(len(run) - 2))
    return {term for term in terms if term not in stop and len(term) >= 2}


def original_conversation_text(sample: dict[str, Any]) -> str:
    """A18-compatible context used by the main response/emotion channel."""
    rendered = []
    for turn in sample["history"]:
        if not isinstance(turn, dict) or turn.get("role") not in {"user", "assistant"} or not isinstance(turn.get("content"), str):
            raise ValueError(f"样本 {sample['id']} 的 history 格式无效")
        rendered.append(f"{turn['role']}: {turn['content']}")
    return "样本 ID: " + sample["id"] + "\n对话历史:\n" + "\n".join(rendered)


def recent_emotion_context(sample: dict[str, Any]) -> str:
    """Latest user message plus the recent local dialogue window."""
    history = sample["history"]
    recent = history[-6:]
    rendered = [f"{turn['role']}: {turn['content']}" for turn in recent]
    latest_user = next(
        (turn["content"] for turn in reversed(history) if turn.get("role") == "user"),
        "",
    )
    return "最新用户消息:\n" + latest_user + "\n\n最近对话:\n" + "\n".join(rendered)


def conversation_context(sample: dict[str, Any]) -> tuple[str, dict[str, Any]]:
    rendered = []
    for turn in sample["history"]:
        if not isinstance(turn, dict) or turn.get("role") not in {"user", "assistant"} or not isinstance(turn.get("content"), str):
            raise ValueError(f"样本 {sample['id']} 的 history 格式无效")
        rendered.append(f"{turn['role']}: {turn['content']}")
    user_turns = [(i, t["content"]) for i, t in enumerate(sample["history"])
                  if t.get("role") == "user" and isinstance(t.get("content"), str)]
    latest = user_turns[-1][1] if user_turns else ""
    query_terms = _retrieval_terms(latest)
    scored = []
    for i, content in user_turns[:-1]:
        terms = _retrieval_terms(content)
        overlap = len(query_terms & terms)
        recency = i / max(1, len(sample["history"]) - 1)
        score = overlap * 3.0 + recency
        if overlap:
            scored.append((score, i, content, overlap))
    scored.sort(reverse=True)
    # Require two shared terms and at least one 3-gram before injecting history.
    selected = [item for item in scored if item[3] >= 2 and
                any(len(term) >= 3 for term in query_terms & _retrieval_terms(item[2]))][:3]
    fallback = not selected
    recent = rendered[-6:]
    relevant = [f"user: {content}" for _, _, content, _ in selected]
    if fallback:
        text = "样本 ID: " + sample["id"] + "\n对话历史:\n" + "\n".join(rendered)
    else:
        text = ("样本 ID: " + sample["id"] + "\n"
            "当前用户问题:\n" + latest + "\n\n"
            "最近对话（必须优先参考）:\n" + "\n".join(recent) + "\n\n"
            "相关历史片段（仅用于判断 user_profile 的长期证据；不得用于改变当前 emotion_label 或 response_text）:\n" +
            ("\n".join(relevant) if relevant else "（未检索到明确相关片段）") + "\n\n"
            "完整对话历史:\n" + "\n".join(rendered))
    audit = {"id": sample["id"], "query_terms": sorted(query_terms),
             "selected_user_turns": [i for _, i, _, _ in selected],
             "selected_overlap": [overlap for _, _, _, overlap in selected],
             "fallback_to_a18": fallback}
    return text, audit


def extract_object(text: str) -> dict[str, Any]:
    text = re.sub(r"<think>.*?</think>", "", text, flags=re.S).strip()
    fenced = re.search(r"```(?:json)?\s*(\{.*?\})\s*```", text, flags=re.S | re.I)
    candidate = fenced.group(1) if fenced else text
    decoder = json.JSONDecoder()
    for match in re.finditer(r"\{", candidate):
        try:
            value, _ = decoder.raw_decode(candidate[match.start():])
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            return value
    raise ValueError("模型输出中没有可解析的 JSON object")


def recover_response_text(text: str) -> str:
    """Preserve useful model text when its surrounding JSON is malformed."""
    cleaned = re.sub(r"<think>.*?</think>", "", text, flags=re.S).strip()
    cleaned = re.sub(r"^```(?:json)?\s*|\s*```$", "", cleaned, flags=re.I).strip()
    match = re.search(
        r'"response_text"\s*:\s*"((?:\\.|[^"\\])*)',
        cleaned,
        flags=re.S,
    )
    if match:
        try:
            recovered = json.loads(f'"{match.group(1)}"')
            if isinstance(recovered, str):
                return recovered.strip()
        except json.JSONDecodeError:
            pass
    return cleaned


# This is the final shared schema boundary: filter labels to known enums,
# preserve the input ID, normalize each profile dimension, and validate memory IDs.
def normalize_prediction(sample_id: str, raw: dict[str, Any]) -> dict[str, Any]:
    response = raw.get("response_text")
    emotion = raw.get("emotion_label")
    profile = raw.get("user_profile")
    memories = raw.get("memory_refs")
    clean_response = response.strip() if isinstance(response, str) else ""
    emotion_aliases = {"disappointment": "sadness", "confidence": "pride"}
    mapped_emotion = emotion_aliases.get(emotion, emotion) if isinstance(emotion, str) else None
    clean_emotion = mapped_emotion if mapped_emotion in EMOTIONS else ""
    profile = profile if isinstance(profile, dict) else {}
    clean_profile: dict[str, list[str]] = {}
    for name, allowed in PROFILE_LABELS.items():
        values = profile.get(name)
        valid = (
            isinstance(values, list)
            and all(isinstance(value, str) for value in values)
            and len(values) == len(set(values))
            and set(values) <= allowed
        )
        clean_profile[name] = values if valid else []
    valid_memories = (
        isinstance(memories, list)
        and all(isinstance(value, str) and MEMORY_RE.fullmatch(value) for value in memories)
        and len(memories) == len(set(memories))
    )
    return {
        "id": sample_id,
        "response_text": clean_response,
        "emotion_label": clean_emotion,
        "user_profile": clean_profile,
        "memory_refs": memories if valid_memories else [],
    }


def parse_prediction(sample_id: str, decoded: str) -> tuple[dict[str, Any], bool]:
    try:
        raw_prediction = extract_object(decoded)
        parse_failed = False
    except ValueError:
        raw_prediction = {
            "response_text": recover_response_text(decoded),
            "emotion_label": "",
            "user_profile": {},
            "memory_refs": [],
        }
        parse_failed = True
    return normalize_prediction(sample_id, raw_prediction), parse_failed


def normalize_style(raw: Any) -> list[str] | None:
    """Return a legal style array, or None when the style-only call is unusable."""
    if not isinstance(raw, dict):
        return None
    values = raw.get("style", raw.get("user_profile", {}).get("style") if isinstance(raw.get("user_profile"), dict) else None)
    allowed = PROFILE_LABELS["style"]
    if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
        return None
    if len(values) != len(set(values)) or not set(values) <= allowed or len(values) > 2:
        return None
    return values


def parse_style_prediction(decoded: str) -> tuple[list[str] | None, bool]:
    try:
        payload = extract_object(decoded)
    except ValueError:
        return None, True
    style = normalize_style(payload)
    return style, style is None


def parse_sensitive_gate(decoded: str) -> tuple[bool | None, bool]:
    """Return the retain decision, or None when the gate response is unusable."""
    try:
        payload = extract_object(decoded)
    except ValueError:
        return None, True
    decision = payload.get("retain_sensitive") if isinstance(payload, dict) else None
    if not isinstance(decision, bool):
        return None, True
    return decision, False


def synchronize(torch_module: Any) -> None:
    if torch_module.cuda.is_available():
        torch_module.cuda.synchronize()


def percentile95(values: list[float]) -> float:
    ordered = sorted(values)
    return ordered[max(0, (95 * len(ordered) + 99) // 100 - 1)]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    args = parse_args()
    test_file = validate_test_file(args.test_file.resolve())
    result_dir = args.result_dir.resolve()
    prepare_result_dir(result_dir)
    config = load_config()
    backend = config["backend"]
    backend_report: dict[str, Any]

    if backend == "transformers":
        local_model = model_path(config)
        try:
            import torch
            from transformers import (
                AutoConfig,
                AutoModelForCausalLM,
                AutoModelForImageTextToText,
                AutoTokenizer,
            )
            from transformers.models.auto.modeling_auto import (
                MODEL_FOR_IMAGE_TEXT_TO_TEXT_MAPPING_NAMES,
            )
        except ImportError as exc:
            raise RuntimeError("请先安装 participant/requirements.txt") from exc

        trust_remote_code = bool(config.get("trust_remote_code", False))
        model_config = AutoConfig.from_pretrained(
            local_model,
            trust_remote_code=trust_remote_code,
            local_files_only=True,
        )
        model_loader = (
            AutoModelForImageTextToText
            if model_config.model_type in MODEL_FOR_IMAGE_TEXT_TO_TEXT_MAPPING_NAMES
            else AutoModelForCausalLM
        )
        tokenizer = AutoTokenizer.from_pretrained(local_model, trust_remote_code=trust_remote_code, local_files_only=True)
        model = model_loader.from_pretrained(
            local_model,
            trust_remote_code=trust_remote_code,
            local_files_only=True,
            device_map=config.get("device_map", "auto"),
            torch_dtype=config.get("torch_dtype", "auto"),
        )
        model.eval()
        backend_report = {
            "version": "transformers_inference_v2",
            "timing_scope": "tokenize+generate+decode+parse+validate; model loading excluded",
            "model_path_name": local_model.name,
            "model_type": model_config.model_type,
            "model_loader": model_loader.__name__,
        }
    else:
        backend_report = {
            "version": "openai_compatible_inference_v2",
            "timing_scope": "HTTP request+server inference+response decode+parse+validate; server startup excluded",
            "api_model": config["api_model"],
            "base_url": config["base_url"],
        }

    samples = read_jsonl(test_file)
    submission_path = result_dir / "submission.jsonl"
    submission_path.write_text("", encoding="utf-8")
    latencies: list[float] = []
    primary_latencies: list[float] = []
    gate_latencies: list[float] = []
    predictions: list[dict[str, Any]] = []
    parse_failure_ids: list[str] = []
    gate_parse_failure_ids: list[str] = []
    generation = dict(config["generation"])
    gate_generation = dict(config.get("gate_generation", config.get("style_generation", generation)))

    system_prompt = load_system_prompt()
    gate_prompt = load_sensitive_gate_prompt()
    audit_path = result_dir / "sensitive_gate_audit.jsonl"
    audit_path.write_text("", encoding="utf-8")
    retrieval_audit_path = result_dir / "a22_channel_audit.jsonl"
    retrieval_audit_path.write_text("", encoding="utf-8")

    def generate(messages: list[dict[str, str]], options: dict[str, Any]) -> str:
        if backend == "transformers":
            prompt = tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
            synchronize(torch)
            inputs = tokenizer(prompt, return_tensors="pt")
            input_device = next(model.parameters()).device
            inputs = {name: tensor.to(input_device) for name, tensor in inputs.items()}
            with torch.inference_mode():
                output_ids = model.generate(**inputs, **options)
            generated = output_ids[0, inputs["input_ids"].shape[1]:]
            return tokenizer.decode(generated, skip_special_tokens=True)
        return request_chat_completion(config, messages, options)

    for index, sample in enumerate(samples, 1):
        context_text, retrieval_audit = conversation_context(sample)
        # Channel 1: keep the full A18 conversation for the response. Retrieved
        # history is never substituted into this main request.
        main_context = original_conversation_text(sample)
        messages = [{"role": "system", "content": system_prompt}, {"role": "user", "content": main_context}]
        started = time.perf_counter()
        decoded = generate(messages, generation)
        prediction, parse_failed = parse_prediction(sample["id"], decoded)
        if parse_failed:
            parse_failure_ids.append(sample["id"])
        if backend == "transformers":
            synchronize(torch)
        primary_elapsed_ms = (time.perf_counter() - started) * 1000

        main_emotion = prediction["emotion_label"]
        emotion_review = ""
        emotion_review_failed = False
        emotion_overridden = False
        emotion_prompt = (
            "只判断最新用户消息此刻的主要情绪。只输出一个 JSON object，且只能包含 emotion_label。"
            "emotion_label 必须从以下枚举选择：" + ", ".join(sorted(EMOTIONS)) + "。"
            "优先依据最新用户消息，最近对话只用于消歧，不得依据更早情绪。"
        )
        # Channel 2: review the latest emotion using only the recent window.
        # A valid enum overrides the main emotion; malformed/failed review keeps it.
        try:
            emotion_decoded = generate([
                {"role": "system", "content": emotion_prompt},
                {"role": "user", "content": recent_emotion_context(sample)},
            ], gate_generation)
            emotion_raw = extract_object(emotion_decoded).get("emotion_label")
            emotion_aliases = {"disappointment": "sadness", "confidence": "pride"}
            emotion_review = emotion_aliases.get(emotion_raw, emotion_raw) if isinstance(emotion_raw, str) else ""
            if emotion_review in EMOTIONS:
                prediction["emotion_label"] = emotion_review
                emotion_overridden = emotion_review != main_emotion
            else:
                emotion_review_failed = True
        except Exception:
            emotion_review_failed = True

        # Channel 3 is conditional: require reliable historical n-gram overlap.
        # Non-empty, normalized dimensions replace the baseline dimension; empty
        # dimensions do not erase baseline evidence. This is not external RAG.
        profile_called = not retrieval_audit["fallback_to_a18"]
        profile_failed = False
        profile_overridden: list[str] = []
        if profile_called:
            profile_prompt = (
                "只判断用户长期画像。只输出一个 JSON object，且只能包含 user_profile；"
                "user_profile 必须包含 personality_traits、interests、style 三个数组。"
                "仅使用相关历史中的稳定、重复证据；没有充分证据的维度输出空数组。"
                "不得输出 response_text、emotion_label 或解释。"
            )
            profile_messages = [
                {"role": "system", "content": profile_prompt},
                {"role": "user", "content": context_text},
            ]
            try:
                profile_decoded = generate(profile_messages, gate_generation)
                profile_raw = extract_object(profile_decoded)
                profile_candidate = normalize_prediction(sample["id"], profile_raw)["user_profile"]
                for dimension in ("personality_traits", "interests", "style"):
                    if profile_candidate[dimension]:
                        prediction["user_profile"][dimension] = profile_candidate[dimension]
                        profile_overridden.append(dimension)
            except Exception:
                profile_failed = True

        # Apply sensitive gate AFTER optional profile merge, so both sources
        # share the same safeguard. Only explicit False removes the label; an
        # unparseable decision retains it. Transport failure still propagates.
        baseline_personality = list(prediction["user_profile"]["personality_traits"])
        gate_called = "sensitive" in baseline_personality
        gate_decision: bool | None = None
        gate_failed = False
        gate_elapsed_ms = 0.0
        if gate_called:
            gate_messages = [
                {"role": "system", "content": gate_prompt},
                {"role": "user", "content": main_context},
            ]
            gate_started = time.perf_counter()
            gate_decoded = generate(gate_messages, gate_generation)
            gate_decision, gate_failed = parse_sensitive_gate(gate_decoded)
            if backend == "transformers":
                synchronize(torch)
            gate_elapsed_ms = (time.perf_counter() - gate_started) * 1000
            if gate_failed:
                gate_parse_failure_ids.append(sample["id"])
            elif gate_decision is False:
                prediction["user_profile"]["personality_traits"] = [
                    label for label in baseline_personality if label != "sensitive"
                ]
        if backend == "transformers":
            synchronize(torch)
        # Include all channels and normalization in per-sample elapsed time;
        # performance reporting uses the first N rows (normally 100), while
        # submission.jsonl covers every input row. Server startup is excluded.
        elapsed_ms = (time.perf_counter() - started) * 1000
        if index <= config["performance_rounds"]:
            latencies.append(elapsed_ms)
            primary_latencies.append(primary_elapsed_ms)
            if gate_called:
                gate_latencies.append(gate_elapsed_ms)
        predictions.append(prediction)
        # Append one official prediction plus separate diagnostic audit records.
        # Audits are evidence; they must never be reused as answers for new input.
        with submission_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(prediction, ensure_ascii=False, separators=(",", ":")) + "\n")
        with audit_path.open("a", encoding="utf-8") as handle:
            audit = {
                "id": sample["id"],
                "main_parse_failed": parse_failed,
                "gate_called": gate_called,
                "gate_parse_failed": gate_failed,
                "main_personality_traits": baseline_personality,
                "retain_sensitive": gate_decision,
                "sensitive_removed": gate_called and gate_decision is False,
                "primary_latency_ms": round(primary_elapsed_ms, 6),
                "gate_latency_ms": round(gate_elapsed_ms, 6),
            }
            handle.write(json.dumps(audit, ensure_ascii=False, separators=(",", ":")) + "\n")
        with retrieval_audit_path.open("a", encoding="utf-8") as handle:
            retrieval_audit.update({
                "response_context": "a18_original",
                "emotion_context": "recent_only",
                "main_emotion": main_emotion,
                "emotion_review": emotion_review,
                "emotion_review_failed": emotion_review_failed,
                "emotion_overridden": emotion_overridden,
                "profile_called": profile_called,
                "profile_failed": profile_failed,
                "profile_overridden": profile_overridden,
            })
            handle.write(json.dumps(retrieval_audit, ensure_ascii=False, separators=(",", ":")) + "\n")
        print(f"[{index}/{len(samples)}] {sample['id']}: {elapsed_ms:.3f} ms", flush=True)

    report = {
        **backend_report,
        "backend": backend,
        "complete": len(latencies) == config["performance_rounds"],
        "required_rounds": config["performance_rounds"],
        "rounds": len(latencies),
        "average_latency_ms": round(statistics.fmean(latencies), 6),
        "median_latency_ms": round(statistics.median(latencies), 6),
        "p95_latency_ms": round(percentile95(latencies), 6),
        "min_latency_ms": round(min(latencies), 6),
        "max_latency_ms": round(max(latencies), 6),
        "latencies_ms": [round(value, 6) for value in latencies],
        "hardware_label": config["hardware_label"],
        "test_file_sha256": sha256(test_file),
        "submission_sha256": sha256(submission_path),
        "samples": len(predictions),
        "parse_failure_count": len(parse_failure_ids),
        "parse_failure_ids": parse_failure_ids,
        "sensitive_gate_parse_failure_count": len(gate_parse_failure_ids),
        "sensitive_gate_parse_failure_ids": gate_parse_failure_ids,
        "primary_average_latency_ms": round(statistics.fmean(primary_latencies), 6),
        "sensitive_gate_average_latency_ms": round(statistics.fmean(gate_latencies), 6) if gate_latencies else None,
    }
    (result_dir / "performance_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {submission_path}")
    print(f"PASS: {result_dir / 'performance_report.json'}")


if __name__ == "__main__":
    main()

