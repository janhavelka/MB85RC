"""Device-free framing and payload gates for the maintained HIL collector."""
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import hil_runner as runner

class Clock:
    def __init__(self):self.now=0.
    def monotonic(self):return self.now
    def sleep(self,seconds):self.now+=seconds

class Port:
    def __init__(self,chunks,clock,short=False):self.chunks=list(chunks);self.clock=clock;self.writes=[];self.short=short
    @property
    def in_waiting(self):return len(self.chunks[0]) if self.chunks and not isinstance(self.chunks[0],Exception) else 0
    def read(self,_):
        self.clock.sleep(.01)
        if self.chunks:
            data=self.chunks.pop(0)
            if isinstance(data,Exception):raise data
            return data
        return b''
    def write(self,data):self.writes.append(data);return len(data)-1 if self.short else len(data)
    def flush(self):raise AssertionError('Unbounded serial.flush must never run')

def session(folder,chunks,clock,short=False):
    obj=runner.SerialSession.__new__(runner.SerialSession)
    obj.timeout_s=.1;obj.idle_timeout_s=.02;obj.transcript_path=Path(folder)/'private.log';obj.verbose=False
    obj.reconnect_count=0;obj.framing_sync_count=0;obj.framing_lost=False
    obj.serial_mod=SimpleNamespace(SerialException=OSError);obj.ser=Port(chunks,clock,short)
    return obj

