import json
import math
import os
import queue
import threading
import time
import tkinter as tk
from collections import deque
from tkinter import messagebox, ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("缺少 pyserial，请先运行：pip install pyserial") from exc

try:
    import matplotlib as mpl
    from matplotlib import font_manager
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure
    from matplotlib.font_manager import FontProperties
except ImportError as exc:
    raise SystemExit("缺少 matplotlib，请先运行：pip install matplotlib") from exc


MAX_POINTS = 120
BAUDRATE = 115200
WHEEL_RADIUS_M = 0.075
RPM_TO_MPS = 2 * math.pi * WHEEL_RADIUS_M / 60
MAX_RPM_DISPLAY = 500
MAX_SPEED_MPS_DISPLAY = 4.0

APP_BG = "#0b1220"
PANEL_BG = "#111827"
CARD_BG = "#172033"
CARD_BORDER = "#263244"
TEXT_PRIMARY = "#f8fafc"
TEXT_SECONDARY = "#cbd5e1"
TEXT_MUTED = "#94a3b8"
CHART_BG = "#0f172a"
CHART_GRID = "#263244"
CHART_SPINE = "#475569"

LINE_COLORS = {
    "left": "#7dd3fc",
    "right": "#fbbf24",
    "speed": "#86efac",
    "height": "#c4b5fd",
    "state": "#fde68a",
}


def get_chinese_font():
    candidates = [
        r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
        r"C:\Windows\Fonts\simsun.ttc",
    ]
    for path in candidates:
        if os.path.exists(path):
            font_manager.fontManager.addfont(path)
            return FontProperties(fname=path)
    return FontProperties(family=["Microsoft YaHei", "SimHei", "SimSun", "sans-serif"])


CJK_FONT = get_chinese_font()
mpl.rcParams["axes.unicode_minus"] = False


STATE_TEXT = {
    "RUN": "正常运行",
    "DISABLED": "未使能",
    "ESTOP": "急停",
    "FAILSAFE": "遥控失联",
}


