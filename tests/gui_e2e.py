#!/usr/bin/env python3
"""
End-to-end GUI test for the options screen / audio events.  NOT part of `make test` (it needs a display).

Starts the real ./lootsmith binary inside Xvfb, drives it with real X11 mouse / key events (XTEST) and checks
what it prints with --audio-log ("[audio] ..." lines) plus the settings file on disk.  The audio output itself is not
audible here: the test asserts which sound was *requested* and that exactly one is requested per input.

    pip install python-xlib
    python3 tests/gui_e2e.py [path/to/lootsmith]          (needs Xvfb; an ALSA "null" default device makes the
                                                           audio device initialise, see the printed hint otherwise)
"""
import os, sys, time, subprocess, threading, random, tempfile, shutil, signal
from Xlib import X, display, XK, Xatom
from Xlib.ext import xtest

EXE = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "lootsmith"))
DISP = ":77"
WORK = tempfile.mkdtemp(prefix="lootsmith_e2e_")
CFG, SAV = os.path.join(WORK, "e2e.cfg"), os.path.join(WORK, "e2e.sav")
results = []


def check(name, cond, extra=""):
    results.append((name, bool(cond)))
    print(("  PASS " if cond else "  FAIL ") + name + (("   " + str(extra)) if (extra and not cond) else ""))


class Game:
    def __init__(self, extra_args=()):
        env = dict(os.environ, DISPLAY=DISP)
        self.lines, self.proc = [], subprocess.Popen(
            [EXE, "--audio-log", "--settings", CFG, "--save", SAV, "--seed", "7", *extra_args],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, text=True, bufsize=1)
        threading.Thread(target=self._read, daemon=True).start()
        self.d = display.Display(DISP)
        self.root = self.d.screen().root
        self.win = None
        for _ in range(100):
            self.win = self._find(self.root)
            if self.win: break
            time.sleep(0.1)
        assert self.win, "game window not found"
        time.sleep(1.0)
        g = self.win.get_geometry()
        self.w, self.h = g.width, g.height
        self.win.set_input_focus(X.RevertToParent, X.CurrentTime); self.d.sync()
        co = self.win.translate_coords(self.root, 0, 0)
        self.ox, self.oy = -co.x, -co.y
        self.scale = min(self.w / 640, self.h / 360)

    def _read(self):
        for ln in self.proc.stdout:
            self.lines.append(ln.rstrip("\n"))

    def _find(self, w):
        try:
            if w.get_wm_name() == "LOOT SMITH": return w
            for c in w.query_tree().children:
                r = self._find(c)
                if r: return r
        except Exception:
            pass
        return None

    # ---- log ----
    def mark(self): return len(self.lines)
    def audio(self, m): return [l[8:] for l in self.lines[m:] if l.startswith("[audio] ")]
    def se(self, m): return [l[3:].split(" (")[0] for l in self.audio(m) if l.startswith("SE ")]        # requested (even if muted)
    def played(self, m): return [l[3:] for l in self.audio(m) if l.startswith("SE ") and "(" not in l]     # really sent to the mixer
    def alive(self): return self.proc.poll() is None

    # ---- input ----
    def move(self, vx, vy):
        xtest.fake_input(self.d, X.MotionNotify, x=int(self.ox + vx * self.scale), y=int(self.oy + vy * self.scale)); self.d.sync(); time.sleep(0.05)
    def down(self): xtest.fake_input(self.d, X.ButtonPress, 1); self.d.sync(); time.sleep(0.08)
    def up(self): xtest.fake_input(self.d, X.ButtonRelease, 1); self.d.sync(); time.sleep(0.12)
    def click(self, vx, vy): self.move(vx, vy); self.down(); self.up()
    def key(self, name, shift=False, hold=0.07):
        code = self.d.keysym_to_keycode(XK.string_to_keysym(name))
        sc = self.d.keysym_to_keycode(XK.string_to_keysym("Shift_L"))
        if shift: xtest.fake_input(self.d, X.KeyPress, sc)
        xtest.fake_input(self.d, X.KeyPress, code); self.d.sync(); time.sleep(hold)
        xtest.fake_input(self.d, X.KeyRelease, code)
        if shift: xtest.fake_input(self.d, X.KeyRelease, sc)
        self.d.sync(); time.sleep(0.12)
    def shot(self, path):
        subprocess.run(["import", "-display", DISP, "-window", "root", path], check=False)
    def close_window(self):
        wm_p = self.d.intern_atom("WM_PROTOCOLS"); wm_d = self.d.intern_atom("WM_DELETE_WINDOW")
        from Xlib.protocol import event
        ev = event.ClientMessage(window=self.win, client_type=wm_p, data=(32, [wm_d, X.CurrentTime, 0, 0, 0]))
        self.win.send_event(ev, event_mask=0); self.d.sync()
    def wait_exit(self, t=6):
        try: self.proc.wait(timeout=t)
        except subprocess.TimeoutExpired: return False
        return True
    def kill(self):
        if self.alive(): self.proc.kill()


