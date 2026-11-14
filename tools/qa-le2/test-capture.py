# SPDX-License-Identifier: MIT
"""Offline capture lifecycle/GDI contract checks; synthetic pixels, no desktop API."""
import copy
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from PIL import Image

spec = importlib.util.spec_from_file_location("capture", Path(__file__).with_name("capture.py"))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def window():
    return dict(hwnd="4294967300", pid=1234, width=3, height=2, visible=True, minimized=False)


def desktop_window():
    return dict(window(), foregroundHwnd=window()["hwnd"],
                clientScreenRect=dict(left=-10, top=20, right=-7, bottom=22),
                virtualScreenRect=dict(left=-1920, top=0, right=1920, bottom=1080))


class WindowTests(unittest.TestCase):
    def api(self, state=None):
        state = state or window()
        return mock.Mock(windows=mock.Mock(return_value=[state]), window=mock.Mock(return_value=state),
                         bitmap=mock.Mock(return_value=Image.new("RGB", (3, 2), "blue")))

    def test_unique_exact_pid_and_full_width_handle(self):
        api = self.api()
        state, image = m.capture_window(api, 1234)
        self.assertEqual(state, window())
        self.assertEqual(image.size, (3, 2))
        api.windows.assert_called_once_with(1234)
        api.bitmap.assert_called_once_with(4294967300, 3, 2)

    def test_ambiguous_minimized_empty_foreign_windows_never_capture(self):
        for windows in ([], [window(), window()], [dict(window(), minimized=True)],
                        [dict(window(), width=0)], [dict(window(), width=9000)],
                        [dict(window(), pid=9999)], [dict(window(), visible=False)]):
            with self.subTest(windows=windows):
                api = self.api()
                api.windows.return_value = windows
                with self.assertRaises(m.CaptureError): m.capture_window(api, 1234)
                api.bitmap.assert_not_called()

    def test_changed_hwnd_owner_or_size_invalidates_capture(self):
        for changed in (dict(window(), pid=9999), dict(window(), width=4), dict(window(), minimized=True)):
            api = self.api()
            api.window.side_effect = [window(), changed]
            with self.assertRaisesRegex(m.CaptureError, "during capture"):
                m.capture_window(api, 1234)

    def test_failed_printwindow_and_short_dib_read_release_handles(self):
        for print_ok, rows in ((False, 2), (True, 1)):
            api = m.WindowsCapture.__new__(m.WindowsCapture)
            api.user = mock.Mock(GetDC=mock.Mock(return_value=101), PrintWindow=mock.Mock(return_value=print_ok))
            api.gdi = mock.Mock(CreateCompatibleDC=mock.Mock(return_value=102),
                                CreateCompatibleBitmap=mock.Mock(return_value=103),
                                SelectObject=mock.Mock(return_value=104), PatBlt=mock.Mock(return_value=True),
                                GetDIBits=mock.Mock(return_value=rows))
            with self.assertRaises(m.CaptureError): api.bitmap(4294967300, 3, 2)
            api.gdi.DeleteDC.assert_called_once_with(102)
            api.gdi.DeleteObject.assert_called_once_with(103)
            api.user.ReleaseDC.assert_called_once_with(4294967300, 101)
            if not print_ok: api.gdi.GetDIBits.assert_not_called()

    def test_desktop_captures_exact_physical_rectangle_without_clipping(self):
        api = self.api()
        api.desktop_window.return_value = desktop_window()
        state, image = m.capture_desktop_client(api, 1234)
        self.assertEqual(state, desktop_window())
        self.assertEqual(image.size, (3, 2))
        api.bitmap.assert_called_once_with(4294967300, 3, 2, screen_rect=state['clientScreenRect'])
        self.assertEqual(api.desktop_window.call_count, 3)

    def test_desktop_rejects_foreign_minimized_offscreen_and_inconsistent_rectangle(self):
        for changed in (dict(desktop_window(), foregroundHwnd='44'), dict(desktop_window(), pid=9999),
                        dict(desktop_window(), minimized=True), dict(desktop_window(), visible=False),
                        dict(desktop_window(), clientScreenRect=dict(left=-1921, top=20, right=-1918, bottom=22)),
                        dict(desktop_window(), clientScreenRect=dict(left=-10, top=20, right=-6, bottom=22)),
                        dict(desktop_window(), virtualScreenRect=dict(left=0, top=0, right=0, bottom=0))):
            with self.subTest(state=changed):
                api = self.api()
                api.desktop_window.return_value = changed
                with self.assertRaises(m.CaptureError): m.capture_desktop_client(api, 1234)
                api.bitmap.assert_not_called()

    def test_desktop_move_focus_resize_and_ownership_change_after_copy_refused(self):
        for changed in (dict(desktop_window(), foregroundHwnd='55'), dict(desktop_window(), pid=5),
                        dict(desktop_window(), minimized=True), dict(desktop_window(), width=4),
                        dict(desktop_window(), clientScreenRect=dict(left=-9, top=20, right=-6, bottom=22)),
                        dict(desktop_window(), virtualScreenRect=dict(left=-1000, top=0, right=1000, bottom=900))):
            api = self.api()
            api.desktop_window.side_effect = [desktop_window(), desktop_window(), changed]
            with self.assertRaisesRegex(m.CaptureError, 'during desktop capture'):
                m.capture_desktop_client(api, 1234)
            api.bitmap.assert_called_once()
        api = self.api()
        api.desktop_window.side_effect = [desktop_window(), dict(desktop_window(), foregroundHwnd='55')]
        with self.assertRaisesRegex(m.CaptureError, 'before desktop capture'): m.capture_desktop_client(api, 1234)
        api.bitmap.assert_not_called()

    def test_desktop_win32_coordinate_conversion_and_negative_origin(self):
        api = m.WindowsCapture.__new__(m.WindowsCapture)
        api.window = mock.Mock(return_value=window())
        def client_rect(hwnd, output):
            output._obj.left, output._obj.top, output._obj.right, output._obj.bottom = 0, 0, 3, 2
            return True
        def to_screen(hwnd, point):
            point._obj.x -= 10
            point._obj.y += 20
            return True
        api.user = mock.Mock(GetClientRect=mock.Mock(side_effect=client_rect), ClientToScreen=mock.Mock(side_effect=to_screen),
                            GetForegroundWindow=mock.Mock(return_value=4294967300),
                            GetSystemMetrics=mock.Mock(side_effect=[-1920, 0, 3840, 1080]))
        self.assertEqual(api.desktop_window(4294967300), desktop_window())
        api.user.ClientToScreen.return_value = False
        api.user.ClientToScreen.side_effect = None
        with self.assertRaisesRegex(m.CaptureError, 'map physical client'): api.desktop_window(4294967300)

    def test_desktop_bitblt_does_not_call_printwindow_and_releases_desktop_dc(self):
        for succeeds in (False, True):
            api = m.WindowsCapture.__new__(m.WindowsCapture)
            api.user = mock.Mock(GetDC=mock.Mock(return_value=101))
            api.gdi = mock.Mock(CreateCompatibleDC=mock.Mock(return_value=102),
                                CreateCompatibleBitmap=mock.Mock(return_value=103),
                                SelectObject=mock.Mock(return_value=104), PatBlt=mock.Mock(return_value=True),
                                BitBlt=mock.Mock(return_value=succeeds), GetDIBits=mock.Mock(return_value=2))
            if succeeds:
                self.assertEqual(api.bitmap(4294967300, 3, 2, screen_rect=desktop_window()['clientScreenRect']).size, (3, 2))
            else:
                with self.assertRaisesRegex(m.CaptureError, 'BitBlt failed'):
                    api.bitmap(4294967300, 3, 2, screen_rect=desktop_window()['clientScreenRect'])
            api.user.GetDC.assert_called_once_with(None)
            api.user.ReleaseDC.assert_called_once_with(None, 101)
            api.user.PrintWindow.assert_not_called()
            api.gdi.BitBlt.assert_called_once_with(102, 0, 0, 3, 2, 101, -10, 20, 0x40CC0020)
            api.gdi.DeleteDC.assert_called_once_with(102)
            api.gdi.DeleteObject.assert_called_once_with(103)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.run = Path(self.temp.name).resolve()
        self.expected = dict(runDirectory=str(self.run), controlDirectory=str(self.run / "control"),
                             identity=dict(run="run-id", lease="lease-id", pid="1234", start="12345678901234567",
                                           exe="a" * 64, host="b" * 64, mod="b" * 64, game="LE2"),
                             process=dict(name="MassEffect2", pid=1234, started="exact", path="game.exe"), planSha256="c" * 64)
        self.bind = mock.Mock(side_effect=lambda _: copy.deepcopy(self.expected))

    def backend(self, run, batch):
        Image.new("RGB", (3, 2), "blue").save(batch / "frame.png")
        return dict(binding=copy.deepcopy(self.expected), window=window(), black=False, constant=True,
                    capturedAtUtc=m.utc(), method="synthetic-fixture")

    def test_receipt_hashes_image_and_never_accepts_gameplay(self):
        path, receipt = m.capture(self.run, "sample", bind=self.bind, backend=self.backend)
        self.assertEqual(receipt["status"], "suspect-constant")
        self.assertFalse(receipt["gameplayPassed"])
        self.assertEqual(receipt["visualAcceptance"], "pending-human-review")
        self.assertEqual(receipt["bindingBefore"], receipt["bindingAfter"])
        self.assertEqual(receipt["image"]["sha256"], m.c.session.digest(path.parent / "frame.png"))
        self.assertEqual(m.c.session.read_json(path), receipt)

    def test_black_frame_is_kept_as_suspect(self):
        def black(run, batch):
            result = self.backend(run, batch)
            result["black"] = True
            return result
        path, receipt = m.capture(self.run, "black", bind=self.bind, backend=black)
        self.assertEqual(receipt["status"], "suspect-black")
        self.assertTrue((path.parent / "frame.png").is_file())

    def test_existing_outputs_and_bad_names_never_overwrite(self):
        path, _ = m.capture(self.run, "existing", bind=self.bind, backend=self.backend)
        original = path.read_bytes()
        backend = mock.Mock()
        with self.assertRaises(FileExistsError): m.capture(self.run, "existing", bind=self.bind, backend=backend)
        for name in ("../elsewhere", "x/y", "x:y"):
            with self.assertRaises(m.CaptureError): m.capture(self.run, name, bind=self.bind, backend=backend)
        backend.assert_not_called()
        self.assertEqual(path.read_bytes(), original)

    def test_refused_session_does_not_create_outputs(self):
        backend = mock.Mock()
        with self.assertRaisesRegex(m.CaptureError, "restored"):
            m.capture(self.run, bind=mock.Mock(side_effect=m.CaptureError("restored")), backend=backend)
        backend.assert_not_called()
        self.assertFalse((self.run / "captures").exists())

    def test_real_binding_requires_active_phase_and_reuses_control_verification(self):
        context = dict(phase="restored", gameProcess=self.expected["process"], planSha256="x")
        with mock.patch.object(m.c, "binding", return_value=(self.run / "control", self.expected["identity"])) as checked:
            with mock.patch.object(m.c.session, "load_context", return_value=(self.run, context)):
                with self.assertRaisesRegex(m.CaptureError, "game-bound"): m.binding(self.run)
        checked.assert_called_once_with(self.run)

    def test_changed_process_or_build_retains_invalid_image_evidence(self):
        changed = copy.deepcopy(self.expected)
        changed["identity"]["start"] = "different-generation"
        bound = mock.Mock(side_effect=[self.expected, self.expected, changed])
        path, receipt = m.capture(self.run, "changed", bind=bound, backend=self.backend)
        self.assertEqual(receipt["status"], "failed")
        self.assertIn("provenance invalid", receipt["error"])
        self.assertTrue((path.parent / "frame.png").is_file())
        self.assertFalse(receipt["gameplayPassed"])

    def test_timeout_or_missing_image_is_failure_and_never_retried(self):
        for name, backend in (("timeout", mock.Mock(side_effect=subprocess.TimeoutExpired("private-helper", 20))),
                              ("missing", mock.Mock(return_value=dict(binding=self.expected, window=window(), black=False, constant=False)))):
            path, receipt = m.capture(self.run, name, bind=self.bind, backend=backend)
            self.assertEqual(receipt["status"], "failed")
            self.assertIsNone(receipt["image"])
            backend.assert_called_once()
            self.assertTrue(path.is_file())

    def test_minimized_failure_preserves_window_flags_without_image(self):
        failed_window = dict(window(), minimized=True)
        backend = mock.Mock(side_effect=m.CaptureError("minimized", window=failed_window))
        _, receipt = m.capture(self.run, "minimized", bind=self.bind, backend=backend)
        self.assertEqual(receipt["status"], "failed")
        self.assertTrue(receipt["window"]["minimized"])
        self.assertIsNone(receipt["image"])

    def test_worker_flags_actual_synthetic_black_and_restores_thread_dpi(self):
        batch = self.run / "captures" / "black-worker"
        batch.mkdir(parents=True)
        api = mock.Mock()
        api.windows.return_value = [window()]
        api.window.return_value = window()
        api.bitmap.return_value = Image.new("RGB", (3, 2), "black")
        api.user.SetThreadDpiAwarenessContext.return_value = 123
        with mock.patch.object(m, "binding", side_effect=self.bind), mock.patch.object(m, "WindowsCapture", return_value=api):
            result = m.worker(self.run, batch)
        self.assertTrue(result["black"])
        self.assertTrue(result["constant"])
        self.assertTrue((batch / "frame.png").is_file())
        self.assertEqual(api.user.SetThreadDpiAwarenessContext.call_args_list[-1], mock.call(123))

    def test_desktop_worker_distinct_method_and_no_automatic_fallback(self):
        batch = self.run / 'captures/desktop-worker'
        batch.mkdir(parents=True)
        api = mock.Mock()
        api.windows.return_value = [window()]
        api.desktop_window.return_value = desktop_window()
        api.bitmap.return_value = Image.new('RGB', (3, 2), 'black')
        api.user.SetThreadDpiAwarenessContext.return_value = 123
        with mock.patch.object(m, 'binding', side_effect=self.bind), mock.patch.object(m, 'WindowsCapture', return_value=api):
            result = m.worker(self.run, batch, 'desktop-client')
        self.assertEqual(result['method'], 'BitBlt(desktop-client;SRCCOPY|CAPTUREBLT)')
        self.assertTrue(result['foregroundVerifiedBeforeAfter'])
        self.assertFalse(result['occlusionIndependentlyVerified'])
        self.assertTrue(result['black'])
        api.bitmap.assert_called_once()
        self.assertEqual(api.user.SetThreadDpiAwarenessContext.call_args_list[-1], mock.call(123))

    def test_desktop_receipt_remains_compatible_with_save_evidence_capture_proof(self):
        def desktop(run, batch):
            result = self.backend(run, batch)
            # Synthetic nonconstant pixels, not an assertion about a live UI.
            image = Image.new('RGB', (3, 2), 'blue')
            image.putpixel((0, 0), (255, 0, 0))
            image.save(batch / 'frame.png')
            result.update(method='BitBlt(desktop-client;SRCCOPY|CAPTUREBLT)', constant=False,
                          window=desktop_window(), pixelSource='composited-desktop-client-region')
            return result
        path, receipt = m.capture(self.run, 'desktop', bind=self.bind, backend=desktop, method='desktop-client')
        self.assertEqual(receipt['status'], 'captured')
        self.assertEqual(receipt['requestedMethod'], 'desktop-client')
        spec = importlib.util.spec_from_file_location('capture_evidence_test', Path(__file__).with_name('save-evidence.py'))
        collector = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(collector)
        proof = collector.capture_proof(self.run, path.relative_to(self.run).as_posix(),
                                       dict(gameProcess=self.expected['process'], planSha256=self.expected['planSha256']),
                                       self.expected['identity'])
        self.assertEqual(proof['sha256'], m.c.session.digest(path))

    def test_explicit_method_propagates_to_native_backend_without_live_call(self):
        with mock.patch.object(m, 'native_backend', side_effect=self.backend) as native:
            # Three arguments reach the real helper boundary; inject a compatible fixture.
            native.side_effect = lambda run, batch, method: self.backend(run, batch)
            m.capture(self.run, 'explicit', bind=self.bind, method='desktop-client')
        self.assertEqual(native.call_args.args[2], 'desktop-client')
        with self.assertRaises(m.CaptureError): m.capture(self.run, method='unknown', bind=self.bind)


if __name__ == "__main__":
    unittest.main()
