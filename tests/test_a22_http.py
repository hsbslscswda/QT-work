"""Exercise the preserved A22 CLI through a local HTTP fixture, without a model."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]


class A22HttpTest(unittest.TestCase):
    def test_preserved_files(self):
        folder = ROOT / 'backend/a22'
        manifest = json.loads((folder / 'SHA256SUMS.json').read_text())
        for name, digest in manifest.items():
            self.assertEqual(hashlib.sha256((folder / name).read_bytes()).hexdigest(), digest, name)

    def test_cli_http_and_output_contract(self):
        requests = []

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                requests.append((self.path, self.headers.get('Authorization'), body))
                prompt = body['messages'][0]['content']
                if '只判断最新用户消息此刻' in prompt:
                    prediction = {'emotion_label': 'joy'}
                elif 'retain_sensitive' in prompt:
                    prediction = {'retain_sensitive': False}
                else:
                    prediction = {
                        'response_text': '这是本地测试服务的合成回复。',
                        'emotion_label': 'neutral',
                        'user_profile': {'personality_traits': ['sensitive'], 'interests': [], 'style': []},
                        'memory_refs': [],
                    }
                output = json.dumps({'choices': [{'message': {'content': json.dumps(prediction, ensure_ascii=False)}}]}).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(output)))
                self.end_headers()
                self.wfile.write(output)

        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as temporary:
                out = Path(temporary) / 'run'
                env = dict(os.environ, EVALUATION_BACKEND='openai_compatible',
                           EVALUATION_BASE_URL=f'http://127.0.0.1:{server.server_port}/team/v1',
                           EVALUATION_API_MODEL='fixture', EVALUATION_API_KEY='fixture-key',
                           NO_PROXY='127.0.0.1,localhost', no_proxy='127.0.0.1,localhost')
                run = subprocess.run([sys.executable, str(ROOT / 'backend/a22/run_inference.py'),
                                      str(ROOT / 'examples/chat.jsonl'), str(out)],
                                     env=env, text=True, capture_output=True, timeout=20)
                self.assertEqual(run.returncode, 0, run.stderr)
                pred = json.loads((out / 'submission.jsonl').read_text())
                self.assertEqual(pred['emotion_label'], 'joy')
                self.assertEqual(pred['user_profile']['personality_traits'], [])
                report = json.loads((out / 'performance_report.json').read_text())
                self.assertFalse(report['complete'])
                self.assertEqual(report['samples'], 1)
                self.assertEqual(len(requests), 3)
                for path, authorization, payload in requests:
                    self.assertEqual(path, '/team/v1/chat/completions')
                    self.assertEqual(authorization, 'Bearer fixture-key')
                    self.assertEqual(payload['model'], 'fixture')
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


if __name__ == '__main__':
    unittest.main()