def slider_x(v): return 140 + v * 280 / 100.0
def read_cfg():
    try: return open(CFG).read()
    except OSError: return None


def main():
    xvfb = subprocess.Popen(["Xvfb", DISP, "-screen", "0", "1400x800x24"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    ok = True
    try:
        # ------------------------------------------------------------------ run 1: title, options, sliders
        print("run 1: title screen, options, sliders")
        g = Game()
        time.sleep(0.5)
        init = g.audio(0)
        print("   ", init[:2])
        check("audio device initialised (else install an ALSA null device)", any("device=ok" in l for l in init))
        check("BGM started exactly once at launch", sum(l == "bgm start" for l in init) == 1)
        check("no settings file yet -> default volumes 35 / 65", any("bgm=35 se=65" in l for l in init))

        m = g.mark(); g.move(320, 134); g.move(320, 162); g.move(200, 300); g.move(320, 246)       # hover only
        check("hovering buttons plays nothing", g.se(m) == [])
        m = g.mark(); g.click(320, 162)                                                            # CONTINUE (disabled: no save file)
        check("clicking a disabled button plays nothing", g.se(m) == [], g.audio(m))

        m = g.mark(); g.click(320, 218)                                                            # OPTIONS on the title screen
        check("OPTIONS button: exactly one click sound", g.se(m) == ["ui_click"], g.audio(m))
        g.shot(os.path.join(WORK, "options_open.png"))
        check("settings file not written just by opening", read_cfg() is None)

        # drag the BGM slider from 35 % to 0 %: no file write while dragging, one write on release
        m = g.mark(); g.move(slider_x(35), 96); g.down()
        for v in (30, 22, 14, 6, 0): g.move(slider_x(v), 96); time.sleep(0.05)
        vols = [l for l in g.audio(m) if l.startswith("volumes")]
        check("drag applies volume immediately (BGM only)", vols and vols[-1] == "volumes bgm=0 se=65", vols[-3:])
        check("no file write during the drag", read_cfg() is None)
        g.up(); time.sleep(0.2)
        check("file written once the drag is released", read_cfg() == "LOOTSMITH_SETTINGS 1\nbgm_volume 0\nse_volume 65\n", read_cfg())
        check("dragging the BGM slider never played an SE", g.se(m) == [], g.se(m))

        # BGM muted, SE still audible: independence
        m = g.mark(); g.key("Down"); g.key("Right"); g.key("Right")                                 # SE 65 -> 75, previews with a click
        check("SE slider: 65 -> 75 with BGM at 0 %: SE still really played (independent)", "volumes bgm=0 se=75" in g.audio(m) and g.played(m) and set(g.played(m)) == {"ui_click"}, g.audio(m))
        m = g.mark(); g.key("Left", shift=True)
        check("Shift+Left changes by 1 (75 -> 74)", "volumes bgm=0 se=74" in g.audio(m), g.audio(m))
        m = g.mark(); g.key("End"); g.key("Home")
        al = g.audio(m)
        check("End = 100 %, Home = 0 %", "volumes bgm=0 se=100" in al and "volumes bgm=0 se=0" in al, al)
        i0 = al.index("volumes bgm=0 se=0")                       # the SE preview that follows the move to 0 % must be muted, not played
        check("SE at 0 %: the preview is muted (nothing sent to the mixer)", al[i0 + 1:i0 + 2] == ["SE ui_click (muted)"], al)
        g.key("Right")                                                                               # SE 0 -> 5 (audible again)
        m2 = g.mark(); g.key("Up"); g.key("Up")                                                      # Up (BGM row), Up again wraps to the BACK row
        g.key("Return")
        time.sleep(0.4)
        check("BACK via Enter: one back sound, really played (SE 5 %)", g.played(m2) == ["ui_back"], g.audio(m2))
        check("leaving options saved the file", read_cfg() == "LOOTSMITH_SETTINGS 1\nbgm_volume 0\nse_volume 5\n", read_cfg())
        g.shot(os.path.join(WORK, "back_to_title.png"))

        # ------------------------------------------------------------------ title -> options -> Esc returns to the title; F2 works too
        m = g.mark(); g.key("F2"); time.sleep(0.2); g.key("Escape"); time.sleep(0.2)
        check("F2 opens, Esc closes (title): click + back", g.se(m) == ["ui_click", "ui_back"] or g.se(m) == ["ui_back"], g.audio(m))
        g.key("F2"); time.sleep(0.2); g.key("F2"); time.sleep(0.2)                                    # toggle open / closed again

        # ------------------------------------------------------------------ in game: new game, navigation, options from the game and back
        m = g.mark(); g.key("n"); time.sleep(0.3)
        check("NEW GAME: one sound (day_start chime, not click + chime)", g.se(m) == ["day_start"], g.audio(m))
        m = g.mark(); g.key("2"); time.sleep(0.2)
        check("tab 2 MARKET: nav tick", g.se(m) == ["ui_nav"], g.audio(m))
        m = g.mark(); g.key("Return"); time.sleep(0.2)                                               # buy 1 iron
        check("buying: one coin sound (no extra click)", g.se(m) == ["coin"], g.audio(m))
        m = g.mark(); g.key("F2"); time.sleep(0.3)
        check("F2 in game opens options", g.se(m) == ["ui_click"], g.audio(m))
        g.shot(os.path.join(WORK, "options_from_game.png"))
        m = g.mark(); g.key("Escape"); time.sleep(0.3)
        check("Esc returns (one back sound)", g.se(m) == ["ui_back"], g.audio(m))
        g.shot(os.path.join(WORK, "back_to_market.png"))
        m = g.mark(); g.click(498, 33); time.sleep(0.3)                                               # header OPTIONS button
        check("header OPTIONS button works in game", g.se(m) == ["ui_click"], g.audio(m))
        m = g.mark(); g.click(210, 312); time.sleep(0.3)                                              # BACK button
        check("BACK button: one back sound", g.se(m) == ["ui_back"], g.audio(m))
        # we should be on the market again: Enter buys (coin) -> proves the return screen is right
        m = g.mark(); g.key("Return"); time.sleep(0.2)
        check("returned to MARKET (Enter buys again)", g.se(m) == ["coin"], g.audio(m))

        m = g.mark(); g.key("3"); time.sleep(0.2); m = g.mark(); g.key("Return"); time.sleep(0.2)
        check("craft without materials: one error sound", g.se(m) == ["error"], g.audio(m))
        m = g.mark(); g.key("2"); g.key("Right"); g.key("Right"); g.key("Right"); g.key("Right"); g.key("Return"); time.sleep(0.2)
        m = g.mark(); g.key("3"); time.sleep(0.2)
        m = g.mark(); g.key("Return"); time.sleep(0.3)
        check("craft success: one hammer sound", g.se(m) in (["craft"], ["craft_rare"]), g.audio(m))

        # ------------------------------------------------------------------ fuzz: one sound per input, no BGM restarts, still alive
        print("fuzz: 150 random inputs")
        rnd = random.Random(5)
        keys = ["1", "2", "3", "4", "5", "Return", "Escape", "Left", "Right", "Up", "Down", "o", "d", "e", "F2", "F1", "F5", "g", "r", "l", "w", "a", "s", "z", "x", "c", "Home", "End"]
        worst, bad = 0, []
        for i in range(150):
            m = g.mark()
            if rnd.random() < 0.35:
                cx, cy = rnd.randint(0, 639), rnd.randint(0, 359)
                if 250 <= cx <= 390 and 234 <= cy <= 258: cx = 5                                    # keep clear of QUIT on the title screen
                g.click(cx, cy)
            else: g.key(rnd.choice(keys))
            time.sleep(0.12)
            n = len(g.se(m)); worst = max(worst, n)
            if n > 1: bad.append((i, g.audio(m)))
            if not g.alive(): break
        check("fuzz: never more than one SE per input", not bad, bad[:3])
        check("fuzz: process still running", g.alive(), "exit=%s" % g.proc.poll())
        allaudio = g.audio(0)
        check("fuzz: BGM never restarted / resumed", sum(l == "bgm start" for l in allaudio) == 1 and not any("resumed" in l for l in allaudio))
        g.shot(os.path.join(WORK, "after_fuzz.png"))

        # ------------------------------------------------------------------ keyboard change then close the window quickly: flushed at exit
        m = g.mark()
        for _ in range(3): g.key("Escape")                         # leave OPTIONS / sub screens if the fuzz ended there
        g.key("F2"); time.sleep(0.3)                               # opens OPTIONS from any screen; row 0 (BGM) is selected
        g.key("Home"); g.key("Right"); g.key("Right"); g.key("Right")  # BGM -> 15
        g.close_window()
        check("window close ends the game", g.wait_exit(6))
        tail = g.audio(0)
        check("shutdown reached (resources released)", "shutdown" in tail, tail[-3:])
        cfg = read_cfg()
        check("unsaved change flushed at exit (BGM 15)", cfg is not None and "bgm_volume 15" in cfg, cfg)
        g.kill()

        # ------------------------------------------------------------------ run 2: restart restores the volumes; corrupt file -> defaults
        print("run 2: restart")
        g = Game(); time.sleep(0.6)
        init = g.audio(0)
        check("restart restores the saved volumes", any("volumes bgm=15" in l for l in init), init[:2])
        g.key("q"); check("Q quits", g.wait_exit(5)); g.kill()
        open(CFG, "w").write("complete garbage\x01\x02")
        g = Game(); time.sleep(0.6)
        check("garbage settings file -> defaults, game runs", g.alive() and any("bgm=35 se=65" in l for l in g.audio(0)), g.audio(0)[:2])
        g.key("q"); g.wait_exit(5); g.kill()
        os.remove(CFG)
        g = Game(("--settings", "/nonexistent_dir_xyz/lootsmith.cfg"))        # settings can't be written: the game must carry on
        time.sleep(0.6); g.key("F2"); time.sleep(0.2); g.key("Right"); time.sleep(1.0); g.key("Escape"); time.sleep(0.4)
        g.shot(os.path.join(WORK, "save_failed.png"))
        check("unwritable settings path: game keeps running", g.alive())
        g.key("q"); check("... and still quits cleanly", g.wait_exit(5)); g.kill()
    except Exception as e:
        import traceback; traceback.print_exc(); ok = False
    finally:
        xvfb.terminate()
    fails = [n for n, c in results if not c]
    print("\n%d checks, %d failed%s" % (len(results), len(fails), "" if ok else "  (aborted by an exception)"))
    print("screenshots / files in", WORK)
    return 1 if (fails or not ok) else 0


if __name__ == "__main__":
    sys.exit(main())