class RunnerTests(unittest.TestCase):
    def test_memory_header_and_prompt_do_not_prove_payload(self):
        for cmd,output in [('read 0 32','0000: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n> '),
                           ('current 1','current: OK\n> '),('crc 0 64','CRC32\n> '),('text 0 16','0000: "short"\n> ')]:
            step=runner.CommandStep('x','memory',cmd)
            self.assertEqual(runner.classify(step,output,.01).status,'FAIL',cmd)
    def test_complete_arduino_and_idf_memory_responses(self):
        row=' '.join(f'{n:02X}' for n in range(16))
        for cmd,output in [('read 0 16','0000: '+row+' |................|\n> '),
                           ('read 0 16','0x000000: '+row+'\n> '),
                           ('current 1','0010: A5 |.|\n> '),('current 1','current: OK\nA5\n> '),
                           ('crc 0 64','CRC32[0x0000 + 64] = 0x12345678\n> '),
                           ('crc 0 64','crc32=0x12345678 addr=0x000000 len=64\n> '),
                           ('text 0 4','0000: "A\\0\\n\\xFF"\n> ')]:
            self.assertEqual(runner.classify(runner.CommandStep('x','memory',cmd),output,.01).status,'PASS',cmd)
    def test_diagnostic_prompt_requires_all_counters_and_restore_proof(self):
        cases = [
            ('stress 3', '[PASS] restore stress byte\n=== Stress Summary ===\nTarget: 3\nAttempts: 3\nSuccess: 3\nErrors: 0\n> ', 'Attempts: 3\n'),
            ('stress_mix 3', '[PASS] restore stress_mix scratch\n=== stress_mix summary ===\nTotal: ok=3 fail=0 (100%)\n> ', 'Total: ok=3 fail=0 (100%)\n'),
            ('selftest', 'Selftest result: pass=12 fail=0 skip=0\n> ', 'pass=12 '),
            ('rw_suite', 'Read/write suite result: pass=8 fail=0\n> ', 'pass=8 '),
            ('xfer_demo', 'Transfer demo result: pass=8 fail=0\n> ', 'pass=8 '),
            ('stress 3', 'stress restore: OK\nstress_ok=3/3\n> ', 'stress restore: OK\n'),
            ('randbench 3', 'randbench restore: OK\nrandbench_ok=3/3 elapsed_us=2 final_match=yes\n> ', 'final_match=yes'),
        ]
        for command,good,missing in cases:
            step=runner.CommandStep('x','diagnostics',command)
            self.assertEqual(runner.classify(step,good,.01).status,'PASS',command)
            self.assertEqual(runner.classify(step,good.replace(missing,''),.01).status,'FAIL',command)

    def test_health_requires_complete_current_snapshot(self):
        healthy=('=== Driver Health ===\nState: READY\nOnline: yes\nTotal success: 10\n'
                 'Total failures: 0\nConsecutive failures: 0\nSuccess rate: 100.0%\n'
                 'Last OK: 2 ms ago (at 45 ms)\nLast error: never\n> ')
        self.assertEqual(runner.classify(runner.CommandStep('x','health','drv'),healthy,.01).status,'PASS')
        for old,new in [('Total failures: 0\n',''),('READY','OFFLINE'),('Consecutive failures: 0','Consecutive failures: 1')]:
            self.assertEqual(runner.classify(runner.CommandStep('x','health','drv'),healthy.replace(old,new),.01).status,'FAIL')
    def test_timeout_never_sends_sync_and_stops_next_command(self):
        clock=Clock()
        with tempfile.TemporaryDirectory() as folder,patch.object(runner,'time',clock):
            obj=session(folder,[b'Status: OK\n'],clock)
            result=obj.command('probe',.1)
            self.assertFalse(result.prompt_detected);self.assertTrue(obj.framing_lost)
            self.assertEqual(obj.ser.writes,[b'probe\n'])
            with self.assertRaises(RuntimeError):obj.command('drv',.1)
            self.assertLessEqual(clock.now,.12)
            self.assertIn('Status: OK',obj.transcript_path.read_text())
    def test_short_write_stops_without_reading_queued_prompt(self):
        clock=Clock()
        with tempfile.TemporaryDirectory() as folder,patch.object(runner,'time',clock):
            obj=session(folder,[b'Status: OK\n> '],clock,True)
            result=obj.command('probe',.1)
            self.assertFalse(result.prompt_detected);self.assertTrue(obj.framing_lost)
            self.assertEqual(obj.ser.chunks,[b'Status: OK\n> '])
    def test_partial_read_error_is_retained_without_reconnect(self):
        clock=Clock()
        with tempfile.TemporaryDirectory() as folder,patch.object(runner,'time',clock):
            obj=session(folder,[b'State: DEGRADED\n',OSError('synthetic disconnect')],clock)
            result=obj.command('drv',.1)
            self.assertFalse(result.prompt_detected);self.assertTrue(obj.framing_lost)
            self.assertIn('DEGRADED',obj.transcript_path.read_text())
            self.assertEqual(obj.reconnect_count,0)
    def test_complete_response_is_not_flushed_or_duplicated(self):
        clock=Clock()
        with tempfile.TemporaryDirectory() as folder,patch.object(runner,'time',clock):
            obj=session(folder,[b'Status: OK\n> '],clock)
            result=obj.command('probe',.1)
            self.assertTrue(result.prompt_detected);self.assertFalse(obj.framing_lost)
            self.assertEqual(obj.transcript_path.read_text().count('Status: OK'),1)
    def test_boot_prompt_required_without_transmitting_version(self):
        clock=Clock()
        with tempfile.TemporaryDirectory() as folder,patch.object(runner,'time',clock):
            obj=session(folder,[b'MB85RC library version: 4.1.0\n'],clock)
            obj.wait_for_prompt(0,.1)
            self.assertTrue(obj.framing_lost);self.assertEqual(obj.ser.writes,[])
    def test_stop_on_first_complete_failed_payload(self):
        class Fake:
            reconnect_count=0;framing_sync_count=0
            def __init__(self):self.commands=[]
            def command(self,cmd,*_):self.commands.append(cmd);return runner.CommandCapture('0000:\n> ',.01,True,True)
        obj=Fake();steps=[runner.CommandStep('x','memory','read 0 16'),runner.CommandStep('y','memory','read 16 16')]
        results=runner.run_steps(obj,steps,1,runner.Observations())
        self.assertEqual(len(results),1);self.assertEqual(obj.commands,['read 0 16'])

if __name__=='__main__':unittest.main()
