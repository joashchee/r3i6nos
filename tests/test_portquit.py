"""overlay/usr/local/bin/r3i6nos-portquit: Select+Start held ends a process group; a short press or one button does not."""
import os
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

SCRIPT = os.path.join(os.path.dirname(__file__), "..", "overlay", "usr", "local", "bin", "r3i6nos-portquit")
EV = struct.Struct("llHHi")
SEL, START = 0x13A, 0x13B


def ev(code, value):
    return EV.pack(0, 0, 1, code, value)


class PortQuit(unittest.TestCase):
    def run_case(self, script, hold="0.6"):
        """Start a sleeping process group, a FIFO pad, and the watcher; script(fifo_fd) sends events. Returns whether the group survived."""
        tmp = tempfile.mkdtemp()
        fifo = os.path.join(tmp, "pad")
        os.mkfifo(fifo)
        victim = subprocess.Popen(["sleep", "30"], start_new_session=True)
        env = dict(os.environ, R3I6NOS_PORTQUIT_DEVICE=fifo, R3I6NOS_PORTQUIT_CODES="%d,%d" % (SEL, START),
                   R3I6NOS_PORTQUIT_HOLD=hold, R3I6NOS_PORTQUIT_GRACE="0.3")
        watcher = subprocess.Popen([sys.executable, SCRIPT, str(victim.pid)], env=env, stderr=subprocess.PIPE)
        wfd = os.open(fifo, os.O_WRONLY)
        try:
            script(wfd)
            time.sleep(0.4)
            alive = victim.poll() is None
        finally:
            if victim.poll() is None:
                os.killpg(victim.pid, signal.SIGKILL)
            victim.wait()
            os.close(wfd)
            watcher.wait(timeout=10)
        return alive

    def test_held_ends_the_group(self):
        def hold(fd):
            os.write(fd, ev(SEL, 1) + ev(START, 1))
            time.sleep(1.6)
        self.assertFalse(self.run_case(hold))

    def test_short_press_does_not(self):
        def tap(fd):
            os.write(fd, ev(SEL, 1) + ev(START, 1))
            time.sleep(0.2)
            os.write(fd, ev(SEL, 0) + ev(START, 0))
            time.sleep(1.0)
        self.assertTrue(self.run_case(tap))

    def test_one_button_does_not(self):
        def one(fd):
            os.write(fd, ev(START, 1))
            time.sleep(1.4)
        self.assertTrue(self.run_case(one))

    def test_exits_when_the_group_is_gone(self):
        victim = subprocess.Popen(["true"], start_new_session=True)
        victim.wait()
        env = dict(os.environ, R3I6NOS_PORTQUIT_DEVICE="/dev/null", R3I6NOS_PORTQUIT_CODES="1,2")
        r = subprocess.run([sys.executable, SCRIPT, str(victim.pid)], env=env, timeout=10)
        self.assertEqual(r.returncode, 0)

    def test_usage(self):
        self.assertEqual(subprocess.run([sys.executable, SCRIPT], stderr=subprocess.DEVNULL).returncode, 2)


if __name__ == "__main__":
    unittest.main()