class TelemetryMonitor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("轮式机器人运行状态监控")
        self.geometry("1120x700")
        self.minsize(960, 620)
        self.configure(bg=APP_BG)

        self.serial_port = None
        self.reader_thread = None
        self.reader_running = False
        self.data_queue = queue.Queue()
        self.start_time = time.monotonic()
        self.chart_font = CJK_FONT

        self.x_data = deque(maxlen=MAX_POINTS)
        self.left_data = deque(maxlen=MAX_POINTS)
        self.right_data = deque(maxlen=MAX_POINTS)
        self.speed_data = deque(maxlen=MAX_POINTS)

        self.value_vars = {
            "left": tk.StringVar(value="-- rpm"),
            "right": tk.StringVar(value="-- rpm"),
            "speed": tk.StringVar(value="-- m/s"),
            "state": tk.StringVar(value="未连接"),
            "height": tk.StringVar(value="-- mm"),
        }

        self._build_ui()
        self.refresh_ports()
        self.after(100, self._poll_data_queue)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def _build_ui(self):
        style = ttk.Style()
        style.theme_use("clam")
        style.configure(
            "Telemetry.TCombobox",
            fieldbackground=CARD_BG,
            background=CARD_BG,
            foreground=TEXT_PRIMARY,
            arrowcolor=TEXT_PRIMARY,
            bordercolor=CARD_BORDER,
            lightcolor=CARD_BORDER,
            darkcolor=CARD_BORDER,
            padding=6,
        )
        style.map("Telemetry.TCombobox", fieldbackground=[("readonly", CARD_BG)])

        top = tk.Frame(self, bg=APP_BG)
        top.pack(fill=tk.X, padx=18, pady=(16, 8))

        title = tk.Label(
            top,
            text="轮式机器人运行状态监控",
            bg=APP_BG,
            fg=TEXT_PRIMARY,
            font=("Microsoft YaHei UI", 20, "bold"),
        )
        title.pack(side=tk.LEFT)

        controls = tk.Frame(top, bg=APP_BG)
        controls.pack(side=tk.RIGHT)

        self.port_combo = ttk.Combobox(controls, width=18, state="readonly", style="Telemetry.TCombobox")
        self.port_combo.pack(side=tk.LEFT, padx=(0, 8))

        self.refresh_button = tk.Button(
            controls,
            text="刷新",
            command=self.refresh_ports,
            bg="#243044",
            fg=TEXT_PRIMARY,
            activebackground="#334155",
            activeforeground=TEXT_PRIMARY,
            relief=tk.FLAT,
            padx=14,
            pady=8,
        )
        self.refresh_button.pack(side=tk.LEFT, padx=(0, 8))

        self.connect_button = tk.Button(
            controls,
            text="连接",
            command=self.toggle_connection,
            bg="#0f766e",
            fg=TEXT_PRIMARY,
            activebackground="#0d9488",
            activeforeground=TEXT_PRIMARY,
            relief=tk.FLAT,
            padx=18,
            pady=8,
        )
        self.connect_button.pack(side=tk.LEFT)

        body = tk.Frame(self, bg=APP_BG)
        body.pack(fill=tk.BOTH, expand=True, padx=18, pady=(0, 18))
        body.columnconfigure(0, weight=0)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        cards = tk.Frame(body, bg=APP_BG, width=310)
        cards.grid(row=0, column=0, sticky="nsw", padx=(0, 14))
        cards.grid_propagate(False)

        self._add_card(cards, "左轮转速", self.value_vars["left"], LINE_COLORS["left"])
        self._add_card(cards, "右轮转速", self.value_vars["right"], LINE_COLORS["right"])
        self._add_card(cards, "当前移动速度", self.value_vars["speed"], LINE_COLORS["speed"])
        self._add_card(cards, "运行状态", self.value_vars["state"], LINE_COLORS["state"])
        self._add_card(cards, "升降机构高度", self.value_vars["height"], LINE_COLORS["height"])

        chart_host = tk.Frame(body, bg=PANEL_BG, highlightthickness=1, highlightbackground=CARD_BORDER)
        chart_host.grid(row=0, column=1, sticky="nsew")

        self.figure = Figure(figsize=(7, 5), dpi=100, facecolor=PANEL_BG)
        self.figure.subplots_adjust(left=0.08, right=0.9, top=0.91, bottom=0.11)
        self.axis = self.figure.add_subplot(111)
        self.axis.set_facecolor(CHART_BG)
        self.axis.set_title("实时速度曲线", fontproperties=self.chart_font, color=TEXT_PRIMARY, fontsize=14, pad=14)
        self.axis.set_xlabel("时间 / s", fontproperties=self.chart_font, color=TEXT_SECONDARY, labelpad=8)
        self.axis.set_ylabel("转速 / rpm", fontproperties=self.chart_font, color=TEXT_SECONDARY, labelpad=8)
        self.axis.grid(True, color=CHART_GRID, linewidth=0.8, alpha=0.85)
        self.axis.axhline(0, color="#64748b", linewidth=0.9, alpha=0.65)
        self.axis.set_xlim(0, 10)
        self.axis.set_ylim(0, MAX_RPM_DISPLAY)
        self.axis.tick_params(colors=TEXT_MUTED, labelsize=9)
        for spine in self.axis.spines.values():
            spine.set_color(CHART_SPINE)
            spine.set_linewidth(1.0)

        self.speed_axis = self.axis.twinx()
        self.speed_axis.set_ylabel("移动速度 / m/s", fontproperties=self.chart_font, color=LINE_COLORS["speed"], labelpad=8)
        self.speed_axis.set_ylim(0, MAX_SPEED_MPS_DISPLAY)
        self.speed_axis.tick_params(colors=LINE_COLORS["speed"], labelsize=9)
        self.speed_axis.spines["right"].set_color(LINE_COLORS["speed"])
        self.speed_axis.spines["right"].set_linewidth(1.0)
        self.speed_axis.spines["left"].set_color(CHART_SPINE)
        self.speed_axis.spines["top"].set_color(CHART_SPINE)
        self.speed_axis.spines["bottom"].set_color(CHART_SPINE)

        (self.left_line,) = self.axis.plot([], [], color=LINE_COLORS["left"], linewidth=2.4, label="左轮")
        (self.right_line,) = self.axis.plot([], [], color=LINE_COLORS["right"], linewidth=2.4, label="右轮")
        (self.speed_line,) = self.speed_axis.plot([], [], color=LINE_COLORS["speed"], linewidth=2.6, label="当前移动速度")
        legend = self.axis.legend(
            handles=[self.left_line, self.right_line, self.speed_line],
            loc="upper left",
            prop=self.chart_font,
            facecolor="#182235",
            edgecolor=CARD_BORDER,
            framealpha=0.92,
        )
        for text in legend.get_texts():
            text.set_color(TEXT_SECONDARY)

        self.canvas = FigureCanvasTkAgg(self.figure, master=chart_host)
        self.canvas.get_tk_widget().configure(bg=PANEL_BG, highlightthickness=0)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True, padx=12, pady=12)

    def _add_card(self, parent, title, value_var, color):
        card = tk.Frame(parent, bg=CARD_BG, highlightthickness=1, highlightbackground=CARD_BORDER)
        card.pack(fill=tk.X, pady=(0, 12))

        label = tk.Label(
            card,
            text=title,
            bg=CARD_BG,
            fg=TEXT_SECONDARY,
            font=("Microsoft YaHei UI", 11),
            anchor="w",
        )
        label.pack(fill=tk.X, padx=16, pady=(12, 2))

        value = tk.Label(
            card,
            textvariable=value_var,
            bg=CARD_BG,
            fg=color,
            font=("Consolas", 22, "bold"),
            anchor="w",
        )
        value.pack(fill=tk.X, padx=16, pady=(0, 14))

    def refresh_ports(self):
        ports = [port.device for port in list_ports.comports()]
        self.port_combo["values"] = ports
        if ports and not self.port_combo.get():
            self.port_combo.set(ports[0])

    def toggle_connection(self):
        if self.serial_port:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_combo.get().strip()
        if not port:
            messagebox.showwarning("未选择串口", "请先选择 STM32 虚拟串口。")
            return

        try:
            self.serial_port = serial.Serial(port, BAUDRATE, timeout=0.1)
        except serial.SerialException as exc:
            messagebox.showerror("连接失败", str(exc))
            self.serial_port = None
            return

        self.reader_running = True
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()
        self.connect_button.configure(text="断开", bg="#b91c1c", activebackground="#dc2626")
        self.value_vars["state"].set("等待数据")

    def disconnect(self):
        self.reader_running = False
        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=0.5)
        self.reader_thread = None

        if self.serial_port:
            try:
                self.serial_port.close()
            except serial.SerialException:
                pass
        self.serial_port = None
        self.connect_button.configure(text="连接", bg="#0f766e", activebackground="#0d9488")
        self.value_vars["state"].set("未连接")

    def _reader_loop(self):
        while self.reader_running and self.serial_port:
            try:
                raw = self.serial_port.readline()
            except serial.SerialException as exc:
                self.data_queue.put(("error", str(exc)))
                break

            if not raw:
                continue

            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            try:
                data = json.loads(line)
            except json.JSONDecodeError:
                continue

            self.data_queue.put(("data", data))

    def _poll_data_queue(self):
        try:
            while True:
                kind, payload = self.data_queue.get_nowait()
                if kind == "error":
                    messagebox.showerror("串口错误", payload)
                    self.disconnect()
                elif kind == "data":
                    self._update_from_data(payload)
        except queue.Empty:
            pass

        self.after(100, self._poll_data_queue)

    def _update_from_data(self, data):
        left = abs(int(data.get("left_rpm", 0)))
        right = abs(int(data.get("right_rpm", 0)))
        speed = ((left + right) / 2) * RPM_TO_MPS
        height = int(data.get("height_mm", -1))
        state = str(data.get("state", "UNKNOWN"))

        self.value_vars["left"].set(f"{left} rpm")
        self.value_vars["right"].set(f"{right} rpm")
        self.value_vars["speed"].set(f"{speed:.3f} m/s")
        self.value_vars["state"].set(STATE_TEXT.get(state, state))
        self.value_vars["height"].set("-- mm" if height < 0 else f"{height} mm")

        now = time.monotonic() - self.start_time
        self.x_data.append(now)
        self.left_data.append(left)
        self.right_data.append(right)
        self.speed_data.append(speed)
        self._redraw_chart()

    def _redraw_chart(self):
        self.left_line.set_data(self.x_data, self.left_data)
        self.right_line.set_data(self.x_data, self.right_data)
        self.speed_line.set_data(self.x_data, self.speed_data)

        if self.x_data:
            self.axis.set_xlim(max(0, self.x_data[0]), max(10, self.x_data[-1]))

        self.axis.set_ylim(0, MAX_RPM_DISPLAY)
        self.speed_axis.set_ylim(0, MAX_SPEED_MPS_DISPLAY)

        self.canvas.draw_idle()

    def on_close(self):
        self.disconnect()
        self.destroy()


if __name__ == "__main__":
    app = TelemetryMonitor()
    app.mainloop()
