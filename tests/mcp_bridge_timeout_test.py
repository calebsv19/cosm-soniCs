"""Qualify the adapter's real Unix-socket wait and original-ID recovery without a user endpoint."""
import importlib.util
import json
import os
from pathlib import Path
import socket
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]

class SlowPeer:
    """Serialize a slow capture and its receipt lookup like a busy control owner."""
    def __init__(self, path, delay=6.2):
        self.delay = delay
        self.calls = []
        self.receipts = {}
        self.errors = []
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(4)
        self.server.settimeout(.1)
        self.closed = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        """Retain terminal receipts even when the sender abandons its response socket."""
        while not self.closed.is_set():
            try:
                connection, _ = self.server.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                with connection:
                    connection.settimeout(3)
                    raw = bytearray()
                    while b'\n' not in raw:
                        chunk = connection.recv(8192)
                        if not chunk:
                            raise RuntimeError('incomplete private request')
                        raw.extend(chunk)
                    payload = json.loads(raw.split(b'\n', 1)[0])
                    self.calls.append(payload)
                    key = (payload.get('instance'), payload.get('request_id'))
                    if payload['op'] == 'capture_ab':
                        time.sleep(self.delay)
                        result = {'ok': True, 'code': 'captured', 'comparison_id': 'private-A',
                                  'instance': key[0], 'request_id': key[1]}
                        self.receipts[key] = result
                    else:
                        result = self.receipts.get(key, {'ok': False, 'code': 'unknown_request'})
                    try:
                        connection.sendall(json.dumps(result).encode() + b'\n')
                    except (BrokenPipeError, ConnectionResetError):
                        pass  # Completion is retained when delivery is lost.
            except Exception as error:
                self.errors.append(repr(error))

    def close(self):
        """Retire only this test's endpoint after queued work and recovery finish."""
        self.closed.set()
        self.thread.join(timeout=10)
        self.server.close()
        if self.thread.is_alive():
            raise RuntimeError('private peer did not retire')

class BridgeTimeoutTest(unittest.TestCase):
    """Exercise the production client using delayed private transport and injected uncertainty."""
    @classmethod
    def setUpClass(cls):
        """Import the real adapter without starting its stdio server or an application."""
        spec = importlib.util.spec_from_file_location('timeout_adapter', ROOT/'tools/mcp/server.py')
        cls.adapter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.adapter)

    def test_operation_budgets(self):
        """Keep ordinary controls short and existing diagnostic budgets unchanged."""
        for op, budget in [('capture_ab', 60), ('request_status', 60),
                           ('analyze_composition', 60), ('capture_mix', 180),
                           ('get_ab_comparison', 5), ('edit_effect_chain', 5), (None, 5)]:
            self.assertEqual(self.adapter.bridge_timeout(op), budget)

    def test_slow_capture_exceeds_old_budget(self):
        """Allow a real six-second response instead of declaring a five-second transport failure."""
        with tempfile.TemporaryDirectory(prefix='sonics-ab-timeout-') as root:
            path = str(Path(root)/'control.sock')
            peer = SlowPeer(path)
            try:
                payload = dict(op='capture_ab', instance='private-instance', request_id='slow-original',
                               expected_mix='private-mix', expected_comparison='empty', side='A',
                               start_frame='1200000', end_frame='1632000', tail_frames='48000')
                with patch.dict(os.environ, DAW_CONTROL_SOCKET=path):
                    start = time.monotonic()
                    result = self.adapter.bridge_call(payload)
                elapsed = time.monotonic() - start
                self.assertTrue(result['ok'])
                self.assertGreater(elapsed, 5)
                self.assertLess(elapsed, 60)
                self.assertEqual(peer.calls, [payload])
                self.assertEqual(peer.errors, [])
            finally:
                peer.close()

    def test_lost_response_recovers_original_id_after_owner_finishes(self):
        """An uncertain sender never replays; same-ID status can wait behind the slow capture."""
        with tempfile.TemporaryDirectory(prefix='sonics-ab-recovery-') as root:
            path = str(Path(root)/'control.sock')
            peer = SlowPeer(path)
            budget = self.adapter.bridge_timeout
            payload = dict(op='capture_ab', instance='private-instance', request_id='lost-original',
                           expected_mix='private-mix', expected_comparison='empty', side='A',
                           start_frame='1200000', end_frame='1632000', tail_frames='48000')
            try:
                with patch.dict(os.environ, DAW_CONTROL_SOCKET=path):
                    # Inject an early sender timeout; the production recovery budget stays real.
                    with patch.object(self.adapter, 'bridge_timeout', side_effect=lambda op: .1 if op=='capture_ab' else budget(op)):
                        with self.assertRaisesRegex(RuntimeError, 'request_status'):
                            self.adapter.bridge_call(payload)
                    start = time.monotonic()
                    result = self.adapter.request_status(payload['instance'], payload['request_id'])
                    elapsed = time.monotonic() - start
                self.assertGreater(elapsed, 5)
                self.assertLess(elapsed, 60)
                self.assertEqual(result, peer.receipts[('private-instance', 'lost-original')])
                self.assertEqual([p['op'] for p in peer.calls], ['capture_ab', 'request_status'])
                self.assertEqual(peer.calls[1]['request_id'], payload['request_id'])
                self.assertEqual(peer.calls[1]['instance'], payload['instance'])
                self.assertEqual(peer.errors, [])
            finally:
                peer.close()

if __name__ == '__main__':
    unittest.main()
